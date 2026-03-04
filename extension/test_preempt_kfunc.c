// SPDX-License-Identifier: GPL-2.0
/*
 * test_preempt_kfunc.c - Userspace loader for test_preempt_kfunc BPF program
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "test_preempt_kfunc.skel.h"

static volatile bool exiting = false;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    return vfprintf(stderr, format, args);
}

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct test_preempt_kfunc_bpf *skel;
    int err;

    libbpf_set_print(libbpf_print_fn);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* Open and load BPF program */
    skel = test_preempt_kfunc_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    /* Attach struct_ops */
    err = test_preempt_kfunc_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF: %d\n", err);
        goto cleanup;
    }

    printf("GPU preempt kfunc test loaded. Press Ctrl+C to stop.\n");
    printf("Launch a CUDA program to trigger preempt.\n");
    printf("Monitor: sudo cat /sys/kernel/tracing/trace_pipe\n");
    printf("---\n");

    while (!exiting) {
        sleep(1);

        /* Print stats */
        int stats_fd = bpf_map__fd(skel->maps.stats);
        __u64 val;
        __u32 key;

        key = 0; bpf_map_lookup_elem(stats_fd, &key, &val);
        printf("\rtask_init=%llu", (unsigned long long)val);
        key = 1; bpf_map_lookup_elem(stats_fd, &key, &val);
        printf(" bind=%llu", (unsigned long long)val);
        key = 2; bpf_map_lookup_elem(stats_fd, &key, &val);
        printf(" preempt_ok=%llu", (unsigned long long)val);
        key = 3; bpf_map_lookup_elem(stats_fd, &key, &val);
        printf(" preempt_err=%llu", (unsigned long long)val);
        fflush(stdout);
    }

    printf("\n\nFinal stats:\n");
    {
        int stats_fd = bpf_map__fd(skel->maps.stats);
        __u64 val;
        __u32 key;
        key = 0; bpf_map_lookup_elem(stats_fd, &key, &val);
        printf("  task_init:   %llu\n", (unsigned long long)val);
        key = 1; bpf_map_lookup_elem(stats_fd, &key, &val);
        printf("  bind:        %llu\n", (unsigned long long)val);
        key = 2; bpf_map_lookup_elem(stats_fd, &key, &val);
        printf("  preempt_ok:  %llu\n", (unsigned long long)val);
        key = 3; bpf_map_lookup_elem(stats_fd, &key, &val);
        printf("  preempt_err: %llu\n", (unsigned long long)val);
    }

cleanup:
    test_preempt_kfunc_bpf__destroy(skel);
    return err < 0 ? 1 : 0;
}
