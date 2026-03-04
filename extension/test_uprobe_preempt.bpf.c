// SPDX-License-Identifier: GPL-2.0
/*
 * test_uprobe_preempt.bpf.c - Sleepable uprobe → kfunc preempt
 *
 * Attach to cuLaunchKernel → preempt BE's TSG when LC launches a kernel.
 * No bpf_wq needed (sleepable uprobe runs in process context).
 *
 * Usage scenario:
 *   - Set target_tsg to BE's TSG handles
 *   - Attach uprobe to LC's cuLaunchKernel
 *   - When LC launches a kernel, BE gets preempted → LC gets GPU immediately
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* kfunc declaration */
extern int bpf_nv_gpu_preempt_tsg(u32 hClient, u32 hTsg) __ksym;

struct tsg_entry {
	__u32 hClient;
	__u32 hTsg;
};

/* Target TSG to preempt (BE's TSG, set by userspace) */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct tsg_entry);
} target_tsg SEC(".maps");

/* Stats */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 8);
	__type(key, __u32);
	__type(value, __u64);
} uprobe_stats SEC(".maps");

#define STAT_UPROBE_HIT    0
#define STAT_PREEMPT_OK    1
#define STAT_PREEMPT_ERR   2
#define STAT_LAST_NS       3
#define STAT_TOTAL_NS      4
#define STAT_SKIPPED       5

/* Latency samples (up to 128) */
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 128);
	__type(key, __u32);
	__type(value, __u64);
} uprobe_latency SEC(".maps");

/* Config: enable/disable (set to 0 to skip preempt) */
const volatile __u32 enabled = 1;

static void inc_stat(__u32 idx)
{
	__u64 *val = bpf_map_lookup_elem(&uprobe_stats, &idx);
	if (val)
		__sync_fetch_and_add(val, 1);
}

static void add_stat(__u32 idx, __u64 delta)
{
	__u64 *val = bpf_map_lookup_elem(&uprobe_stats, &idx);
	if (val)
		__sync_fetch_and_add(val, delta);
}

/*
 * Sleepable uprobe on cuLaunchKernel.
 * When LC calls cuLaunchKernel, preempt BE's TSG so LC gets GPU priority.
 */
SEC("uprobe.s//usr/lib/x86_64-linux-gnu/libcuda.so:cuLaunchKernel")
int preempt_on_launch(struct pt_regs *ctx)
{
	inc_stat(STAT_UPROBE_HIT);

	if (!enabled) {
		inc_stat(STAT_SKIPPED);
		return 0;
	}

	__u32 zero = 0;
	struct tsg_entry *tsg = bpf_map_lookup_elem(&target_tsg, &zero);
	if (!tsg || tsg->hClient == 0)
		return 0;

	__u64 start = bpf_ktime_get_ns();
	int ret = bpf_nv_gpu_preempt_tsg(tsg->hClient, tsg->hTsg);
	__u64 elapsed = bpf_ktime_get_ns() - start;

	/* Record latency */
	__u32 k3 = STAT_LAST_NS;
	__u64 *v3 = bpf_map_lookup_elem(&uprobe_stats, &k3);
	if (v3) *v3 = elapsed;
	add_stat(STAT_TOTAL_NS, elapsed);

	/* Record in latency samples ring */
	__u32 hit_key = STAT_UPROBE_HIT;
	__u64 *hit_cnt = bpf_map_lookup_elem(&uprobe_stats, &hit_key);
	if (hit_cnt) {
		__u32 idx = ((__u32)(*hit_cnt - 1)) % 128;
		bpf_map_update_elem(&uprobe_latency, &idx, &elapsed, BPF_ANY);
	}

	if (ret == 0) {
		inc_stat(STAT_PREEMPT_OK);
	} else {
		inc_stat(STAT_PREEMPT_ERR);
		bpf_printk("uprobe preempt FAIL: ret=%d\n", ret);
	}

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
