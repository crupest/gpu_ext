/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord Minimal Policy: GPU-Aware sched_ext scheduler loader
 *
 * Minimal overhead variant — fewer hooks, no vtime tracking.
 * Identical CLI interface to sched_gpu_aware.
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

#include "sched_gpu_minimal.skel.h"
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

static void read_stats(struct sched_gpu_minimal_bpf *skel, __u64 *out)
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
	struct sched_gpu_minimal_bpf *skel;
	struct bpf_link *link = NULL;
	int opt;
	int gpu_map_fd = -1;
	int workers_map_fd = -1;
	int err = 0;
	__u32 gpu_pids[MAX_GPU_PIDS];
	int n_gpu_pids = 0;

	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	while ((opt = getopt(argc, argv, "p:vh")) != -1) {
		switch (opt) {
		case 'p':
			if (n_gpu_pids < MAX_GPU_PIDS)
				gpu_pids[n_gpu_pids++] = (__u32)atoi(optarg);
			break;
		case 'v':
			verbose = true;
			break;
		default:
			fprintf(stderr,
				"xCoord Minimal GPU-aware scheduler.\n\n"
				"Usage: %s [-v] [-p PID ...]\n\n"
				"  -p PID   GPU process PID to boost\n"
				"  -v       Print libbpf debug messages\n",
				basename(argv[0]));
			return opt != 'h';
		}
	}

	/* Try pinned maps from gpu_ext (optional) */
	gpu_map_fd = bpf_obj_get(XCOORD_GPU_STATE_PIN);
	workers_map_fd = bpf_obj_get(XCOORD_UVM_WORKERS_PIN);

	skel = sched_gpu_minimal_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	skel->struct_ops.gpu_minimal_ops->hotplug_seq = scx_hotplug_seq();
	SCX_ENUM_INIT(skel);

	/* Reuse pinned maps if available */
	if (gpu_map_fd >= 0)
		bpf_map__reuse_fd(skel->maps.gpu_state_map, gpu_map_fd);
	if (workers_map_fd >= 0)
		bpf_map__reuse_fd(skel->maps.uvm_worker_pids, workers_map_fd);

	err = sched_gpu_minimal_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF: %d\n", err);
		goto cleanup;
	}

	link = bpf_map__attach_struct_ops(skel->maps.gpu_minimal_ops);
	if (!link) {
		err = -errno;
		fprintf(stderr, "Failed to attach struct_ops: %s\n",
			strerror(errno));
		goto cleanup;
	}

	/* Register GPU PIDs */
	if (n_gpu_pids > 0) {
		int proc_fd = bpf_map__fd(skel->maps.gpu_process_pids);
		for (int i = 0; i < n_gpu_pids; i++) {
			__u32 val = 1;
			err = bpf_map_update_elem(proc_fd, &gpu_pids[i],
						  &val, BPF_ANY);
			if (err)
				fprintf(stderr, "Failed to add PID %u\n",
					gpu_pids[i]);
			else
				printf("GPU PID %u registered\n", gpu_pids[i]);
		}
	}

	printf("gpu_minimal loaded (%d PIDs)\n", n_gpu_pids);

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
	sched_gpu_minimal_bpf__destroy(skel);
	if (gpu_map_fd >= 0)
		close(gpu_map_fd);
	if (workers_map_fd >= 0)
		close(workers_map_fd);

	return err < 0 ? -err : 0;
}
