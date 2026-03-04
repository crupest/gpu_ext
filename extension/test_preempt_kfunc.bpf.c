// SPDX-License-Identifier: GPL-2.0
/*
 * test_preempt_kfunc.bpf.c - End-to-end test for bpf_nv_gpu_preempt_tsg kfunc
 *
 * Combines handle capture (3-probe kprobe strategy) with kfunc invocation
 * via bpf_wq from struct_ops hooks.
 *
 * Architecture:
 *   - kprobes (non-sleepable): capture hClient/hTsg from ioctl path
 *   - struct_ops (has bpf_wq): check trigger map, fire preempt kfunc
 *
 * Flow:
 *   1. kprobe/nvidia_unlocked_ioctl: intercept TSG alloc (class 0xa06c)
 *   2. kprobe/nv_gpu_sched_task_init: capture tsg_id + engine_type
 *   3. kretprobe/nvidia_unlocked_ioctl: read hTsg from user-space NVOS21
 *   4. struct_ops on_task_init: check trigger → bpf_wq → kfunc preempt
 *
 * The trigger fires on the NEXT struct_ops hook call after userspace arms it.
 * This naturally happens when a new CUDA workload starts (on_task_init) or
 * when the TSG binds (on_bind).
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "gpu_sched_set_timeslices.h"

/* kfunc declaration */
extern int bpf_nv_gpu_preempt_tsg(u32 hClient, u32 hTsg) __ksym;

#define NV_ESC_RM_ALLOC      0x2B
#define NV_ESC_IOCTL_XFER_NR 211
#define TSG_CLASS_A06C       0xa06c

struct tsg_entry {
	__u32 hClient;
	__u32 hTsg;
	__u32 engine_type;
	__u32 pid;
	__u64 tsg_id;
};

/* Pending alloc context (between kprobe entry and kretprobe exit) */
struct pending_alloc {
	__u64 nvos21_ptr;
	__u32 hRoot;
	__u32 engine_type;
	__u64 tsg_id;
	__u8  has_engine;
};

/* Per-TID pending allocs */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 128);
	__type(key, __u64);
	__type(value, struct pending_alloc);
} pending_map SEC(".maps");

/* Captured TSG entries (indexed by sequential counter) */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 64);
	__type(key, __u32);
	__type(value, struct tsg_entry);
} tsg_map SEC(".maps");

/* Counter for sequential indexing */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} tsg_count SEC(".maps");

/* Stats counters */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 8);
	__type(key, __u32);
	__type(value, __u64);
} stats SEC(".maps");

#define STAT_TSG_CAPTURED   0
#define STAT_PREEMPT_OK     1
#define STAT_PREEMPT_ERR    2
#define STAT_WQ_FIRED       3
#define STAT_STRUCT_OPS_HIT 4
#define STAT_LAST_KFUNC_NS  5  /* last kfunc-only latency in ns */
#define STAT_LAST_WQ_NS     6  /* last bpf_wq→kfunc latency in ns */

static void inc_stat(__u32 idx)
{
	__u64 *val = bpf_map_lookup_elem(&stats, &idx);
	if (val)
		__sync_fetch_and_add(val, 1);
}

/* ==================== bpf_wq section (struct_ops only) ==================== */

/* Work queue for deferred preempt (kfunc is sleepable) */
struct preempt_work_ctx {
	struct bpf_wq wq;
	__u32 hClient;
	__u32 hTsg;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct preempt_work_ctx);
} preempt_wq_map SEC(".maps");

/*
 * Trigger map:
 *   [0] = target_tsg_idx + 1 (non-zero = armed), consumed on fire
 *   [1] = repeat count (if > 0, auto-rearm after fire)
 */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, __u32);
} trigger SEC(".maps");

/* Latency samples array (ring buffer, up to 128 entries) */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 128);
	__type(key, __u32);
	__type(value, __u64);  /* kfunc latency in ns */
} latency_samples SEC(".maps");

