/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FPRS: Fault-Pressure Regulated Scheduling
 *
 * A feedback-control-based CPU scheduler that uses GPU fault pressure
 * as the control signal. Instead of static priority assignment, FPRS
 * continuously regulates BE (best-effort) processes' CPU allocation
 * to maintain a QoS target for LC (latency-critical) processes.
 *
 * Control loop:
 *   1. SENSOR:    Read LC's fault_rate from gpu_state_map
 *   2. ERROR:     error = lc_fault_rate - target_lc_fault_rate
 *   3. INTEGRAL:  pressure_integral += error (with anti-windup)
 *                 OR decay when error < 0
 *   4. ACTUATOR:  be_throttle_pct = f(pressure_integral) ∈ [0, 1000]
 *                 → controls BE timeslice and DSQ placement
 *
 * Key differences from priority scheduling:
 *   - Closed-loop: adapts based on measured GPU impact, not static rules
 *   - Continuous:  throttle_pct ranges 0-1000, not binary boost/throttle
 *   - QoS-driven:  explicit target (lc_fault_rate < target)
 *   - Self-correcting: integral decays when LC is fine → BE recovers
 *
 * Based on scx_simple from Linux kernel tools/sched_ext/.
 */
#include <scx/common.bpf.h>
#include "shared_maps.h"

char _license[] SEC("license") = "GPL";

/* ── Shared maps (reused from gpu_ext via pinned BPF maps) ─────────── */

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_PIDS);
	__type(key, __u32);
	__type(value, struct gpu_pid_state);
} gpu_state_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_WORKERS);
	__type(key, __u32);
	__type(value, __u64);
} uvm_worker_pids SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_GPU_PROCS);
	__type(key, __u32);
	__type(value, __u32);
} gpu_process_pids SEC(".maps");

/*
 * LC PID array for regulate() iteration.
 * Hash maps can't be iterated in BPF; this array stores LC PIDs
 * so regulate() can look up each LC's fault_rate.
 * Populated by userspace loader from -p PID flags.
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, XCOORD_MAX_GPU_PROCS);
	__type(key, __u32);
	__type(value, __u32);
} lc_pid_array SEC(".maps");

/* ── Statistics ────────────────────────────────────────────────────── */

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, 8);
} stats SEC(".maps");

enum stat_idx {
	STAT_LOCAL = 0,             /* Non-GPU fast-path (idle CPU) */
	STAT_GLOBAL = 1,            /* Non-GPU normal (SHARED_DSQ) */
	STAT_LC_BOOSTED = 2,        /* LC process (-p PID) boosted */
	STAT_UVM_WORKER = 3,        /* UVM BH worker boosted */
	STAT_BE_REGULATED = 4,      /* BE GPU process (regulated) */
	STAT_BACKPRESSURE = 5,      /* Non-GPU throttled (backpressure) */
	STAT_REGULATE_CALLS = 6,    /* Number of regulate() invocations */
	STAT_BE_DEMOTED = 7,        /* BE moved to SHARED_DSQ (high pressure) */
};

/* ── DSQs ──────────────────────────────────────────────────────────── */

#define SHARED_DSQ 0
#define GPU_BOOST_DSQ 1

/* ── Controller state (BPF global variables) ──────────────────────── */

static u64 vtime_now;
volatile int exit_kind;

/*
 * Integral controller state — readable from userspace via bss.
 *
 * pressure_integral: accumulated error (lc_fault_rate - target).
 *   Grows when LC is under pressure, decays when LC is fine.
 *
 * be_throttle_pct: derived from pressure_integral, range [0, 1000].
 *   0 = BE runs freely, 1000 = BE fully throttled.
 *   Controls both timeslice and DSQ placement of BE tasks.
 *
 * last_regulate_ns: timestamp of last regulation step.
 *   regulate() runs at most once per regulate_interval_ns.
 *
 * lc_fault_rate_observed: last observed LC fault rate (for monitoring).
 */
volatile u64 pressure_integral = 0;
volatile u32 be_throttle_pct = 0;
volatile u64 last_regulate_ns = 0;
volatile u64 lc_fault_rate_observed = 0;

/* ── Tunable parameters (rodata, set before BPF load) ─────────────── */

