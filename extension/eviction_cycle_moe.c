/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace loader for Cycle-Aware MoE Eviction Policy (Lightweight)
 *
 * Loads and attaches the cycle_moe BPF eviction policy.
 * Uses direct-mapped per-CPU array for O(1) access counting.
 *
 * Usage:
 *   sudo ./eviction_cycle_moe
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "eviction_cycle_moe.skel.h"
#include "cleanup_struct_ops.h"

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args)
{
    return vfprintf(stderr, format, args);
}

static volatile bool exiting = false;

void handle_signal(int sig) {
    exiting = true;
}

int main(int argc, char **argv) {
    struct eviction_cycle_moe_bpf *skel;
    struct bpf_link *link;
    int err;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    libbpf_set_print(libbpf_print_fn);
    cleanup_old_struct_ops();

    /* Open BPF application */
    skel = eviction_cycle_moe_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    /* Load BPF programs */
    err = eviction_cycle_moe_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
        goto cleanup;
    }

    /* Register struct_ops */
    link = bpf_map__attach_struct_ops(skel->maps.uvm_ops_cycle_moe);
    if (!link) {
        err = -errno;
        fprintf(stderr, "Failed to attach struct_ops: %s (%d)\n", strerror(-err), err);
        goto cleanup;
    }

    printf("Cycle-Aware MoE Eviction Policy (Lightweight) loaded!\n");
    printf("  Counter slots: 16384 (per-CPU array)\n");
    printf("  T1 freq threshold: 3\n");
    printf("  Strategy: T1 (freq>=3) → TAIL (protected)\n");
    printf("            Expert/new   → HEAD (MRU, evict first)\n");
    printf("\nPress Ctrl-C to exit...\n\n");

    while (!exiting) {
        sleep(5);
    }

    printf("\nDetaching struct_ops...\n");
    bpf_link__destroy(link);

cleanup:
    eviction_cycle_moe_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}