/* bpf_wq callback: runs in sleepable kworker context */
static int preempt_wq_callback(void *map, int *key, void *value)
{
	struct preempt_work_ctx *wctx = (struct preempt_work_ctx *)value;
	int ret;
	__u64 wq_start = bpf_ktime_get_ns();

	inc_stat(STAT_WQ_FIRED);

	__u64 kfunc_start = bpf_ktime_get_ns();
	ret = bpf_nv_gpu_preempt_tsg(wctx->hClient, wctx->hTsg);
	__u64 kfunc_end = bpf_ktime_get_ns();

	__u64 kfunc_ns = kfunc_end - kfunc_start;
	__u64 wq_ns = kfunc_end - wq_start;

	/* Record latencies in stats */
	__u32 k5 = STAT_LAST_KFUNC_NS;
	__u64 *v5 = bpf_map_lookup_elem(&stats, &k5);
	if (v5) *v5 = kfunc_ns;

	__u32 k6 = STAT_LAST_WQ_NS;
	__u64 *v6 = bpf_map_lookup_elem(&stats, &k6);
	if (v6) *v6 = wq_ns;

	/* Record in latency_samples ring (use wq_count - 1 since inc_stat already ran) */
	__u32 wq_count_key = STAT_WQ_FIRED;
	__u64 *wq_count = bpf_map_lookup_elem(&stats, &wq_count_key);
	if (wq_count) {
		__u32 idx = ((__u32)(*wq_count - 1)) % 128;
		bpf_map_update_elem(&latency_samples, &idx, &kfunc_ns, BPF_ANY);
	}

	if (ret == 0) {
		inc_stat(STAT_PREEMPT_OK);
		bpf_printk("kfunc OK: %llu ns (wq+kfunc=%llu ns) hClient=0x%x\n",
			   kfunc_ns, wq_ns, wctx->hClient);
	} else {
		inc_stat(STAT_PREEMPT_ERR);
		bpf_printk("kfunc FAIL: ret=%d hClient=0x%x hTsg=0x%x\n",
			   ret, wctx->hClient, wctx->hTsg);
	}

	return 0;
}

/*
 * check_and_fire_preempt - Called from struct_ops hooks.
 * When trigger[0] != 0, look up the target TSG and fire bpf_wq.
 */
static __always_inline void check_and_fire_preempt(void)
{
	__u32 zero = 0;
	__u32 *trig = bpf_map_lookup_elem(&trigger, &zero);
	if (!trig || *trig == 0)
		return;

	__u32 tsg_idx = *trig - 1;

	/* Check repeat count */
	__u32 one = 1;
	__u32 *repeat = bpf_map_lookup_elem(&trigger, &one);
	if (repeat && *repeat > 0) {
		*repeat = *repeat - 1;
		/* keep trigger armed for next hook call */
	} else {
		*trig = 0; /* consume trigger (last shot) */
	}

	struct tsg_entry *entry = bpf_map_lookup_elem(&tsg_map, &tsg_idx);
	if (!entry || entry->hClient == 0)
		return;

	struct preempt_work_ctx *wctx = bpf_map_lookup_elem(&preempt_wq_map,
							    &zero);
	if (!wctx)
		return;

	wctx->hClient = entry->hClient;
	wctx->hTsg = entry->hTsg;

	bpf_wq_init(&wctx->wq, &preempt_wq_map, 0);
	if (bpf_wq_set_callback_impl(&wctx->wq, preempt_wq_callback,
				     0, NULL) == 0) {
		bpf_wq_start(&wctx->wq, 0);
		bpf_printk("trigger: preempt idx=%u hClient=0x%x hTsg=0x%x\n",
			   tsg_idx, entry->hClient, entry->hTsg);
	}
}

/* struct_ops: on_task_init — check trigger on every TSG creation */
SEC("struct_ops/on_task_init")
int BPF_PROG(on_task_init, struct nv_gpu_task_init_ctx *init_ctx)
{
	inc_stat(STAT_STRUCT_OPS_HIT);
	check_and_fire_preempt();
	return 0;
}

/* struct_ops: on_bind — check trigger on every TSG bind */
SEC("struct_ops/on_bind")
int BPF_PROG(on_bind, struct nv_gpu_bind_ctx *bind_ctx)
{
	inc_stat(STAT_STRUCT_OPS_HIT);
	check_and_fire_preempt();
	return 0;
}

/* struct_ops: on_task_destroy — no-op, required for completeness */
SEC("struct_ops/on_task_destroy")
int BPF_PROG(on_task_destroy, struct nv_gpu_task_destroy_ctx *destroy_ctx)
{
	return 0;
}

SEC(".struct_ops.link")
struct nv_gpu_sched_ops preempt_kfunc_ops = {
	.on_task_init    = (void *)on_task_init,
	.on_bind         = (void *)on_bind,
	.on_task_destroy = (void *)on_task_destroy,
};

/* ==================== kprobe section (handle capture) ==================== */

/* Target PID filter (0 = capture all) */
const volatile __u32 target_pid = 0;

