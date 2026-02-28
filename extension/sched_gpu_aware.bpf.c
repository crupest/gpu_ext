/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord CPU-side: GPU-Aware sched_ext Scheduler
 *
 * A simple sched_ext scheduler that reads GPU state from the shared
 * gpu_state_map and uvm_worker_pids map (written by eviction_lfu_xcoord)
 * to boost CPU scheduling priority for kernel threads actively handling
 * UVM page faults.
 *
 * Based on scx_simple from Linux kernel tools/sched_ext/.
 * Simplified to avoid UEI (32-bit atomics not supported by clang 18).
 */
#include <scx/common.bpf.h>
#include "shared_maps.h"

char _license[] SEC("license") = "GPL";

/*
 * Shared GPU state map -- will be replaced with the pinned map from
 * gpu_ext via bpf_map__reuse_fd() in the userspace loader.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_PIDS);
	__type(key, __u32);
	__type(value, struct gpu_pid_state);
} gpu_state_map SEC(".maps");

/*
 * UVM worker thread tracking -- replaced with the pinned map from
 * gpu_ext. Maps kernel worker thread PIDs to their last UVM activity
 * timestamp. Used to identify which threads to boost.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_WORKERS);
	__type(key, __u32);
	__type(value, __u64);
} uvm_worker_pids SEC(".maps");

/*
 * GPU process PIDs -- set by the userspace loader (-p PID).
 * Maps GPU process TGIDs to boost flag (1 = boost).
 * This directly boosts all threads of the GPU application,
 * not just UVM kworker threads. Essential for workloads
 * that fit in VRAM (no UVM paging during inference).
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_GPU_PROCS);
	__type(key, __u32);
	__type(value, __u32);
} gpu_process_pids SEC(".maps");

/* Per-CPU statistics: [local, global, gpu_boosted, gpu_throttled] */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, 4);
} stats SEC(".maps");

enum stat_idx {
	STAT_LOCAL = 0,
	STAT_GLOBAL = 1,
	STAT_GPU_BOOSTED = 2,
	STAT_GPU_THROTTLED = 3,
};

static u64 vtime_now;

/* Exit state (simplified, no UEI to avoid clang 18 atomic issues) */
volatile int exit_kind;

#define SHARED_DSQ 0
#define GPU_BOOST_DSQ 1

/* Configurable via rodata */
const volatile u64 fault_rate_boost_threshold = XCOORD_FAULT_RATE_HIGH;
const volatile u64 slice_boost_ns = 40000000ULL; /* 40ms (2x default 20ms) */

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

/*
 * is_gpu_task - Check if this task should receive GPU-aware boosting.
 *
 * Returns true if the task matches:
 *   1. A directly registered GPU process PID (-p flag), OR
 *   2. An active UVM worker thread (from gpu_ext tracking)
 */
static bool is_gpu_task(struct task_struct *p)
{
	u32 pid = p->tgid;
	u32 *boost;
	u64 *worker_ts;
	u64 now;

	/* Check direct GPU process registration */
	boost = bpf_map_lookup_elem(&gpu_process_pids, &pid);
	if (boost && *boost)
		return true;

	/* Check UVM worker thread tracking */
	worker_ts = bpf_map_lookup_elem(&uvm_worker_pids, &pid);
	if (worker_ts) {
		now = bpf_ktime_get_ns();
		if (now - *worker_ts < XCOORD_WORKER_TIMEOUT_NS)
			return true;
	}

	return false;
}

s32 BPF_STRUCT_OPS(gpu_aware_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	bool is_idle = false;
	s32 cpu;

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
	if (is_idle) {
		/*
		 * Don't fast-path GPU tasks to local DSQ — let them
		 * go through enqueue() where the boost logic runs.
		 * This is critical: without this, GPU tasks on idle
		 * CPUs bypass the boost entirely.
		 */
		if (is_gpu_task(p)) {
			/* Still use the selected CPU for cache warmth,
			 * but don't insert — enqueue() will handle it */
			return cpu;
		}

		stat_inc(STAT_LOCAL);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
	}

	return cpu;
}

void BPF_STRUCT_OPS(gpu_aware_enqueue, struct task_struct *p, u64 enq_flags)
{
	u64 vtime = p->scx.dsq_vtime;

	/*
	 * GPU tasks → GPU_BOOST_DSQ (FIFO, boosted timeslice).
	 * Non-GPU tasks → SHARED_DSQ (global PRIQ with vtime fairness).
	 *
	 * This is the POC-1 R2 winning configuration:
	 * - GPU tasks get dispatched first (priority boost via separate DSQ)
	 * - Non-GPU tasks compete fairly in SHARED_DSQ with vtime scheduling
	 * - The global SHARED_DSQ for stress-ng prevents local DSQ bypass
	 *
	 * NOT suitable for batch workloads (FAISS/GNN) with many GPU threads —
	 * use sched_gpu_serving (local DSQ) or no scheduler for those.
	 */
	if (is_gpu_task(p)) {
		stat_inc(STAT_GPU_BOOSTED);
		scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, slice_boost_ns,
				    enq_flags);
		return;
	}

	stat_inc(STAT_GLOBAL);
	scx_bpf_dsq_insert(p, SHARED_DSQ, SCX_SLICE_DFL,
			    enq_flags);
}

void BPF_STRUCT_OPS(gpu_aware_dispatch, s32 cpu, struct task_struct *prev)
{
	/* Drain GPU boost queue first (higher priority) */
	scx_bpf_dsq_move_to_local(GPU_BOOST_DSQ);
	scx_bpf_dsq_move_to_local(SHARED_DSQ);
}

void BPF_STRUCT_OPS(gpu_aware_running, struct task_struct *p)
{
	if (time_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(gpu_aware_stopping, struct task_struct *p, bool runnable)
{
	p->scx.dsq_vtime += (SCX_SLICE_DFL - p->scx.slice) * 100 / p->scx.weight;
}

void BPF_STRUCT_OPS(gpu_aware_enable, struct task_struct *p)
{
	p->scx.dsq_vtime = vtime_now;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(gpu_aware_init)
{
	s32 ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret)
		return ret;
	return scx_bpf_create_dsq(GPU_BOOST_DSQ, -1);
}

void BPF_STRUCT_OPS(gpu_aware_exit, struct scx_exit_info *ei)
{
	/* Simple exit recording without UEI (avoids 32-bit atomic issue) */
	exit_kind = 1;
}

SCX_OPS_DEFINE(gpu_aware_ops,
	       .select_cpu	= (void *)gpu_aware_select_cpu,
	       .enqueue		= (void *)gpu_aware_enqueue,
	       .dispatch	= (void *)gpu_aware_dispatch,
	       .running		= (void *)gpu_aware_running,
	       .stopping	= (void *)gpu_aware_stopping,
	       .enable		= (void *)gpu_aware_enable,
	       .init		= (void *)gpu_aware_init,
	       .exit		= (void *)gpu_aware_exit,
	       .name		= "gpu_aware");
