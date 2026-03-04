// SPDX-License-Identifier: GPL-2.0
/*
 * test_preempt_multi.bpf.c - BPF program to capture GPU TSG handles
 *
 * Identical to test_preempt_demo.bpf.c — same 3-probe strategy for
 * capturing TSG handles from stock nvidia module.
 *
 * Separate file needed for independent skeleton generation
 * (test_preempt_multi.skel.h).
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define NV_ESC_RM_ALLOC      0x2B
#define NV_ESC_IOCTL_XFER_NR 211   /* _IOC_NR of NV_ESC_IOCTL_XFER_CMD */
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

/* Output: captured TSG entries */
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

/* Target PID filter (set from userspace, 0 = capture all) */
const volatile __u32 target_pid = 0;

SEC("kprobe/nvidia_unlocked_ioctl")
int capture_nvidia_ioctl_entry(struct pt_regs *ctx)
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

SEC("kprobe/nv_gpu_sched_task_init")
int capture_engine_type(struct pt_regs *ctx)
{
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	struct pending_alloc *pa = bpf_map_lookup_elem(&pending_map,
						       &pid_tgid);
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

SEC("kretprobe/nvidia_unlocked_ioctl")
int capture_nvidia_ioctl_exit(struct pt_regs *ctx)
{
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	struct pending_alloc *pa = bpf_map_lookup_elem(&pending_map,
						       &pid_tgid);
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

	return 0;
}

char LICENSE[] SEC("license") = "GPL";
