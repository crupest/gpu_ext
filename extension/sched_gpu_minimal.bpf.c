/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord Minimal Policy: GPU-Aware sched_ext Scheduler
 *
 * Minimal overhead variant — eliminates all unnecessary hooks to reduce
 * sched_ext overhead under high CPU contention. No vtime tracking, no
 * global DSQs, no cache-bouncing global variables.
 *
 * GPU tasks: detected via gpu_process_pids map, dispatched to local DSQ
 * with boosted timeslice.
 * Non-GPU tasks: dispatched to local DSQ with default timeslice.
 *
 * This is "policy_minimal" — compare with sched_gpu_baseline for the
 * full-featured version.
 */
#include <scx/common.bpf.h>
#include "shared_maps.h"

char _license[] SEC("license") = "GPL";

/* GPU process PIDs — set by userspace loader (-p PID) */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_GPU_PROCS);
	__type(key, __u32);
	__type(value, __u32);
} gpu_process_pids SEC(".maps");

/* UVM worker thread tracking (from gpu_ext) */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_WORKERS);
	__type(key, __u32);
	__type(value, __u64);
} uvm_worker_pids SEC(".maps");

/* Shared GPU state (from gpu_ext) */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_PIDS);
	__type(key, __u32);
	__type(value, struct gpu_pid_state);
} gpu_state_map SEC(".maps");

/* Per-CPU statistics */
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

/* Exit state */
volatile int exit_kind;

/* Configurable timeslice for boosted GPU tasks */
const volatile u64 slice_boost_ns = 20000000ULL; /* 20ms (same as default) */

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

/*
 * Minimal GPU task detection — single hash map lookup.
 * Only checks gpu_process_pids (direct registration via -p PID).
 * UVM worker tracking is skipped for minimal overhead.
 */
static bool is_gpu_task_fast(struct task_struct *p)
{
	u32 pid = p->tgid;
	u32 *boost = bpf_map_lookup_elem(&gpu_process_pids, &pid);
	return boost && *boost;
}

s32 BPF_STRUCT_OPS(gpu_minimal_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	bool is_idle = false;
	s32 cpu;

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
	if (is_idle) {
		stat_inc(STAT_LOCAL);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
	}
	return cpu;
}

void BPF_STRUCT_OPS(gpu_minimal_enqueue, struct task_struct *p, u64 enq_flags)
{
	if (is_gpu_task_fast(p)) {
		stat_inc(STAT_GPU_BOOSTED);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, slice_boost_ns,
				    enq_flags);
		return;
	}

	stat_inc(STAT_GLOBAL);
	scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, enq_flags);
}

/*
 * No dispatch() — all tasks already on local DSQ.
 * No running()/stopping()/enable() — no vtime tracking needed.
 * This eliminates cache-bouncing from global vtime_now variable.
 */

s32 BPF_STRUCT_OPS_SLEEPABLE(gpu_minimal_init)
{
	return 0;
}

void BPF_STRUCT_OPS(gpu_minimal_exit, struct scx_exit_info *ei)
{
	exit_kind = 1;
}

SCX_OPS_DEFINE(gpu_minimal_ops,
	       .select_cpu	= (void *)gpu_minimal_select_cpu,
	       .enqueue		= (void *)gpu_minimal_enqueue,
	       .init		= (void *)gpu_minimal_init,
	       .exit		= (void *)gpu_minimal_exit,
	       .name		= "gpu_minimal");
