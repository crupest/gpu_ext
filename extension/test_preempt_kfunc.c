// SPDX-License-Identifier: GPL-2.0
/*
 * test_preempt_kfunc.c - End-to-end test for bpf_nv_gpu_preempt_tsg kfunc
 *
 * Usage:
 *   sudo ./test_preempt_kfunc [-p PID]
 *
 * Steps:
 *   1. Load BPF, attach kprobes — capture TSG handles
 *   2. Start a CUDA workload
 *   3. Use interactive commands to list TSGs and fire preempt
 *   4. Preempt is triggered via: userspace writes trigger map →
 *      next nvidia ioctl kprobe hit → bpf_wq → kfunc → RM → GSP
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "test_preempt_kfunc.skel.h"

static volatile bool exiting = false;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sig_handler(int sig)
{
	exiting = true;
}

struct tsg_entry {
	__u32 hClient;
	__u32 hTsg;
	__u32 engine_type;
	__u32 pid;
	__u64 tsg_id;
};

static void print_stats(struct test_preempt_kfunc_bpf *skel)
{
	int fd = bpf_map__fd(skel->maps.stats);
	__u64 val;
	__u32 key;

	key = 0; bpf_map_lookup_elem(fd, &key, &val);
	printf("  tsg_captured: %llu\n", (unsigned long long)val);
	key = 1; bpf_map_lookup_elem(fd, &key, &val);
	printf("  preempt_ok:   %llu\n", (unsigned long long)val);
	key = 2; bpf_map_lookup_elem(fd, &key, &val);
	printf("  preempt_err:  %llu\n", (unsigned long long)val);
	key = 3; bpf_map_lookup_elem(fd, &key, &val);
	printf("  wq_fired:     %llu\n", (unsigned long long)val);
	key = 4; bpf_map_lookup_elem(fd, &key, &val);
	printf("  struct_ops:   %llu\n", (unsigned long long)val);
	key = 5; bpf_map_lookup_elem(fd, &key, &val);
	printf("  kfunc_ns:     %llu (%llu us)\n", (unsigned long long)val,
	       (unsigned long long)(val / 1000));
	key = 6; bpf_map_lookup_elem(fd, &key, &val);
	printf("  wq+kfunc_ns:  %llu (%llu us)\n", (unsigned long long)val,
	       (unsigned long long)(val / 1000));
}

static void list_tsgs(struct test_preempt_kfunc_bpf *skel)
{
	int map_fd = bpf_map__fd(skel->maps.tsg_map);
	int cnt_fd = bpf_map__fd(skel->maps.tsg_count);
	__u32 zero = 0, count = 0;

	bpf_map_lookup_elem(cnt_fd, &zero, &count);

	if (count == 0) {
		printf("No TSGs captured yet. Start a CUDA workload.\n");
		return;
	}

	printf("Captured TSGs (%u total):\n", count);
	printf("  %-5s %-12s %-12s %-8s %-8s %-10s\n",
	       "Idx", "hClient", "hTsg", "Engine", "PID", "tsg_id");

	for (__u32 i = 0; i < count && i < 64; i++) {
		struct tsg_entry entry;
		if (bpf_map_lookup_elem(map_fd, &i, &entry) == 0) {
			printf("  %-5u 0x%-10x 0x%-10x %-8u %-8u %-10llu\n",
			       i, entry.hClient, entry.hTsg, entry.engine_type,
			       entry.pid, (unsigned long long)entry.tsg_id);
		}
	}
}

/*
 * Fire preempt: write trigger map value = tsg_idx + 1.
 * The BPF kprobe on nvidia_unlocked_ioctl will pick this up
 * on the next ioctl call from the target process and fire bpf_wq.
 */
static int fire_preempt(struct test_preempt_kfunc_bpf *skel, __u32 tsg_idx)
{
	int map_fd = bpf_map__fd(skel->maps.tsg_map);
	int trigger_fd = bpf_map__fd(skel->maps.trigger);
	struct tsg_entry entry;

	if (bpf_map_lookup_elem(map_fd, &tsg_idx, &entry) != 0) {
		fprintf(stderr, "TSG index %u not found\n", tsg_idx);
		return -1;
	}

	printf("Arming preempt: idx=%u hClient=0x%x hTsg=0x%x pid=%u\n",
	       tsg_idx, entry.hClient, entry.hTsg, entry.pid);

	__u32 zero = 0;
	__u32 trigger_val = tsg_idx + 1; /* non-zero = armed */
	bpf_map_update_elem(trigger_fd, &zero, &trigger_val, BPF_ANY);

	return 0;
}

