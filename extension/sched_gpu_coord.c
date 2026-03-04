/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FPRS: Fault-Pressure Regulated Scheduling — userspace loader
 *
 * Loads the FPRS BPF scheduler, connects to gpu_ext's shared maps,
 * populates LC PID array, and reports controller state.
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

#include "sched_gpu_coord.skel.h"
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

static void read_stats(struct sched_gpu_coord_bpf *skel, __u64 *out)
{
	int nr_cpus = libbpf_num_possible_cpus();
	assert(nr_cpus > 0);

	memset(out, 0, sizeof(out[0]) * 8);

	for (__u32 idx = 0; idx < 8; idx++) {
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
	struct sched_gpu_coord_bpf *skel;
	struct bpf_link *link = NULL;
	int opt;
	int gpu_map_fd = -1;
	int workers_map_fd = -1;
	int err = 0;
	__u32 gpu_pids[MAX_GPU_PIDS];
	int n_gpu_pids = 0;
	__u64 target_fr = 0; /* 0 = use default */

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
			target_fr = (__u64)atoll(optarg);
			break;
		case 'v':
			verbose = true;
			break;
		default:
			fprintf(stderr,
				"FPRS: Fault-Pressure Regulated Scheduling\n\n"
				"Feedback-control CPU scheduler driven by GPU fault pressure.\n"
				"Uses LC fault_rate as QoS signal to regulate BE CPU allocation.\n\n"
				"Usage: %s [-v] [-t target] [-p PID ...]\n\n"
				"  -p PID     LC GPU process PID (always boosted).\n"
				"             When set, enables feedback control:\n"
				"             BE GPU processes regulated based on LC fault pressure.\n"
				"  -t RATE    Target LC fault rate (default: 100 faults/sec).\n"
				"             Lower = stricter LC QoS, more BE throttling.\n"
				"             Higher = relaxed LC QoS, more BE throughput.\n"
				"  -v         Print libbpf debug messages\n"
				"  -h         Display this help and exit\n",
				basename(argv[0]));
			return opt != 'h';
		}
	}

	/* Open pinned maps from gpu_ext */
	gpu_map_fd = bpf_obj_get(XCOORD_GPU_STATE_PIN);
	if (gpu_map_fd < 0) {
		fprintf(stderr,
			"WARNING: Cannot open %s: %s\n"
			"  gpu_ext may not be running.\n"
			"  Feedback control requires gpu_ext for fault_rate data.\n\n",
			XCOORD_GPU_STATE_PIN, strerror(errno));
	} else {
		printf("Connected to gpu_state_map\n");
	}

	workers_map_fd = bpf_obj_get(XCOORD_UVM_WORKERS_PIN);
	if (workers_map_fd < 0) {
		fprintf(stderr,
			"WARNING: Cannot open %s: %s\n"
			"  UVM worker thread boosting will not work.\n\n",
			XCOORD_UVM_WORKERS_PIN, strerror(errno));
	} else {
		printf("Connected to uvm_worker_pids\n");
	}

	skel = sched_gpu_coord_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	skel->struct_ops.gpu_coord_ops->hotplug_seq = scx_hotplug_seq();
	SCX_ENUM_INIT(skel);

	/* Set rodata parameters before load */
	skel->rodata->n_lc_pids = (__u32)n_gpu_pids;
	if (target_fr > 0)
		skel->rodata->target_lc_fault_rate = target_fr;

	/* Reuse pinned maps from gpu_ext */
	if (gpu_map_fd >= 0) {
		err = bpf_map__reuse_fd(skel->maps.gpu_state_map, gpu_map_fd);
		if (err)
			fprintf(stderr, "Failed to reuse gpu_state_map: %s\n",
				strerror(-err));
		else
			printf("Reused gpu_state_map\n");
	}

	if (workers_map_fd >= 0) {
		err = bpf_map__reuse_fd(skel->maps.uvm_worker_pids,
					workers_map_fd);
		if (err)
			fprintf(stderr, "Failed to reuse uvm_worker_pids: %s\n",
				strerror(-err));
		else
			printf("Reused uvm_worker_pids\n");
	}

	err = sched_gpu_coord_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
		goto cleanup;
	}

	link = bpf_map__attach_struct_ops(skel->maps.gpu_coord_ops);
	if (!link) {
		err = -errno;
		fprintf(stderr, "Failed to attach struct_ops: %s\n",
			strerror(errno));
		goto cleanup;
	}

	/* Populate gpu_process_pids (hash) and lc_pid_array (array) */
	if (n_gpu_pids > 0) {
		int proc_fd = bpf_map__fd(skel->maps.gpu_process_pids);
		int arr_fd = bpf_map__fd(skel->maps.lc_pid_array);

		for (int i = 0; i < n_gpu_pids; i++) {
			__u32 val = 1;
			__u32 idx = (__u32)i;

			err = bpf_map_update_elem(proc_fd, &gpu_pids[i],
						  &val, BPF_ANY);
			if (err)
				fprintf(stderr,
					"WARNING: Failed to add PID %u to hash: %s\n",
					gpu_pids[i], strerror(-err));

			err = bpf_map_update_elem(arr_fd, &idx,
						  &gpu_pids[i], BPF_ANY);
			if (err)
				fprintf(stderr,
					"WARNING: Failed to add PID %u to array: %s\n",
					gpu_pids[i], strerror(-err));
			else
				printf("Registered LC PID %u\n", gpu_pids[i]);
		}
	}

	printf("\nFPRS scheduler loaded!\n");
	printf("  Target LC fault rate: %llu faults/sec\n",
	       (unsigned long long)skel->rodata->target_lc_fault_rate);
	printf("  Regulation interval: %llums\n",
	       (unsigned long long)skel->rodata->regulate_interval_ns / 1000000);
	printf("  LC PIDs: %d registered\n", n_gpu_pids);
	printf("  Integral gain: %llu, decay: >>%u, max: %llu\n",
	       (unsigned long long)skel->rodata->ki_gain,
	       skel->rodata->decay_shift,
	       (unsigned long long)skel->rodata->max_integral);
	printf("  GPU state map: %s\n",
	       gpu_map_fd >= 0 ? "connected" : "not available");
	printf("  UVM worker map: %s\n",
	       workers_map_fd >= 0 ? "connected" : "not available");
	printf("\nPress Ctrl-C to exit...\n\n");

	/*
	 * Stats indices (must match enum stat_idx in BPF):
	 * 0=local, 1=global, 2=lc_boosted, 3=uvm_worker,
	 * 4=be_regulated, 5=backpressure, 6=regulate_calls, 7=be_demoted
	 */
	while (!exit_req && !skel->bss->exit_kind) {
		__u64 stats[8];

		read_stats(skel, stats);
		printf("local=%llu lc=%llu uvm=%llu be_reg=%llu "
		       "be_demoted=%llu backpres=%llu | "
		       "throttle=%u%% integral=%llu lc_fr=%llu regulate=%llu\n",
		       (unsigned long long)stats[0], /* LOCAL */
		       (unsigned long long)stats[2], /* LC_BOOSTED */
		       (unsigned long long)stats[3], /* UVM_WORKER */
		       (unsigned long long)stats[4], /* BE_REGULATED */
		       (unsigned long long)stats[7], /* BE_DEMOTED */
		       (unsigned long long)stats[5], /* BACKPRESSURE */
		       (unsigned int)(skel->bss->be_throttle_pct / 10),
		       (unsigned long long)skel->bss->pressure_integral,
		       (unsigned long long)skel->bss->lc_fault_rate_observed,
		       (unsigned long long)stats[6]  /* REGULATE_CALLS */);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);

cleanup:
	sched_gpu_coord_bpf__destroy(skel);

	if (gpu_map_fd >= 0)
		close(gpu_map_fd);
	if (workers_map_fd >= 0)
		close(workers_map_fd);

	return err < 0 ? -err : 0;
}
