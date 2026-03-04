// SPDX-License-Identifier: GPL-2.0
/*
 * test_uprobe_preempt.c - Sleepable uprobe → kfunc preempt loader
 *
 * Usage: sudo ./test_uprobe_preempt <hClient_hex> <hTsg_hex> [-p LC_PID]
 *
 * Sets BE's TSG handles, attaches sleepable uprobe to cuLaunchKernel.
 * When cuLaunchKernel is called (by LC or any process), BE gets preempted.
 *
 * With -p: attach only to LC_PID's cuLaunchKernel calls (uprobe filter).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "test_uprobe_preempt.skel.h"

static volatile bool exiting = false;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sig_handler(int sig) { exiting = true; }

struct tsg_entry {
	__u32 hClient;
	__u32 hTsg;
};

int main(int argc, char **argv)
{
	struct test_uprobe_preempt_bpf *skel;
	int err;
	int lc_pid = -1; /* -1 = all processes */

	if (argc < 3) {
		fprintf(stderr, "Usage: %s <hClient_hex> <hTsg_hex> [-p LC_PID]\n",
			argv[0]);
		return 1;
	}

	__u32 hClient = strtoul(argv[1], NULL, 16);
	__u32 hTsg = strtoul(argv[2], NULL, 16);

	for (int i = 3; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
			lc_pid = atoi(argv[++i]);
	}

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	skel = test_uprobe_preempt_bpf__open_and_load();
	if (!skel) {
		fprintf(stderr, "Failed to open/load BPF: %s\n", strerror(errno));
		return 1;
	}

	/* Write target TSG (BE's handles) */
	struct tsg_entry tsg = { .hClient = hClient, .hTsg = hTsg };
	__u32 zero = 0;
	bpf_map_update_elem(bpf_map__fd(skel->maps.target_tsg), &zero, &tsg, BPF_ANY);

	/* Attach uprobe — optionally filter by PID */
	if (lc_pid > 0) {
		LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts,
			.func_name = "cuLaunchKernel",
		);
		skel->links.preempt_on_launch = bpf_program__attach_uprobe_opts(
			skel->progs.preempt_on_launch,
			lc_pid,
			"/usr/lib/x86_64-linux-gnu/libcuda.so",
			0, /* offset 0 = resolve by func_name */
			&uprobe_opts);
	} else {
		/* Attach to all processes */
		skel->links.preempt_on_launch =
			bpf_program__attach(skel->progs.preempt_on_launch);
	}

	if (!skel->links.preempt_on_launch) {
		err = -errno;
		fprintf(stderr, "Failed to attach uprobe: %d (%s)\n",
			err, strerror(-err));
		goto cleanup;
	}

	printf("=== Uprobe Preempt Active ===\n");
	printf("  Target (BE): hClient=0x%x hTsg=0x%x\n", hClient, hTsg);
	if (lc_pid > 0)
		printf("  Filter: PID=%d (LC process only)\n", lc_pid);
	else
		printf("  Filter: all processes\n");
	printf("  Hook: cuLaunchKernel → bpf_nv_gpu_preempt_tsg()\n");
	printf("  Ctrl-C to stop.\n\n");

	while (!exiting) {
		sleep(1);

		int fd = bpf_map__fd(skel->maps.uprobe_stats);
		__u64 hits, ok, errs, last_ns, total_ns, skipped;
		__u32 key;

		key = 0; bpf_map_lookup_elem(fd, &key, &hits);
		key = 1; bpf_map_lookup_elem(fd, &key, &ok);
		key = 2; bpf_map_lookup_elem(fd, &key, &errs);
		key = 3; bpf_map_lookup_elem(fd, &key, &last_ns);
		key = 4; bpf_map_lookup_elem(fd, &key, &total_ns);
		key = 5; bpf_map_lookup_elem(fd, &key, &skipped);

		if (hits > 0) {
			__u64 avg_ns = ok > 0 ? total_ns / ok : 0;
			printf("  hits=%llu preempt_ok=%llu err=%llu skip=%llu "
			       "last=%llu us avg=%llu us\n",
			       (unsigned long long)hits,
			       (unsigned long long)ok,
			       (unsigned long long)errs,
			       (unsigned long long)skipped,
			       (unsigned long long)(last_ns / 1000),
			       (unsigned long long)(avg_ns / 1000));
		}
	}

	printf("\n=== Final Stats ===\n");
	{
		int fd = bpf_map__fd(skel->maps.uprobe_stats);
		__u64 val;
		__u32 key;
		key = 0; bpf_map_lookup_elem(fd, &key, &val);
		printf("  uprobe_hits:  %llu\n", (unsigned long long)val);
		key = 1; bpf_map_lookup_elem(fd, &key, &val);
		printf("  preempt_ok:   %llu\n", (unsigned long long)val);
		key = 2; bpf_map_lookup_elem(fd, &key, &val);
		printf("  preempt_err:  %llu\n", (unsigned long long)val);
		key = 4; bpf_map_lookup_elem(fd, &key, &val);
		__u64 total = val;
		key = 1; bpf_map_lookup_elem(fd, &key, &val);
		if (val > 0)
			printf("  avg_latency:  %llu us\n",
			       (unsigned long long)(total / val / 1000));
	}

cleanup:
	test_uprobe_preempt_bpf__destroy(skel);
	return err < 0 ? 1 : 0;
}
