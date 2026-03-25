// SPDX-License-Identifier: GPL-2.0
/*
 * uprobe_preempt_multi.c - Loader for multi-target sleepable uprobe preempt
 *
 * Usage examples:
 *   sudo ./uprobe_preempt_multi --be-name bench_be
 *   sudo ./uprobe_preempt_multi --lc-pid 12345 --target 0x123,0x456
 *   sudo ./uprobe_preempt_multi --cooldown-us 250 --target 0x123,0x456 -v
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "uprobe_preempt_multi.skel.h"

#define MAX_BE_TARGETS 16
#define LATENCY_SAMPLES 256
#define TASK_COMM_LEN 16

enum stat_idx {
	STAT_UPROBE_HIT = 0,
	STAT_PREEMPT_OK = 1,
	STAT_PREEMPT_ERR = 2,
	STAT_LAST_NS = 3,
	STAT_TOTAL_NS = 4,
	STAT_SKIPPED = 5,
	STAT_COOLDOWN_SKIP = 6,
	STAT_TARGETS_HIT = 7,
	STAT_TSG_CAPTURED = 8,
	STAT_TSG_FILTERED = 9,
};

struct tsg_entry {
	__u32 hClient;
	__u32 hTsg;
};

struct config {
	char be_name[TASK_COMM_LEN];
	char lc_name[TASK_COMM_LEN];
	int lc_pid;
	__u64 cooldown_ns;
	bool auto_capture;
	bool verbose;
	struct tsg_entry targets[MAX_BE_TARGETS];
	size_t nr_targets;
};

static volatile bool exiting;
static bool verbose;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
			  va_list args)
{
	if (!verbose && level == LIBBPF_DEBUG)
		return 0;
	return vfprintf(stderr, format, args);
}

static void sig_handler(int sig)
{
	(void)sig;
	exiting = true;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\nOptions:\n"
		"  --be-name NAME      BE process name for auto-capture (default: bench_be)\n"
		"  --lc-name NAME      LC process name for uprobe trigger filter (default: bench_lc)\n"
		"  --lc-pid PID        Attach uprobe only to this PID\n"
		"  --cooldown-us US    Per-CPU cooldown in microseconds (default: 100)\n"
		"  --no-auto           Disable kprobe auto-capture\n"
		"  --target H,T        Add manual target hClient,hTsg (hex or decimal, repeatable)\n"
		"  -v                  Enable libbpf debug output\n"
		"  -h, --help          Show this help\n",
		prog);
}

static int parse_u32_arg(const char *arg, __u32 *out)
{
	char *end = NULL;
	unsigned long value;

	errno = 0;
	value = strtoul(arg, &end, 0);
	if (errno || end == arg || *end != '\0' || value > UINT32_MAX)
		return -1;

	*out = (__u32)value;
	return 0;
}

static int parse_target_arg(const char *arg, struct tsg_entry *target)
{
	char *comma;
	char *end = NULL;
	unsigned long value;

	errno = 0;
	value = strtoul(arg, &end, 0);
	if (errno || end == arg || *end != ',' || value > UINT32_MAX)
		return -1;
	target->hClient = (__u32)value;

	comma = end;
	errno = 0;
	value = strtoul(comma + 1, &end, 0);
	if (errno || end == comma + 1 || *end != '\0' || value > UINT32_MAX)
		return -1;
	target->hTsg = (__u32)value;

	return 0;
}

static __u64 read_stat(struct uprobe_preempt_multi_bpf *skel, __u32 key)
{
	__u64 value = 0;

	bpf_map_lookup_elem(bpf_map__fd(skel->maps.uprobe_stats), &key, &value);
	return value;
}

static __u32 read_target_count(struct uprobe_preempt_multi_bpf *skel)
{
	__u32 zero = 0;
	__u32 count = 0;

	bpf_map_lookup_elem(bpf_map__fd(skel->maps.be_target_count), &zero, &count);
	if (count > MAX_BE_TARGETS)
		count = MAX_BE_TARGETS;
	return count;
}

static int write_manual_targets(struct uprobe_preempt_multi_bpf *skel,
			       const struct config *cfg)
{
	int map_fd = bpf_map__fd(skel->maps.target_tsg);
	int count_fd = bpf_map__fd(skel->maps.be_target_count);
	__u32 zero = 0;
	__u32 count = (__u32)cfg->nr_targets;
	size_t i;

	for (i = 0; i < cfg->nr_targets; i++) {
		__u32 key = (__u32)i;

		if (bpf_map_update_elem(map_fd, &key, &cfg->targets[i], BPF_ANY) != 0) {
			fprintf(stderr, "Failed to write manual target %zu: %s\n",
				i, strerror(errno));
			return -1;
		}
	}

	if (bpf_map_update_elem(count_fd, &zero, &count, BPF_ANY) != 0) {
		fprintf(stderr, "Failed to write target count: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static void print_targets(struct uprobe_preempt_multi_bpf *skel)
{
	int map_fd = bpf_map__fd(skel->maps.target_tsg);
	__u32 count = read_target_count(skel);
	__u32 i;

	printf("Active targets (%u):\n", count);
	for (i = 0; i < count; i++) {
		struct tsg_entry entry = {};

		if (bpf_map_lookup_elem(map_fd, &i, &entry) != 0)
			continue;
		printf("  [%u] hClient=0x%x hTsg=0x%x\n",
		       i, entry.hClient, entry.hTsg);
	}
}

static void print_stats_line(struct uprobe_preempt_multi_bpf *skel)
{
	__u64 hits = read_stat(skel, STAT_UPROBE_HIT);
	__u64 ok = read_stat(skel, STAT_PREEMPT_OK);
	__u64 err = read_stat(skel, STAT_PREEMPT_ERR);
	__u64 last_ns = read_stat(skel, STAT_LAST_NS);
	__u64 total_ns = read_stat(skel, STAT_TOTAL_NS);
	__u64 skipped = read_stat(skel, STAT_SKIPPED);
	__u64 cooldown_skip = read_stat(skel, STAT_COOLDOWN_SKIP);
	__u64 targets_hit = read_stat(skel, STAT_TARGETS_HIT);
	__u64 tsg_captured = read_stat(skel, STAT_TSG_CAPTURED);
	__u64 tsg_filtered = read_stat(skel, STAT_TSG_FILTERED);
	__u64 avg_ns = targets_hit ? total_ns / targets_hit : 0;
	__u32 target_count = read_target_count(skel);

	printf("hits=%llu active_targets=%u targets_hit=%llu ok=%llu err=%llu "
	       "skip=%llu cooldown=%llu captured=%llu filtered=%llu last=%llu us avg=%llu us\n",
	       (unsigned long long)hits,
	       target_count,
	       (unsigned long long)targets_hit,
	       (unsigned long long)ok,
	       (unsigned long long)err,
	       (unsigned long long)skipped,
	       (unsigned long long)cooldown_skip,
	       (unsigned long long)tsg_captured,
	       (unsigned long long)tsg_filtered,
	       (unsigned long long)(last_ns / 1000),
	       (unsigned long long)(avg_ns / 1000));
}

static void print_final_stats(struct uprobe_preempt_multi_bpf *skel)
{
	printf("\n=== Final Stats ===\n");
	printf("  uprobe_hit:     %llu\n",
	       (unsigned long long)read_stat(skel, STAT_UPROBE_HIT));
	printf("  preempt_ok:     %llu\n",
	       (unsigned long long)read_stat(skel, STAT_PREEMPT_OK));
	printf("  preempt_err:    %llu\n",
	       (unsigned long long)read_stat(skel, STAT_PREEMPT_ERR));
	printf("  last_ns:        %llu\n",
	       (unsigned long long)read_stat(skel, STAT_LAST_NS));
	printf("  total_ns:       %llu\n",
	       (unsigned long long)read_stat(skel, STAT_TOTAL_NS));
	printf("  skipped:        %llu\n",
	       (unsigned long long)read_stat(skel, STAT_SKIPPED));
	printf("  cooldown_skip:  %llu\n",
	       (unsigned long long)read_stat(skel, STAT_COOLDOWN_SKIP));
	printf("  targets_hit:    %llu\n",
	       (unsigned long long)read_stat(skel, STAT_TARGETS_HIT));
	printf("  tsg_captured:   %llu\n",
	       (unsigned long long)read_stat(skel, STAT_TSG_CAPTURED));
	printf("  tsg_filtered:   %llu\n",
	       (unsigned long long)read_stat(skel, STAT_TSG_FILTERED));
	printf("  active_targets: %u\n", read_target_count(skel));
}

static void print_latency_summary(struct uprobe_preempt_multi_bpf *skel)
{
	int map_fd = bpf_map__fd(skel->maps.uprobe_latency);
	__u64 total_ns = read_stat(skel, STAT_TOTAL_NS);
	__u64 targets_hit = read_stat(skel, STAT_TARGETS_HIT);
	__u64 min_ns = 0;
	__u64 max_ns = 0;
	__u32 sample_count = 0;
	__u32 i;

	for (i = 0; i < LATENCY_SAMPLES; i++) {
		__u64 sample = 0;

		if (bpf_map_lookup_elem(map_fd, &i, &sample) != 0 || sample == 0)
			continue;

		if (sample_count == 0 || sample < min_ns)
			min_ns = sample;
		if (sample > max_ns)
			max_ns = sample;
		sample_count++;
	}

	printf("\n=== Latency Summary ===\n");
	if (targets_hit == 0 || sample_count == 0) {
		printf("  no latency samples recorded\n");
		return;
	}

	printf("  samples: %u\n", sample_count);
	printf("  min:     %llu us\n", (unsigned long long)(min_ns / 1000));
	printf("  avg:     %llu us\n",
	       (unsigned long long)((total_ns / targets_hit) / 1000));
	printf("  max:     %llu us\n", (unsigned long long)(max_ns / 1000));
}

static int attach_uprobe(struct uprobe_preempt_multi_bpf *skel, int lc_pid)
{
	if (lc_pid > 0) {
		LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts,
			.func_name = "cuLaunchKernel",
		);

		skel->links.preempt_on_launch =
			bpf_program__attach_uprobe_opts(skel->progs.preempt_on_launch,
							lc_pid,
							"/usr/lib/x86_64-linux-gnu/libcuda.so",
							0,
							&uprobe_opts);
	} else {
		skel->links.preempt_on_launch =
			bpf_program__attach(skel->progs.preempt_on_launch);
	}

	if (!skel->links.preempt_on_launch) {
		fprintf(stderr, "Failed to attach uprobe: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int attach_auto_capture(struct uprobe_preempt_multi_bpf *skel)
{
	skel->links.capture_ioctl_entry =
		bpf_program__attach(skel->progs.capture_ioctl_entry);
	if (!skel->links.capture_ioctl_entry) {
		fprintf(stderr, "Failed to attach kprobe nvidia_unlocked_ioctl: %s\n",
			strerror(errno));
		return -1;
	}

	skel->links.capture_engine_type =
		bpf_program__attach(skel->progs.capture_engine_type);
	if (!skel->links.capture_engine_type) {
		fprintf(stderr, "Failed to attach kprobe nv_gpu_sched_task_init: %s\n",
			strerror(errno));
		return -1;
	}

	skel->links.capture_ioctl_exit =
		bpf_program__attach(skel->progs.capture_ioctl_exit);
	if (!skel->links.capture_ioctl_exit) {
		fprintf(stderr, "Failed to attach kretprobe nvidia_unlocked_ioctl: %s\n",
			strerror(errno));
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "be-name", required_argument, NULL, 1 },
		{ "lc-name", required_argument, NULL, 2 },
		{ "lc-pid", required_argument, NULL, 3 },
		{ "cooldown-us", required_argument, NULL, 4 },
		{ "no-auto", no_argument, NULL, 5 },
		{ "target", required_argument, NULL, 6 },
		{ "help", no_argument, NULL, 'h' },
		{ 0, 0, 0, 0 },
	};
	struct uprobe_preempt_multi_bpf *skel = NULL;
	struct config cfg = {
		.be_name = "bench_be",
		.lc_name = "bench_lc",
		.lc_pid = -1,
		.cooldown_ns = 100000ULL,
		.auto_capture = true,
	};
	int err = 0;
	int opt;

	while ((opt = getopt_long(argc, argv, "hv", long_options, NULL)) != -1) {
		switch (opt) {
		case 1:
			if (strlen(optarg) >= TASK_COMM_LEN) {
				fprintf(stderr, "BE name too long (max %d chars)\n",
					TASK_COMM_LEN - 1);
				return 1;
			}
			memset(cfg.be_name, 0, sizeof(cfg.be_name));
			strncpy(cfg.be_name, optarg, sizeof(cfg.be_name) - 1);
			break;
		case 2: {
			if (strlen(optarg) >= TASK_COMM_LEN) {
				fprintf(stderr, "LC name too long (max %d chars)\n",
					TASK_COMM_LEN - 1);
				return 1;
			}
			memset(cfg.lc_name, 0, sizeof(cfg.lc_name));
			strncpy(cfg.lc_name, optarg, sizeof(cfg.lc_name) - 1);
			break;
		}
		case 3: {
			__u32 pid = 0;

			if (parse_u32_arg(optarg, &pid) != 0 || pid == 0) {
				fprintf(stderr, "Invalid PID: %s\n", optarg);
				return 1;
			}
			cfg.lc_pid = (int)pid;
			break;
		}
		case 4: {
			char *end = NULL;
			unsigned long long us;

			errno = 0;
			us = strtoull(optarg, &end, 0);
			if (errno || end == optarg || *end != '\0') {
				fprintf(stderr, "Invalid cooldown: %s\n", optarg);
				return 1;
			}
			cfg.cooldown_ns = us * 1000ULL;
			break;
		}
		case 5:
			cfg.auto_capture = false;
			break;
		case 6:
			if (cfg.nr_targets >= MAX_BE_TARGETS) {
				fprintf(stderr, "Too many manual targets (max %d)\n",
					MAX_BE_TARGETS);
				return 1;
			}
			if (parse_target_arg(optarg, &cfg.targets[cfg.nr_targets]) != 0) {
				fprintf(stderr, "Invalid target format: %s\n", optarg);
				return 1;
			}
			cfg.nr_targets++;
			break;
		case 'v':
			cfg.verbose = true;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	verbose = cfg.verbose;
	libbpf_set_print(libbpf_print_fn);
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	skel = uprobe_preempt_multi_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	skel->rodata->enabled = 1;
	skel->rodata->cooldown_ns = cfg.cooldown_ns;
	memset((char *)skel->rodata->be_comm, 0, sizeof(skel->rodata->be_comm));
	strncpy((char *)skel->rodata->be_comm, cfg.be_name,
		sizeof(skel->rodata->be_comm) - 1);
	memset((char *)skel->rodata->lc_comm, 0, sizeof(skel->rodata->lc_comm));
	strncpy((char *)skel->rodata->lc_comm, cfg.lc_name,
		sizeof(skel->rodata->lc_comm) - 1);

	err = uprobe_preempt_multi_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF: %d\n", err);
		goto cleanup;
	}

	if (write_manual_targets(skel, &cfg) != 0) {
		err = -1;
		goto cleanup;
	}

	err = attach_uprobe(skel, cfg.lc_pid);
	if (err)
		goto cleanup;

	if (cfg.auto_capture) {
		err = attach_auto_capture(skel);
		if (err)
			goto cleanup;
	}

	printf("=== Multi-Target Uprobe Preempt Active ===\n");
	printf("  BE auto-capture name: %s\n", cfg.be_name);
	printf("  LC comm filter:       %s\n", cfg.lc_name[0] ? cfg.lc_name : "<disabled>");
	printf("  Auto-capture:         %s\n", cfg.auto_capture ? "enabled" : "disabled");
	printf("  Cooldown:             %llu us\n",
	       (unsigned long long)(cfg.cooldown_ns / 1000));
	printf("  Uprobe filter:        %s\n",
	       cfg.lc_pid > 0 ? "single PID" : "all processes");
	if (cfg.lc_pid > 0)
		printf("  LC PID:               %d\n", cfg.lc_pid);
	printf("  Manual targets:       %zu\n", cfg.nr_targets);
	if (cfg.nr_targets > 0 || cfg.verbose)
		print_targets(skel);
	printf("  Ctrl-C to stop.\n\n");

	while (!exiting) {
		sleep(1);
		print_stats_line(skel);
	}

	print_final_stats(skel);
	print_latency_summary(skel);
	if (cfg.verbose)
		print_targets(skel);

cleanup:
	uprobe_preempt_multi_bpf__destroy(skel);
	return err < 0 ? 1 : 0;
}
