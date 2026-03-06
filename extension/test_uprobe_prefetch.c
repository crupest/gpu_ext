#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "test_uprobe_prefetch.skel.h"
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
    struct test_uprobe_prefetch_bpf *skel;
    struct bpf_link *link_struct_ops = NULL;
    struct bpf_link *link_kprobe = NULL;
    struct bpf_link *link_uprobe = NULL;
    int err;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-to-target-binary> [func-name] [--direct]\n", argv[0]);
        fprintf(stderr, "  Attaches uprobe to 'request_prefetch' in the target binary.\n");
        fprintf(stderr, "  --direct: use sleepable uprobe with direct kfunc call (no bpf_wq)\n");
        return 1;
    }

    const char *target_path = argv[1];
    const char *func_name = "request_prefetch";
    int use_direct = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--direct") == 0)
            use_direct = 1;
        else
            func_name = argv[i];
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    libbpf_set_print(libbpf_print_fn);
    cleanup_old_struct_ops();

    skel = test_uprobe_prefetch_bpf__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = test_uprobe_prefetch_bpf__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF skeleton: %d\n", err);
        goto cleanup;
    }

    /* 1. Attach struct_ops (needed for gpu kfunc access) */
    link_struct_ops = bpf_map__attach_struct_ops(skel->maps.uvm_ops_uprobe_prefetch);
    if (!link_struct_ops) {
        err = -errno;
        fprintf(stderr, "Failed to attach struct_ops: %s\n", strerror(-err));
        goto cleanup;
    }
    printf("struct_ops attached (default passthrough)\n");

    /* 2. Attach kprobe for va_space capture */
    link_kprobe = bpf_program__attach(skel->progs.capture_va_space);
    if (!link_kprobe) {
        err = -errno;
        fprintf(stderr, "Failed to attach kprobe: %s\n", strerror(-err));
        goto cleanup;
    }
    printf("kprobe attached (va_space capture)\n");

    /* 3. Attach uprobe on target binary's function */
    LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts,
        .func_name = func_name,
        .retprobe = false,
    );
    struct bpf_program *uprobe_prog = use_direct
        ? skel->progs.uprobe_direct_prefetch
        : skel->progs.uprobe_request_prefetch;
    link_uprobe = bpf_program__attach_uprobe_opts(
        uprobe_prog,
        -1, /* all PIDs */
        target_path,
        0,  /* offset (resolved by func_name) */
        &uprobe_opts
    );
    if (!link_uprobe) {
        err = -errno;
        fprintf(stderr, "Failed to attach uprobe on %s:%s: %s\n",
                target_path, func_name, strerror(-err));
        goto cleanup;
    }
    printf("uprobe attached (%s): %s:%s\n",
           use_direct ? "direct kfunc" : "pending_map relay",
           target_path, func_name);

    printf("\nUprobe prefetch POC running. Now start the target binary.\n");
    printf("Press Ctrl-C to exit...\n\n");

    int stats_fd = bpf_map__fd(skel->maps.stats);

    while (!exiting) {
        sleep(2);
        __u32 k;
        __u64 v;
        printf("Stats: ");
        const char *names[] = {"uprobe_fire", "wq_sched", "migrate_ok", "migrate_fail"};
        for (k = 0; k < 4; k++) {
            v = 0;
            bpf_map_lookup_elem(stats_fd, &k, &v);
            printf("%s=%llu ", names[k], (unsigned long long)v);
        }
        printf("\n");
    }

    printf("\nDetaching...\n");

cleanup:
    if (link_uprobe) bpf_link__destroy(link_uprobe);
    if (link_kprobe) bpf_link__destroy(link_kprobe);
    if (link_struct_ops) bpf_link__destroy(link_struct_ops);
    test_uprobe_prefetch_bpf__destroy(skel);
    return err < 0 ? -err : 0;
}