/*
 * QoS target: LC should see fewer than this many faults/sec.
 * When lc_fault_rate > target → controller tightens BE.
 * When lc_fault_rate < target → controller relaxes BE.
 */
const volatile u64 target_lc_fault_rate = 100;

/*
 * Regulation interval: how often regulate() runs.
 * Too fast → noisy signal, oscillation.
 * Too slow → slow response to pressure changes.
 * 100ms is a good balance for GPU fault dynamics.
 */
const volatile u64 regulate_interval_ns = 100000000ULL; /* 100ms */

/*
 * Integral gain: how fast pressure_integral grows per unit error.
 * Higher → more aggressive throttling response.
 * Lower → more gradual response (but slower convergence).
 * With error~2000 and ki_gain=10, integral reaches 10000 in 500ms.
 */
const volatile u64 ki_gain = 10;

/*
 * Decay shift: how fast pressure_integral decays when LC is fine.
 * pressure_integral >>= decay_shift each interval when error < 0.
 * 2 = ÷4 per interval → ~200ms half-life at 100ms interval.
 * Balance between fast recovery and sustained throttling during
 * continuous interference.
 */
const volatile u32 decay_shift = 2;

/*
 * Anti-windup: maximum value for pressure_integral.
 * Prevents integral from growing unboundedly during sustained pressure.
 * Lower values = faster recovery after interference stops.
 */
const volatile u64 max_integral = 10000;

/* Timeslice parameters */
const volatile u64 base_slice_ns = 5000000ULL;    /* 5ms base */
const volatile u64 max_gpu_slice_ns = 80000000ULL; /* 80ms max for LC/UVM */
const volatile u64 min_be_slice_ns = 1000000ULL;   /* 1ms minimum for BE */
const volatile u64 throttle_slice_ns = 5000000ULL; /* 5ms for backpressure */
const volatile u64 fault_rate_scale = 500;         /* faults/s for 2x slice */
const volatile u64 max_slice_multiplier = 16;      /* max 16x base slice */

/* Number of LC PIDs (set by userspace loader) */
const volatile u32 n_lc_pids = 0;

/* ── Helper functions ─────────────────────────────────────────────── */

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

/*
 * Compute proportional timeslice for UVM workers based on fault rate.
 * More faults → longer uninterrupted time for fault resolution.
 */
static u64 compute_gpu_slice(u64 fault_rate)
{
	u64 multiplier, slice;

	if (fault_rate == 0)
		return base_slice_ns;

	multiplier = 1 + fault_rate / fault_rate_scale;
	if (multiplier > max_slice_multiplier)
		multiplier = max_slice_multiplier;

	slice = base_slice_ns * multiplier;
	if (slice > max_gpu_slice_ns)
		slice = max_gpu_slice_ns;

	return slice;
}

/*
 * REGULATE: The feedback controller.
 *
 * Called lazily when any task is enqueued. Runs at most once per
 * regulate_interval_ns. Reads LC fault pressure and adjusts
 * be_throttle_pct accordingly.
 *
 * This is the core algorithmic novelty of FPRS:
 * - Integral controller accumulates error over time
 * - Decay when LC is fine lets BE gradually recover
 * - Anti-windup prevents runaway integral
 * - be_throttle_pct provides continuous (not binary) control
 */
