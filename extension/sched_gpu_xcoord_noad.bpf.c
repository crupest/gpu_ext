/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord NO-AUTODETECT baseline: Priority boost for registered PIDs only
 *
 * Same as sched_gpu_xcoord EXCEPT:
 *   - Does NOT auto-detect GPU processes from gpu_state_map
 *   - Only boosts: (1) manually registered PIDs (-p), (2) UVM workers
 *   - Auto-detected thrashing GPU processes get NORMAL scheduling
 *
 * Purpose: isolate whether coord's improvement comes from its novel
 * algorithm or simply from "not boosting the aggressor".
 *
 * Based on scx_simple from Linux kernel tools/sched_ext/.
 */
#include <scx/common.bpf.h>
#include "shared_maps.h"

char _license[] SEC("license") = "GPL";

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

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, 4);
} stats SEC(".maps");

enum stat_idx {
	STAT_LOCAL = 0,
	STAT_GLOBAL = 1,
	STAT_GPU_BOOSTED = 2,       /* Boosted via -p PID (always) */
	STAT_UVM_WORKER_BOOSTED = 3,/* UVM worker thread boosted */
};

static u64 vtime_now;
volatile int exit_kind;

#define SHARED_DSQ 0
#define GPU_BOOST_DSQ 1

const volatile u64 slice_boost_ns = 40000000ULL; /* 40ms */

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

/*
 * Boost level — NO auto-detect of GPU processes:
 *   2 = manually registered -p PID (always boost)
 *   1 = active UVM worker thread (boost)
 *   0 = everything else, including auto-detected GPU processes
 */
static int gpu_boost_level(struct task_struct *p)
{
	u32 tgid = p->tgid;
	u32 pid = p->pid;
	u32 *boost;
	u64 *worker_ts;

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

	/* NO Priority 3: deliberately skip gpu_state_map auto-detect */
	return 0;
}

s32 BPF_STRUCT_OPS(gpu_xcoord_noad_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	bool is_idle = false;
	s32 cpu;

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &is_idle);
	if (is_idle) {
		int level = gpu_boost_level(p);
		if (level > 0)
			return cpu;
		stat_inc(STAT_LOCAL);
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
	}

	return cpu;
}

void BPF_STRUCT_OPS(gpu_xcoord_noad_enqueue, struct task_struct *p,
		    u64 enq_flags)
{
	int level = gpu_boost_level(p);

	if (level == 2) {
		stat_inc(STAT_GPU_BOOSTED);
		scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, slice_boost_ns,
				    enq_flags);
		return;
	}

	if (level == 1) {
		stat_inc(STAT_UVM_WORKER_BOOSTED);
		scx_bpf_dsq_insert(p, GPU_BOOST_DSQ, slice_boost_ns,
				    enq_flags);
		return;
	}

	stat_inc(STAT_GLOBAL);
	scx_bpf_dsq_insert(p, SHARED_DSQ, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(gpu_xcoord_noad_dispatch, s32 cpu, struct task_struct *prev)
{
	scx_bpf_dsq_move_to_local(GPU_BOOST_DSQ);
	scx_bpf_dsq_move_to_local(SHARED_DSQ);
}

void BPF_STRUCT_OPS(gpu_xcoord_noad_running, struct task_struct *p)
{
	if (time_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(gpu_xcoord_noad_stopping, struct task_struct *p,
		    bool runnable)
{
	p->scx.dsq_vtime += (SCX_SLICE_DFL - p->scx.slice) * 100 /
			     p->scx.weight;
}

void BPF_STRUCT_OPS(gpu_xcoord_noad_enable, struct task_struct *p)
{
	p->scx.dsq_vtime = vtime_now;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(gpu_xcoord_noad_init)
{
	s32 ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret)
		return ret;
	return scx_bpf_create_dsq(GPU_BOOST_DSQ, -1);
}

void BPF_STRUCT_OPS(gpu_xcoord_noad_exit, struct scx_exit_info *ei)
{
	exit_kind = 1;
}

SCX_OPS_DEFINE(gpu_xcoord_noad_ops,
	       .select_cpu	= (void *)gpu_xcoord_noad_select_cpu,
	       .enqueue		= (void *)gpu_xcoord_noad_enqueue,
	       .dispatch	= (void *)gpu_xcoord_noad_dispatch,
	       .running		= (void *)gpu_xcoord_noad_running,
	       .stopping	= (void *)gpu_xcoord_noad_stopping,
	       .enable		= (void *)gpu_xcoord_noad_enable,
	       .init		= (void *)gpu_xcoord_noad_init,
	       .exit		= (void *)gpu_xcoord_noad_exit,
	       .name		= "gpu_xcoord_noad");
