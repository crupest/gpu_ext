/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xCoord GPU-side userspace loader
 *
 * Loads eviction_lfu_xcoord.bpf.c and pins the gpu_state_map to
 * /sys/fs/bpf/xcoord_gpu_state so sched_ext can read it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "eviction_lfu_xcoord.skel.h"
#include "cleanup_struct_ops.h"
#include "eviction_common.h"
#include "shared_maps.h"

static __u64 g_priority_pid = 0;
static __u64 g_low_priority_pid = 0;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
	return vfprintf(stderr, format, args);
}

static volatile bool exiting = false;

void handle_signal(int sig) {
	exiting = true;
}

static void print_gpu_state(struct eviction_lfu_xcoord_bpf *skel)
{
	int gpu_state_fd = bpf_map__fd(skel->maps.gpu_state_map);
	struct gpu_pid_state gs;
	__u32 pid = 0, next_pid;

	printf("\n=== xCoord GPU State (shared with sched_ext) ===\n");
	printf("  %-8s %12s %12s %12s %12s %10s\n",
	       "PID", "fault_rate", "fault_cnt", "evict_cnt", "used_cnt", "thrashing");

	while (bpf_map_get_next_key(gpu_state_fd, &pid, &next_pid) == 0) {
		if (bpf_map_lookup_elem(gpu_state_fd, &next_pid, &gs) == 0) {
			printf("  %-8u %12llu %12llu %12llu %12llu %10s\n",
			       next_pid,
			       (unsigned long long)gs.fault_rate,
			       (unsigned long long)gs.fault_count,
			       (unsigned long long)gs.eviction_count,
			       (unsigned long long)gs.used_count,
			       gs.is_thrashing ? "YES" : "no");
		}
		pid = next_pid;
	}
}

static void print_pid_stats(struct eviction_lfu_xcoord_bpf *skel)
{
	int pid_stats_fd = bpf_map__fd(skel->maps.pid_chunk_count);
	struct pid_chunk_stats ps;
	__u32 pid;

	printf("\n=== Per-PID Chunk Statistics ===\n");

	if (g_priority_pid > 0) {
		pid = (__u32)g_priority_pid;
		if (bpf_map_lookup_elem(pid_stats_fd, &pid, &ps) == 0) {
			printf("  HIGH PID %u: chunks=%llu activate=%llu used=%llu allow=%llu deny=%llu\n",
			       pid, (unsigned long long)ps.current_count,
			       (unsigned long long)ps.total_activate,
			       (unsigned long long)ps.total_used,
			       (unsigned long long)ps.policy_allow,
			       (unsigned long long)ps.policy_deny);
		}
	}

	if (g_low_priority_pid > 0) {
		pid = (__u32)g_low_priority_pid;
		if (bpf_map_lookup_elem(pid_stats_fd, &pid, &ps) == 0) {
			printf("  LOW  PID %u: chunks=%llu activate=%llu used=%llu allow=%llu deny=%llu\n",
			       pid, (unsigned long long)ps.current_count,
			       (unsigned long long)ps.total_activate,
			       (unsigned long long)ps.total_used,
			       (unsigned long long)ps.policy_allow,
			       (unsigned long long)ps.policy_deny);
		}
	}
}

static void usage(const char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("xCoord GPU-side: LFU eviction + shared GPU state map\n\n");
	printf("Options:\n");
	printf("  -p PID     Set high priority PID\n");
	printf("  -P N       Set high priority decay (default 1)\n");
	printf("  -l PID     Set low priority PID\n");
	printf("  -L N       Set low priority decay (default 10)\n");
	printf("  -d N       Set default decay (default 5)\n");
	printf("  -h         Show this help\n");
	printf("\nShared map pinned at: %s\n", XCOORD_GPU_STATE_PIN);
	printf("sched_ext can read fault_rate/eviction_count from this map.\n");
}