int main(int argc, char **argv)
{
	struct test_preempt_kfunc_bpf *skel;
	__u32 target_pid_val = 0;
	int err, opt;

	while ((opt = getopt(argc, argv, "p:h")) != -1) {
		switch (opt) {
		case 'p':
			target_pid_val = atoi(optarg);
			break;
		case 'h':
		default:
			fprintf(stderr, "Usage: %s [-p PID]\n", argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	skel = test_preempt_kfunc_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	skel->rodata->target_pid = target_pid_val;

	err = test_preempt_kfunc_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF: %d\n", err);
		goto cleanup;
	}

	/* Attach struct_ops (bpf_wq + kfunc path) */
	skel->links.preempt_kfunc_ops =
		bpf_map__attach_struct_ops(skel->maps.preempt_kfunc_ops);
	if (!skel->links.preempt_kfunc_ops) {
		fprintf(stderr, "Failed to attach struct_ops: %s\n",
			strerror(errno));
		err = -errno;
		goto cleanup;
	}

	/* Attach kprobes (handle capture) */
	skel->links.capture_ioctl_entry =
		bpf_program__attach(skel->progs.capture_ioctl_entry);
	if (!skel->links.capture_ioctl_entry) {
		fprintf(stderr, "Failed to attach kprobe entry: %s\n",
			strerror(errno));
		err = -errno;
		goto cleanup;
	}

	skel->links.capture_engine_type =
		bpf_program__attach(skel->progs.capture_engine_type);
	if (!skel->links.capture_engine_type) {
		fprintf(stderr, "Failed to attach kprobe task_init: %s\n",
			strerror(errno));
		err = -errno;
		goto cleanup;
	}

	skel->links.capture_ioctl_exit =
		bpf_program__attach(skel->progs.capture_ioctl_exit);
	if (!skel->links.capture_ioctl_exit) {
		fprintf(stderr, "Failed to attach kretprobe exit: %s\n",
			strerror(errno));
		err = -errno;
		goto cleanup;
	}

	printf("=== GPU Preempt kfunc End-to-End Test ===\n");
	if (target_pid_val)
		printf("Filtering PID: %u\n", target_pid_val);
	printf("Kprobes attached. Capturing TSG handles...\n");
	printf("Monitor: sudo cat /sys/kernel/tracing/trace_pipe\n");
	printf("\nCommands:\n");
	printf("  l        - List captured TSGs\n");
	printf("  p [idx]  - Preempt TSG (default idx=0)\n");
	printf("  s        - Show stats\n");
	printf("  q        - Quit\n\n");

	while (!exiting) {
		char buf[64];
		printf("> ");
		fflush(stdout);

		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;

		if (buf[0] == 'q' || buf[0] == 'Q')
			break;

		if (buf[0] == 'l' || buf[0] == 'L') {
			list_tsgs(skel);
		} else if (buf[0] == 'p' || buf[0] == 'P') {
			__u32 idx = 0;
			sscanf(buf + 1, "%u", &idx);

			struct timespec ts_start, ts_end;
			clock_gettime(CLOCK_MONOTONIC, &ts_start);

			if (fire_preempt(skel, idx) == 0) {
				/* Wait for bpf_wq to execute (fires on next struct_ops hook) */
				printf("Trigger armed. Waiting for next TSG init/bind...\n");
				/* Poll stats to detect preempt completion */
				int wait_ms = 0;
				__u64 old_wq = 0, new_wq = 0;
				__u32 key = 3; /* STAT_WQ_FIRED */
				int stats_fd = bpf_map__fd(skel->maps.stats);
				bpf_map_lookup_elem(stats_fd, &key, &old_wq);

				while (wait_ms < 5000 && !exiting) {
					usleep(10000); /* 10ms */
					wait_ms += 10;
					bpf_map_lookup_elem(stats_fd, &key, &new_wq);
					if (new_wq > old_wq)
						break;
				}

				clock_gettime(CLOCK_MONOTONIC, &ts_end);
				long elapsed_us = (ts_end.tv_sec - ts_start.tv_sec) * 1000000
					+ (ts_end.tv_nsec - ts_start.tv_nsec) / 1000;

				if (new_wq > old_wq)
					printf("Preempt completed in %ld us (end-to-end)\n",
					       elapsed_us);
				else
					printf("Timeout: no ioctl hit within %d ms. "
					       "Ensure CUDA workload is running.\n",
					       wait_ms);
			}
			printf("\n");
			print_stats(skel);
		} else if (buf[0] == 's' || buf[0] == 'S') {
			print_stats(skel);
		}
	}

	printf("\nFinal stats:\n");
	print_stats(skel);

cleanup:
	test_preempt_kfunc_bpf__destroy(skel);
	return err < 0 ? 1 : 0;
}