static void regulate(void)
{
	u64 now = bpf_ktime_get_ns();
	u64 max_lc_fr = 0;
	u64 integral;
	u32 throttle;

	if (now - last_regulate_ns < regulate_interval_ns)
		return;
	last_regulate_ns = now;

	stat_inc(STAT_REGULATE_CALLS);

	/*
	 * SENSOR: Find max fault_rate among registered LC processes.
	 *
	 * We use max (not sum) because each LC process independently
	 * needs QoS protection. If any LC is suffering, throttle BE.
	 *
	 * Loop bound is compile-time constant (XCOORD_MAX_GPU_PROCS=16)
	 * so BPF verifier accepts it.
	 */
	for (__u32 i = 0; i < XCOORD_MAX_GPU_PROCS; i++) {
		__u32 *pid_p;
		struct gpu_pid_state *state;

		if (i >= n_lc_pids)
			break;

		pid_p = bpf_map_lookup_elem(&lc_pid_array, &i);
		if (!pid_p || *pid_p == 0)
			continue;

		state = bpf_map_lookup_elem(&gpu_state_map, pid_p);
		if (state && state->fault_rate > max_lc_fr) {
			/*
			 * Only trust fault_rate if recently updated.
			 * gpu_state_map entries are only updated during
			 * page faults — when idle, stale values persist.
			 * Treat fault_rate as 0 if no update in 2 seconds.
			 */
			if (now - state->last_update_ns < 2000000000ULL)
				max_lc_fr = state->fault_rate;
		}
	}

	lc_fault_rate_observed = max_lc_fr;

	/* ERROR SIGNAL + INTEGRAL CONTROLLER */
	integral = pressure_integral;

	if (max_lc_fr > target_lc_fault_rate) {
		/*
		 * LC under pressure: accumulate error.
		 * The integral grows proportionally to how far above
		 * target we are, scaled by ki_gain.
		 */
		u64 error = max_lc_fr - target_lc_fault_rate;
		u64 increment = error * ki_gain;

		/* Saturating add with anti-windup */
		if (integral + increment > max_integral)
			integral = max_integral;
		else
			integral += increment;
	} else {
		/*
		 * LC is fine: DECAY integral.
		 * This lets BE gradually recover CPU time.
		 * Shift right = divide by 2^decay_shift per interval.
		 *
		 * With decay_shift=2: ÷4 per 100ms → ~200ms to halve.
		 * This ensures BE recovers within ~1s of LC pressure
		 * subsiding (integral drops below meaningful threshold).
		 */
		integral >>= decay_shift;
	}

	pressure_integral = integral;

	/* ACTUATOR: Convert integral to throttle percentage [0, 1000] */
	if (max_integral > 0) {
		throttle = (u32)(integral * 1000 / max_integral);
		if (throttle > 1000)
			throttle = 1000;
	} else {
		throttle = 0;
	}

	be_throttle_pct = throttle;
}

/*
 * Classify a task:
 *   3 = LC process (manually registered -p PID) → always max boost
 *   1 = UVM BH worker thread → proportional boost
 *   2 = auto-detected thrashing GPU process (potential BE) → regulated
 *   0 = non-GPU task → normal or backpressure
 *
 * Also sets *slice_out to recommended timeslice.
 */
static int classify_task(struct task_struct *p, u64 *slice_out)
{
	u32 tgid = p->tgid;
	u32 pid = p->pid;
	u32 *boost;
	u64 *worker_ts;
	struct gpu_pid_state *state;

	*slice_out = base_slice_ns;

	/* Priority 1: LC processes always get max boost */
	boost = bpf_map_lookup_elem(&gpu_process_pids, &tgid);
	if (boost && *boost) {
		*slice_out = max_gpu_slice_ns;
		return 3;
	}

	/* Priority 2: UVM BH worker threads — proportional to fault rate */
	worker_ts = bpf_map_lookup_elem(&uvm_worker_pids, &pid);
	if (worker_ts) {
		u64 now = bpf_ktime_get_ns();
		if (now - *worker_ts < XCOORD_WORKER_TIMEOUT_NS) {
			state = bpf_map_lookup_elem(&gpu_state_map, &tgid);
			if (state)
				*slice_out = compute_gpu_slice(state->fault_rate);
			else
				*slice_out = max_gpu_slice_ns / 2;
			return 1;
		}
	}

	/* Priority 3: auto-detected thrashing GPU process */
	state = bpf_map_lookup_elem(&gpu_state_map, &tgid);
	if (state && state->is_thrashing) {
		*slice_out = compute_gpu_slice(state->fault_rate);
		return 2;
	}

	return 0;
}

/* ── sched_ext struct_ops ─────────────────────────────────────────── */

s32 BPF_STRUCT_OPS(gpu_coord_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	bool is_idle = false;
	s32 cpu;

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
	if (is_idle) {
		u64 slice;
		int level = classify_task(p, &slice);
		if (level > 0)
			return cpu; /* GPU task: let enqueue() handle */
		stat_inc(STAT_LOCAL);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
	}

	return cpu;
}