int main(int argc, char **argv)
{
	struct eviction_lfu_xcoord_bpf *skel;
	struct bpf_link *link;
	int err;
	__u64 priority_pid = 0;
	__u64 priority_param = 1;
	__u64 low_priority_pid = 0;
	__u64 low_priority_param = 10;
	__u64 default_param = 5;
	int opt;

	while ((opt = getopt(argc, argv, "p:P:l:L:d:h")) != -1) {
		switch (opt) {
		case 'p':
			priority_pid = atoi(optarg);
			g_priority_pid = priority_pid;
			break;
		case 'P':
			priority_param = atoll(optarg);
			if (priority_param == 0) priority_param = 1;
			break;
		case 'l':
			low_priority_pid = atoi(optarg);
			g_low_priority_pid = low_priority_pid;
			break;
		case 'L':
			low_priority_param = atoll(optarg);
			if (low_priority_param == 0) low_priority_param = 1;
			break;
		case 'd':
			default_param = atoll(optarg);
			if (default_param == 0) default_param = 1;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	libbpf_set_print(libbpf_print_fn);

	cleanup_old_struct_ops();

	skel = eviction_lfu_xcoord_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	err = eviction_lfu_xcoord_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
		goto cleanup;
	}

	/* Set configuration */
	int config_fd = bpf_map__fd(skel->maps.policy_config);
	__u32 key;

	key = CONFIG_PRIORITY_PID;
	bpf_map_update_elem(config_fd, &key, &priority_pid, BPF_ANY);
	key = CONFIG_PRIORITY_PARAM;
	bpf_map_update_elem(config_fd, &key, &priority_param, BPF_ANY);
	key = CONFIG_LOW_PRIORITY_PID;
	bpf_map_update_elem(config_fd, &key, &low_priority_pid, BPF_ANY);
	key = CONFIG_LOW_PRIORITY_PARAM;
	bpf_map_update_elem(config_fd, &key, &low_priority_param, BPF_ANY);
	key = CONFIG_DEFAULT_PARAM;
	bpf_map_update_elem(config_fd, &key, &default_param, BPF_ANY);

	/* Pin gpu_state_map for sched_ext to read */
	int gpu_state_fd = bpf_map__fd(skel->maps.gpu_state_map);

	/* Remove stale pin if exists */
	unlink(XCOORD_GPU_STATE_PIN);

	err = bpf_obj_pin(gpu_state_fd, XCOORD_GPU_STATE_PIN);
	if (err) {
		fprintf(stderr, "Failed to pin gpu_state_map at %s: %s\n",
			XCOORD_GPU_STATE_PIN, strerror(errno));
		fprintf(stderr, "Make sure /sys/fs/bpf/ is mounted (bpffs)\n");
		goto cleanup;
	}
	printf("Pinned gpu_state_map at %s\n", XCOORD_GPU_STATE_PIN);

	/* Pin uvm_worker_pids map for sched_ext to read */
	int workers_fd = bpf_map__fd(skel->maps.uvm_worker_pids);
	unlink(XCOORD_UVM_WORKERS_PIN);
	err = bpf_obj_pin(workers_fd, XCOORD_UVM_WORKERS_PIN);
	if (err) {
		fprintf(stderr, "Failed to pin uvm_worker_pids at %s: %s\n",
			XCOORD_UVM_WORKERS_PIN, strerror(errno));
	} else {
		printf("Pinned uvm_worker_pids at %s\n", XCOORD_UVM_WORKERS_PIN);
	}

	/* Attach struct_ops */
	link = bpf_map__attach_struct_ops(skel->maps.uvm_ops_lfu_xcoord);
	if (!link) {
		err = -errno;
		fprintf(stderr, "Failed to attach struct_ops: %s (%d)\n",
			strerror(-err), err);
		unlink(XCOORD_GPU_STATE_PIN);
		unlink(XCOORD_UVM_WORKERS_PIN);
		goto cleanup;
	}

	printf("xCoord GPU-side loaded!\n");
	printf("  Eviction policy: LFU with frequency decay\n");
	printf("  Shared maps: %s, %s\n", XCOORD_GPU_STATE_PIN,
	       XCOORD_UVM_WORKERS_PIN);
	printf("  High priority PID: %llu (decay: %llu)\n",
	       (unsigned long long)priority_pid,
	       (unsigned long long)priority_param);
	printf("  Low priority PID:  %llu (decay: %llu)\n",
	       (unsigned long long)low_priority_pid,
	       (unsigned long long)low_priority_param);
	printf("  Default decay:     %llu\n", (unsigned long long)default_param);
	printf("\nPress Ctrl-C to exit...\n");

	while (!exiting) {
		sleep(5);
		print_gpu_state(skel);
		print_pid_stats(skel);
	}

	printf("\nDetaching...\n");
	print_gpu_state(skel);
	bpf_link__destroy(link);

	/* Unpin shared maps on exit */
	unlink(XCOORD_GPU_STATE_PIN);
	unlink(XCOORD_UVM_WORKERS_PIN);
	printf("Unpinned shared maps\n");

cleanup:
	eviction_lfu_xcoord_bpf__destroy(skel);
	return err < 0 ? -err : 0;
}