/*
 * Probe 1: kprobe on nvidia_unlocked_ioctl entry
 */
SEC("kprobe/nvidia_unlocked_ioctl")
int capture_ioctl_entry(struct pt_regs *ctx)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	if (target_pid && pid != target_pid)
		return 0;

	unsigned int cmd = (unsigned int)PT_REGS_PARM2(ctx);
	unsigned long i_arg = PT_REGS_PARM3(ctx);
	unsigned int nr = cmd & 0xFF;

	__u64 nvos21_ptr = 0;

	if (nr == NV_ESC_IOCTL_XFER_NR) {
		__u32 inner_cmd = 0;
		bpf_probe_read_user(&inner_cmd, sizeof(__u32), (void *)i_arg);
		if (inner_cmd != NV_ESC_RM_ALLOC)
			return 0;
		bpf_probe_read_user(&nvos21_ptr, sizeof(__u64),
				    (void *)(i_arg + 8));
	} else if (nr == NV_ESC_RM_ALLOC) {
		nvos21_ptr = i_arg;
	} else {
		return 0;
	}

	if (!nvos21_ptr)
		return 0;

	__u32 hClass = 0;
	bpf_probe_read_user(&hClass, sizeof(__u32),
			    (void *)(nvos21_ptr + 12));
	if (hClass != TSG_CLASS_A06C)
		return 0;

	__u32 hRoot = 0;
	bpf_probe_read_user(&hRoot, sizeof(__u32), (void *)nvos21_ptr);

	struct pending_alloc pa = {};
	pa.nvos21_ptr = nvos21_ptr;
	pa.hRoot = hRoot;
	pa.has_engine = 0;

	__u64 pid_tgid = bpf_get_current_pid_tgid();
	bpf_map_update_elem(&pending_map, &pid_tgid, &pa, BPF_ANY);

	return 0;
}

/*
 * Probe 2: kprobe on nv_gpu_sched_task_init
 */
SEC("kprobe/nv_gpu_sched_task_init")
int capture_engine_type(struct pt_regs *ctx)
{
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	struct pending_alloc *pa = bpf_map_lookup_elem(&pending_map, &pid_tgid);
	if (!pa)
		return 0;

	void *init_ctx = (void *)PT_REGS_PARM1(ctx);

	__u64 tsg_id = 0;
	__u32 engine_type = 0;
	bpf_probe_read_kernel(&tsg_id, sizeof(__u64), init_ctx);
	bpf_probe_read_kernel(&engine_type, sizeof(__u32), init_ctx + 8);

	pa->tsg_id = tsg_id;
	pa->engine_type = engine_type;
	pa->has_engine = 1;

	return 0;
}

/*
 * Probe 3: kretprobe on nvidia_unlocked_ioctl
 */
SEC("kretprobe/nvidia_unlocked_ioctl")
int capture_ioctl_exit(struct pt_regs *ctx)
{
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	struct pending_alloc *pa = bpf_map_lookup_elem(&pending_map, &pid_tgid);
	if (!pa)
		return 0;

	__u64 nvos21_ptr = pa->nvos21_ptr;
	__u32 hRoot = pa->hRoot;
	__u32 engine_type = pa->engine_type;
	__u64 tsg_id = pa->tsg_id;
	__u8 has_engine = pa->has_engine;

	bpf_map_delete_elem(&pending_map, &pid_tgid);

	__u32 hObjectNew = 0;
	bpf_probe_read_user(&hObjectNew, sizeof(__u32),
			    (void *)(nvos21_ptr + 8));

	if (hObjectNew == 0)
		return 0;

	struct tsg_entry entry = {};
	entry.hClient = hRoot;
	entry.hTsg = hObjectNew;
	entry.engine_type = has_engine ? engine_type : 0xFF;
	entry.pid = pid_tgid >> 32;
	entry.tsg_id = has_engine ? tsg_id : 0;

	__u32 zero = 0;
	__u32 *cnt = bpf_map_lookup_elem(&tsg_count, &zero);
	if (!cnt)
		return 0;

	__u32 idx = *cnt;
	__sync_fetch_and_add(cnt, 1);
	bpf_map_update_elem(&tsg_map, &idx, &entry, BPF_ANY);

	inc_stat(STAT_TSG_CAPTURED);

	bpf_printk("TSG captured: idx=%u hClient=0x%x hTsg=0x%x engine=%u\n",
		   idx, hRoot, hObjectNew, entry.engine_type);

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
