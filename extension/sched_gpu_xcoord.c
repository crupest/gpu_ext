/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord CPU-side ADAPTIVE: GPU-State-Driven scheduler loader
 *
 * Key differences from sched_gpu_baseline loader:
 *   - Reports gpu_state_boosted and gpu_skipped counters
 *   - Auto-detects GPU processes from gpu_state_map (in addition to -p PID)
 *   - Otherwise same structure: open pinned maps, reuse fds, attach struct_ops
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <libgen.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <scx/common.h>

#include "sched_gpu_xcoord.skel.h"
#include "shared_maps.h"

static bool verbose;
static volatile int exit_req;

static int libbpf_print_fn(enum libbpf_print_level level,
			    const char *format, va_list args)
{
	if (level == LIBBPF_DEBUG && !verbose)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sigint_handler(int sig)
{
	exit_req = 1;
}

static void read_stats(struct sched_gpu_xcoord_bpf *skel, __u64 *out)
{
	int nr_cpus = libbpf_num_possible_cpus();
	assert(nr_cpus > 0);

	memset(out, 0, sizeof(out[0]) * 5);

	for (__u32 idx = 0; idx < 5; idx++) {
		__u64 cnts[nr_cpus];
		int ret;

		ret = bpf_map_lookup_elem(bpf_map__fd(skel->maps.stats),
					  &idx, cnts);
		if (ret < 0)
			continue;
		for (int cpu = 0; cpu < nr_cpus; cpu++)
			out[idx] += cnts[cpu];
	}
}

#define MAX_GPU_PIDS 16

int main(int argc, char **argv)
{
	struct sched_gpu_xcoord_bpf *skel;
	struct bpf_link *link = NULL;
	int opt;
	__u64 threshold = XCOORD_FAULT_RATE_HIGH;
	int gpu_map_fd = -1;
	int workers_map_fd = -1;
	int err = 0;
	__u32 gpu_pids[MAX_GPU_PIDS];
	int n_gpu_pids = 0;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	while ((opt = getopt(argc, argv, "p:t:vh")) != -1) {
		switch (opt) {
		case 'p':
			if (n_gpu_pids < MAX_GPU_PIDS) {
				gpu_pids[n_gpu_pids++] = (__u32)atoi(optarg);
			} else {
				fprintf(stderr, "Too many -p PIDs (max %d)\n",
					MAX_GPU_PIDS);
			}
			break;
		case 't':
			threshold = atoll(optarg);
			break;
		case 'v':
			verbose = true;
			break;
		default:
			fprintf(stderr,
				"xCoord ADAPTIVE: GPU-state-driven sched_ext scheduler.\n\n"
				"Reads GPU state from %s and adapts CPU scheduling:\n"
				"  - Thrashing GPU tasks → GPU_BOOST_DSQ (priority boost)\n"
				"  - Non-thrashing GPU tasks → normal scheduling\n"
				"  - Non-GPU tasks → local DSQ (preserves CFS locality)\n"
				"  - Manually registered PIDs (-p) always get boosted\n\n"
				"Usage: %s [-v] [-t threshold] [-p PID ...]\n\n"
				"  -p PID   GPU process PID to always boost (can specify multiple)\n"
				"  -t RATE  Fault rate threshold for auto-boost (default: %d)\n"
				"  -v       Print libbpf debug messages\n"
				"  -h       Display this help and exit\n",
				XCOORD_GPU_STATE_PIN, basename(argv[0]),
				XCOORD_FAULT_RATE_HIGH);
			return opt != 'h';
		}
	}

	/* Open pinned maps from gpu_ext */
	gpu_map_fd = bpf_obj_get(XCOORD_GPU_STATE_PIN);
	if (gpu_map_fd < 0) {
		fprintf(stderr,
			"WARNING: Cannot open %s: %s\n"
			"  gpu_ext may not be running.\n"
			"  Auto-detection of GPU processes will not work.\n"
			"  Use -p PID to manually register GPU processes.\n\n",
			XCOORD_GPU_STATE_PIN, strerror(errno));
	} else {
		printf("Connected to gpu_ext gpu_state_map at %s\n",
		       XCOORD_GPU_STATE_PIN);
	}

	workers_map_fd = bpf_obj_get(XCOORD_UVM_WORKERS_PIN);
	if (workers_map_fd < 0) {
		fprintf(stderr,
			"WARNING: Cannot open %s: %s\n"
			"  UVM worker thread boosting will not work.\n\n",
			XCOORD_UVM_WORKERS_PIN, strerror(errno));
	} else {
		printf("Connected to gpu_ext uvm_worker_pids at %s\n",
		       XCOORD_UVM_WORKERS_PIN);
	}

	skel = sched_gpu_xcoord_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	skel->struct_ops.gpu_xcoord_ops->hotplug_seq = scx_hotplug_seq();
	SCX_ENUM_INIT(skel);

	skel->rodata->fault_rate_boost_threshold = threshold;

	/* Reuse pinned maps from gpu_ext */
	if (gpu_map_fd >= 0) {
		err = bpf_map__reuse_fd(skel->maps.gpu_state_map, gpu_map_fd);
		if (err) {
			fprintf(stderr, "Failed to reuse gpu_state_map: %s\n",
				strerror(-err));
		} else {
			printf("Successfully connected gpu_state_map\n");
		}
	}

	if (workers_map_fd >= 0) {
		err = bpf_map__reuse_fd(skel->maps.uvm_worker_pids,
					workers_map_fd);
		if (err) {
			fprintf(stderr, "Failed to reuse uvm_worker_pids: %s\n",
				strerror(-err));
		} else {
			printf("Successfully connected uvm_worker_pids\n");
		}
	}

	err = sched_gpu_xcoord_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
		goto cleanup;
	}

	link = bpf_map__attach_struct_ops(skel->maps.gpu_xcoord_ops);
	if (!link) {
		err = -errno;
		fprintf(stderr, "Failed to attach struct_ops: %s\n",
			strerror(errno));
		goto cleanup;
	}

	/* Populate gpu_process_pids from -p flags */
	if (n_gpu_pids > 0) {
		int proc_fd = bpf_map__fd(skel->maps.gpu_process_pids);
		for (int i = 0; i < n_gpu_pids; i++) {
			__u32 val = 1;
			err = bpf_map_update_elem(proc_fd, &gpu_pids[i],
						  &val, BPF_ANY);
			if (err) {
				fprintf(stderr,
					"WARNING: Failed to add PID %u: %s\n",
					gpu_pids[i], strerror(-err));
			} else {
				printf("Registered GPU process PID %u (always boost)\n",
				       gpu_pids[i]);
			}
		}
	}

	printf("xCoord ADAPTIVE scheduler loaded!\n");
	printf("  Fault rate threshold: %llu faults/sec\n",
	       (unsigned long long)threshold);
	printf("  Manual PIDs: %d registered (always boost)\n", n_gpu_pids);
	printf("  GPU state map: %s\n",
	       gpu_map_fd >= 0 ? "connected (auto-detect enabled)" :
				 "not available (manual PIDs only)");
	printf("  UVM worker map: %s\n",
	       workers_map_fd >= 0 ? "connected" : "not available");
	printf("\nPress Ctrl-C to exit...\n\n");

	while (!exit_req && !skel->bss->exit_kind) {
		__u64 stats[5];

		read_stats(skel, stats);
		printf("local=%llu pid_boost=%llu state_boost=%llu skipped=%llu\n",
		       stats[0], stats[2], stats[3], stats[4]);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);

cleanup:
	sched_gpu_xcoord_bpf__destroy(skel);

	if (gpu_map_fd >= 0)
		close(gpu_map_fd);
	if (workers_map_fd >= 0)
		close(workers_map_fd);

	return err < 0 ? -err : 0;
}
