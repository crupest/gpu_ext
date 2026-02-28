/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord CPU-side BASELINE: Blind Priority Boost scheduler loader
 *
 * Loads sched_gpu_baseline.bpf.c and connects it to the shared gpu_state_map
 * pinned by eviction_lfu_xcoord at /sys/fs/bpf/xcoord_gpu_state.
 * This is the baseline (blind boost) for comparison with adaptive xCoord.
 *
 * Uses SCX enum initialization from scx/common.h but manual load/attach
 * (no UEI) because the BPF side avoids UEI to work around clang 18's
 * lack of 32-bit atomic support.
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

#include "sched_gpu_baseline.skel.h"
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

static void read_stats(struct sched_gpu_baseline_bpf *skel, __u64 *out)
{
	int nr_cpus = libbpf_num_possible_cpus();
	assert(nr_cpus > 0);

	memset(out, 0, sizeof(out[0]) * 4);

	for (__u32 idx = 0; idx < 4; idx++) {
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
	struct sched_gpu_baseline_bpf *skel;
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
				"xCoord BASELINE: blind priority boost sched_ext scheduler.\n\n"
				"Reads GPU state from %s (written by eviction_lfu_xcoord)\n"
				"and boosts CPU scheduling priority for GPU processes and\n"
				"UVM fault handler threads.\n\n"
				"Usage: %s [-v] [-t threshold] [-p PID ...]\n\n"
				"  -p PID   GPU process PID to boost (can specify multiple)\n"
				"  -t RATE  Fault rate threshold for boost (default: %d)\n"
				"  -v       Print libbpf debug messages\n"
				"  -h       Display this help and exit\n",
				XCOORD_GPU_STATE_PIN, basename(argv[0]),
				XCOORD_FAULT_RATE_HIGH);
			return opt != 'h';
		}
	}

	/* Try to open the pinned gpu_state_map from gpu_ext */
	gpu_map_fd = bpf_obj_get(XCOORD_GPU_STATE_PIN);
	if (gpu_map_fd < 0) {
		fprintf(stderr,
			"WARNING: Cannot open %s: %s\n"
			"  gpu_ext (eviction_lfu_xcoord) may not be running.\n"
			"  Scheduler will load but GPU state will be empty.\n"
			"  Start gpu_ext first for full xCoord functionality.\n\n",
			XCOORD_GPU_STATE_PIN, strerror(errno));
	} else {
		printf("Connected to gpu_ext gpu_state_map at %s\n",
		       XCOORD_GPU_STATE_PIN);
	}

	/* Try to open the pinned uvm_worker_pids map from gpu_ext */
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

	skel = sched_gpu_baseline_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	/*
	 * Initialize sched_ext enum values from kernel BTF.
	 * This is critical — without it, constants like SCX_DSQ_LOCAL,
	 * SCX_SLICE_DFL, SCX_ENQ_HEAD are all 0, breaking the scheduler.
	 */
	skel->struct_ops.gpu_baseline_ops->hotplug_seq = scx_hotplug_seq();
	SCX_ENUM_INIT(skel);

	/* Set configurable threshold */
	skel->rodata->fault_rate_boost_threshold = threshold;

	/*
	 * Replace the gpu_state_map with the pinned map from gpu_ext.
	 * This must be done after open() but before load().
	 */
	if (gpu_map_fd >= 0) {
		err = bpf_map__reuse_fd(skel->maps.gpu_state_map, gpu_map_fd);
		if (err) {
			fprintf(stderr, "Failed to reuse gpu_state_map fd: %s\n",
				strerror(-err));
			fprintf(stderr, "Continuing with empty local map.\n");
		} else {
			printf("Successfully connected gpu_state_map to gpu_ext\n");
		}
	}

	if (workers_map_fd >= 0) {
		err = bpf_map__reuse_fd(skel->maps.uvm_worker_pids, workers_map_fd);
		if (err) {
			fprintf(stderr, "Failed to reuse uvm_worker_pids fd: %s\n",
				strerror(-err));
			fprintf(stderr, "Worker boosting will not work.\n");
		} else {
			printf("Successfully connected uvm_worker_pids to gpu_ext\n");
		}
	}

	err = sched_gpu_baseline_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
		goto cleanup;
	}

	link = bpf_map__attach_struct_ops(skel->maps.gpu_baseline_ops);
	if (!link) {
		err = -errno;
		fprintf(stderr, "Failed to attach struct_ops: %s\n",
			strerror(errno));
		goto cleanup;
	}

	/* Populate gpu_process_pids map with PIDs from -p flags */
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
				printf("Registered GPU process PID %u for boosting\n",
				       gpu_pids[i]);
			}
		}
	}

	printf("xCoord BASELINE scheduler loaded (blind boost)!\n");
	printf("  Fault rate boost threshold: %llu faults/sec\n",
	       (unsigned long long)threshold);
	printf("  GPU process PIDs: %d registered\n", n_gpu_pids);
	printf("  GPU state map: %s\n",
	       gpu_map_fd >= 0 ? "connected" : "not available (running blind)");
	printf("  UVM worker map: %s\n",
	       workers_map_fd >= 0 ? "connected" : "not available");
	printf("\nPress Ctrl-C to exit (will fallback to CFS)...\n\n");

	while (!exit_req && !skel->bss->exit_kind) {
		__u64 stats[4];

		read_stats(skel, stats);
		printf("local=%llu global=%llu gpu_boosted=%llu gpu_throttled=%llu\n",
		       stats[0], stats[1], stats[2], stats[3]);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);

cleanup:
	sched_gpu_baseline_bpf__destroy(skel);

	if (gpu_map_fd >= 0)
		close(gpu_map_fd);
	if (workers_map_fd >= 0)
		close(workers_map_fd);

	return err < 0 ? -err : 0;
}
