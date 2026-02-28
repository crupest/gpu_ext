/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord Serving Policy: GPU-Aware sched_ext Scheduler
 *
 * Optimized for latency-sensitive GPU workloads (LLM serving).
 * GPU tasks get priority via dedicated GPU_BOOST_DSQ.
 * Non-GPU tasks go to local DSQ (minimal overhead).
 *
 * This works well for workloads with few GPU threads (llama.cpp,
 * vLLM) where global DSQ contention is not an issue.
 *
 * NOT suitable for multi-threaded batch workloads (FAISS, OpenMP)
 * where global DSQ causes serialization bottleneck.
 */
#include <scx/common.bpf.h>
#include "shared_maps.h"

char _license[] SEC("license") = "GPL";

/* Shared maps from gpu_ext (optional, connected via bpf_map__reuse_fd) */
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

/* GPU process PIDs — set by userspace loader (-p PID) */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, XCOORD_MAX_GPU_PROCS);
	__type(key, __u32);
	__type(value, __u32);
} gpu_process_pids SEC(".maps");

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

static u64 vtime_now;
volatile int exit_kind;

#define GPU_BOOST_DSQ 1

/* Configurable boost timeslice (default: 2x CFS default) */
const volatile u64 slice_boost_ns = 40000000ULL; /* 40ms */

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

static bool is_gpu_task(struct task_struct *p)
{
	u32 pid = p->tgid;
	u32 *boost;
	u64 *worker_ts;
	u64 now;

	boost = bpf_map_lookup_elem(&gpu_process_pids, &pid);
	if (boost && *boost)
		return true;

	worker_ts = bpf_map_lookup_elem(&uvm_worker_pids, &pid);
	if (worker_ts) {
		now = bpf_ktime_get_ns();
		if (now - *worker_ts < XCOORD_WORKER_TIMEOUT_NS)
			return true;
	}

	return false;
}

s32 BPF_STRUCT_OPS(gpu_serving_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	bool is_idle = false;
	s32 cpu;

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
	if (is_idle) {
		/* GPU tasks skip fast path → go through enqueue for boost */
		if (is_gpu_task(p))
			return cpu;

		stat_inc(STAT_LOCAL);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
	}

	return cpu;
}

void BPF_STRUCT_OPS(gpu_serving_enqueue, struct task_struct *p, u64 enq_flags)
{
	/*
	 * GPU tasks → global GPU_BOOST_DSQ with longer timeslice.
	 * This gives GPU threads priority over stress-ng/noisy neighbors.
	 *
	 * OK for serving workloads with few GPU threads (< 8).
	 * NOT OK for batch workloads with 24+ threads (use local DSQ).
	 */
	if (is_gpu_task(p)) {
		stat_inc(STAT_GPU_BOOSTED);
		scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, slice_boost_ns,
				    enq_flags);
		return;
	}

	stat_inc(STAT_GLOBAL);
	scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(gpu_serving_dispatch, s32 cpu, struct task_struct *prev)
{
	/* GPU tasks have highest priority */
	scx_bpf_dsq_move_to_local(GPU_BOOST_DSQ);
}

void BPF_STRUCT_OPS(gpu_serving_running, struct task_struct *p)
{
	if (time_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(gpu_serving_stopping, struct task_struct *p, bool runnable)
{
	p->scx.dsq_vtime += (SCX_SLICE_DFL - p->scx.slice) * 100 / p->scx.weight;
}

void BPF_STRUCT_OPS(gpu_serving_enable, struct task_struct *p)
{
	p->scx.dsq_vtime = vtime_now;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(gpu_serving_init)
{
	return scx_bpf_create_dsq(GPU_BOOST_DSQ, -1);
}

void BPF_STRUCT_OPS(gpu_serving_exit, struct scx_exit_info *ei)
{
	exit_kind = 1;
}

SCX_OPS_DEFINE(gpu_serving_ops,
	       .select_cpu	= (void *)gpu_serving_select_cpu,
	       .enqueue		= (void *)gpu_serving_enqueue,
	       .dispatch	= (void *)gpu_serving_dispatch,
	       .running		= (void *)gpu_serving_running,
	       .stopping	= (void *)gpu_serving_stopping,
	       .enable		= (void *)gpu_serving_enable,
	       .init		= (void *)gpu_serving_init,
	       .exit		= (void *)gpu_serving_exit,
	       .name		= "gpu_serving");