void BPF_STRUCT_OPS(gpu_coord_enqueue, struct task_struct *p, u64 enq_flags)
{
	u64 slice;
	int level = classify_task(p, &slice);

	/* Run feedback controller (lazy, at most once per interval) */
	regulate();

	if (level == 3) {
		/* LC: always GPU_BOOST_DSQ, max timeslice */
		stat_inc(STAT_LC_BOOSTED);
		scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, slice, enq_flags);
		return;
	}

	if (level == 1) {
		/* UVM worker: always GPU_BOOST_DSQ, proportional timeslice */
		stat_inc(STAT_UVM_WORKER);
		scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, slice, enq_flags);
		return;
	}

	if (level == 2) {
		/*
		 * BE GPU process: REGULATED by feedback controller.
		 *
		 * The controller continuously adjusts be_throttle_pct
		 * based on LC's observed fault_rate:
		 *
		 * - throttle_pct=0:    BE runs freely (no LC pressure)
		 * - throttle_pct=500:  BE gets half timeslice, demoted to SHARED_DSQ
		 * - throttle_pct=1000: BE gets minimum timeslice, low priority
		 *
		 * When no LC PIDs registered (n_lc_pids=0), regulate()
		 * never sees fault pressure → throttle_pct stays 0 → BE
		 * gets full timeslice in GPU_BOOST_DSQ (same as xcoord).
		 */
		u32 tpct = be_throttle_pct;
		u64 be_slice;

		/* Continuous timeslice: scale from max down to min */
		if (tpct >= 1000) {
			be_slice = min_be_slice_ns;
		} else {
			be_slice = slice * (1000 - tpct) / 1000;
			if (be_slice < min_be_slice_ns)
				be_slice = min_be_slice_ns;
		}

		if (tpct > 300) {
			/* Moderate+ pressure: demote to SHARED_DSQ */
			stat_inc(STAT_BE_DEMOTED);
			scx_bpf_dsq_insert(p, SHARED_DSQ, be_slice,
					    enq_flags);
		} else {
			/* Low pressure: keep in GPU_BOOST_DSQ */
			stat_inc(STAT_BE_REGULATED);
			scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, be_slice,
					    enq_flags);
		}
		return;
	}

	/*
	 * Non-GPU task: SHARED_DSQ with default timeslice.
	 * We do NOT throttle non-GPU tasks — they include system services,
	 * network handlers, and LC's own helper threads. Throttling them
	 * causes collateral damage to LC (e.g., 45s latency spikes).
	 * Only GPU-classified tasks (level 2) get regulated.
	 */
	stat_inc(STAT_GLOBAL);
	scx_bpf_dsq_insert(p, SHARED_DSQ, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(gpu_coord_dispatch, s32 cpu, struct task_struct *prev)
{
	scx_bpf_dsq_move_to_local(GPU_BOOST_DSQ);
	scx_bpf_dsq_move_to_local(SHARED_DSQ);
}

void BPF_STRUCT_OPS(gpu_coord_running, struct task_struct *p)
{
	if (time_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(gpu_coord_stopping, struct task_struct *p, bool runnable)
{
	p->scx.dsq_vtime += (SCX_SLICE_DFL - p->scx.slice) * 100 /
			     p->scx.weight;
}

void BPF_STRUCT_OPS(gpu_coord_enable, struct task_struct *p)
{
	p->scx.dsq_vtime = vtime_now;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(gpu_coord_init)
{
	s32 ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret)
		return ret;
	return scx_bpf_create_dsq(GPU_BOOST_DSQ, -1);
}

void BPF_STRUCT_OPS(gpu_coord_exit, struct scx_exit_info *ei)
{
	exit_kind = 1;
}

SCX_OPS_DEFINE(gpu_coord_ops,
	       .select_cpu	= (void *)gpu_coord_select_cpu,
	       .enqueue		= (void *)gpu_coord_enqueue,
	       .dispatch	= (void *)gpu_coord_dispatch,
	       .running		= (void *)gpu_coord_running,
	       .stopping	= (void *)gpu_coord_stopping,
	       .enable		= (void *)gpu_coord_enable,
	       .init		= (void *)gpu_coord_init,
	       .exit		= (void *)gpu_coord_exit,
	       .name		= "gpu_coord");
