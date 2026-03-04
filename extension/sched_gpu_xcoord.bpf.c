/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord CPU-side ADAPTIVE: GPU-State-Driven sched_ext Scheduler
 *
 * Reads gpu_state_map from gpu_ext to make informed scheduling decisions:
 *   - Auto-detects GPU processes (no -p PID required for UVM workloads)
 *   - Only boosts when GPU is thrashing (fault_rate > threshold)
 *   - Non-GPU tasks stay in local DSQ (preserves CFS per-CPU locality)
 *   - Manually registered PIDs (-p) always get boosted (for non-UVM workloads)
 *
 * Compared to sched_gpu_baseline:
 *   - Lower overhead: non-thrashing GPU tasks get normal scheduling
 *   - Better for mixed workloads: non-GPU tasks keep CFS locality
 *   - GPU-aware: adapts boost level based on real-time fault rate
 *
 * Based on scx_simple from Linux kernel tools/sched_ext/.
 */
#include <scx/common.bpf.h>
#include "shared_maps.h"

char _license[] SEC("license") = "GPL";

/*
 * Shared GPU state map -- replaced with pinned map from gpu_ext
 * via bpf_map__reuse_fd() in the userspace loader.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_PIDS);
	__type(key, __u32);
	__type(value, struct gpu_pid_state);
} gpu_state_map SEC(".maps");

/*
 * UVM worker thread tracking -- replaced with pinned map from gpu_ext.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_WORKERS);
	__type(key, __u32);
	__type(value, __u64);
} uvm_worker_pids SEC(".maps");

/*
 * Manually registered GPU process PIDs (-p flag).
 * These are ALWAYS boosted regardless of gpu_state_map.
 * Use for workloads that fit in VRAM (no UVM faults).
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_GPU_PROCS);
	__type(key, __u32);
	__type(value, __u32);
} gpu_process_pids SEC(".maps");

/* Per-CPU statistics: [local, global, gpu_boosted, gpu_state_boosted] */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, 5);
} stats SEC(".maps");

enum stat_idx {
	STAT_LOCAL = 0,
	STAT_GLOBAL = 1,
	STAT_GPU_BOOSTED = 2,       /* Boosted via -p PID (always) */
	STAT_GPU_STATE_BOOSTED = 3, /* Boosted via gpu_state_map (adaptive) */
	STAT_GPU_SKIPPED = 4,       /* GPU task but not thrashing, normal sched */
};

static u64 vtime_now;

volatile int exit_kind;

#define SHARED_DSQ 0
#define GPU_BOOST_DSQ 1

/* Configurable via rodata */
const volatile u64 fault_rate_boost_threshold = XCOORD_FAULT_RATE_HIGH;
const volatile u64 slice_boost_ns = 40000000ULL; /* 40ms */

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

/*
 * Boost level for a task:
 *   2 = always boost (manually registered -p PID)
 *   1 = adaptive boost (gpu_state_map shows thrashing OR active UVM worker)
 *   0 = no boost (normal scheduling)
 */
static int gpu_boost_level(struct task_struct *p)
{
	u32 tgid = p->tgid;
	u32 pid = p->pid;
	u32 *boost;
	u64 *worker_ts;
	struct gpu_pid_state *state;

	/* Priority 1: manually registered PIDs always boost */
	boost = bpf_map_lookup_elem(&gpu_process_pids, &tgid);
	if (boost && *boost)
		return 2;

	/* Priority 2: active UVM worker threads */
	worker_ts = bpf_map_lookup_elem(&uvm_worker_pids, &pid);
	if (worker_ts) {
		u64 now = bpf_ktime_get_ns();
		if (now - *worker_ts < XCOORD_WORKER_TIMEOUT_NS)
			return 1;
	}

	/* Priority 3: GPU process with high fault rate (auto-detected) */
	state = bpf_map_lookup_elem(&gpu_state_map, &tgid);
	if (state && state->is_thrashing)
		return 1;

	return 0;
}

s32 BPF_STRUCT_OPS(gpu_xcoord_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	bool is_idle = false;
	s32 cpu;

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
	if (is_idle) {
		int level = gpu_boost_level(p);
		if (level > 0) {
			/* GPU task: don't fast-path, let enqueue() handle */
			return cpu;
		}
		/* Non-GPU task: fast-path to local DSQ when idle */
		stat_inc(STAT_LOCAL);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
	}

	return cpu;
}

void BPF_STRUCT_OPS(gpu_xcoord_enqueue, struct task_struct *p, u64 enq_flags)
{
	int level = gpu_boost_level(p);

	if (level == 2) {
		/* Manually registered: always boost */
		stat_inc(STAT_GPU_BOOSTED);
		scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, slice_boost_ns,
				    enq_flags);
		return;
	}

	if (level == 1) {
		/* GPU-state-driven: adaptive boost */
		stat_inc(STAT_GPU_STATE_BOOSTED);
		scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, slice_boost_ns,
				    enq_flags);
		return;
	}

	/*
	 * Non-GPU tasks → SHARED_DSQ (lower priority than GPU_BOOST_DSQ).
	 *
	 * IMPORTANT: Cannot use SCX_DSQ_LOCAL here! The kernel dispatches
	 * local DSQ tasks BEFORE calling dispatch(), which means stress-ng
	 * tasks would bypass GPU_BOOST_DSQ priority entirely.
	 *
	 * Using SHARED_DSQ ensures dispatch() drains GPU_BOOST_DSQ first,
	 * then SHARED_DSQ — maintaining GPU task priority.
	 */
	stat_inc(STAT_GLOBAL);
	scx_bpf_dsq_insert(p, SHARED_DSQ, SCX_SLICE_DFL,
			    enq_flags);
}

void BPF_STRUCT_OPS(gpu_xcoord_dispatch, s32 cpu, struct task_struct *prev)
{
	/* GPU boost queue first (higher priority), then shared */
	scx_bpf_dsq_move_to_local(GPU_BOOST_DSQ);
	scx_bpf_dsq_move_to_local(SHARED_DSQ);
}

void BPF_STRUCT_OPS(gpu_xcoord_running, struct task_struct *p)
{
	if (time_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(gpu_xcoord_stopping, struct task_struct *p, bool runnable)
{
	p->scx.dsq_vtime += (SCX_SLICE_DFL - p->scx.slice) * 100 / p->scx.weight;
}

void BPF_STRUCT_OPS(gpu_xcoord_enable, struct task_struct *p)
{
	p->scx.dsq_vtime = vtime_now;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(gpu_xcoord_init)
{
	s32 ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret)
		return ret;
	return scx_bpf_create_dsq(GPU_BOOST_DSQ, -1);
}

void BPF_STRUCT_OPS(gpu_xcoord_exit, struct scx_exit_info *ei)
{
	exit_kind = 1;
}

SCX_OPS_DEFINE(gpu_xcoord_ops,
	       .select_cpu	= (void *)gpu_xcoord_select_cpu,
	       .enqueue		= (void *)gpu_xcoord_enqueue,
	       .dispatch	= (void *)gpu_xcoord_dispatch,
	       .running		= (void *)gpu_xcoord_running,
	       .stopping	= (void *)gpu_xcoord_stopping,
	       .enable		= (void *)gpu_xcoord_enable,
	       .init		= (void *)gpu_xcoord_init,
	       .exit		= (void *)gpu_xcoord_exit,
	       .name		= "gpu_xcoord");
