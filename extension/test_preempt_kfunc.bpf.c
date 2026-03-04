// SPDX-License-Identifier: GPL-2.0
/*
 * test_preempt_kfunc.bpf.c - Test BPF program for GPU TSG preempt kfunc
 *
 * This struct_ops program:
 * 1. Captures hClient/hTsg at TSG init time
 * 2. Uses bpf_wq to defer a sleepable preempt kfunc call
 * 3. On TSG bind (when GPU work starts), triggers preempt + timeslice set
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

/* kfunc declarations */
extern int bpf_nv_gpu_preempt_tsg(u32 hClient, u32 hTsg) __ksym;
extern int bpf_nv_gpu_set_timeslice_runtime(u32 hClient, u32 hTsg,
                                             u64 timeslice_us) __ksym;

/* TSG info for deferred preempt */
struct tsg_entry {
    u32 hClient;
    u32 hTsg;
    u64 tsg_id;
    u32 engine_type;
    u32 preempt_count;
};

/* Map: tsg_id -> tsg_entry */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 64);
    __type(key, u64);
    __type(value, struct tsg_entry);
} tsg_map SEC(".maps");

/* Stats counters */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, u32);
    __type(value, u64);
} stats SEC(".maps");

#define STAT_TASK_INIT   0
#define STAT_BIND        1
#define STAT_PREEMPT_OK  2
#define STAT_PREEMPT_ERR 3

/* Work queue for deferred preempt (sleepable) */
struct preempt_work_ctx {
    struct bpf_wq wq;
    u32 hClient;
    u32 hTsg;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, u32);
    __type(value, struct preempt_work_ctx);
} preempt_wq_map SEC(".maps");

static void inc_stat(u32 idx)
{
    u64 *val = bpf_map_lookup_elem(&stats, &idx);
    if (val)
        __sync_fetch_and_add(val, 1);
}

/* bpf_wq callback: runs in sleepable context */
static int preempt_wq_callback(void *map, int *key, struct preempt_work_ctx *wctx)
{
    int ret;

    /* Call the sleepable preempt kfunc */
    ret = bpf_nv_gpu_preempt_tsg(wctx->hClient, wctx->hTsg);
    if (ret == 0) {
        inc_stat(STAT_PREEMPT_OK);
        bpf_printk("preempt OK: hClient=0x%x hTsg=0x%x\n",
                    wctx->hClient, wctx->hTsg);
    } else {
        inc_stat(STAT_PREEMPT_ERR);
        bpf_printk("preempt FAIL: hClient=0x%x hTsg=0x%x ret=%d\n",
                    wctx->hClient, wctx->hTsg, ret);
    }

    /* Also test set_timeslice_runtime: set to 1us (GPreempt-style) */
    ret = bpf_nv_gpu_set_timeslice_runtime(wctx->hClient, wctx->hTsg, 1);
    if (ret == 0) {
        bpf_printk("set_timeslice OK: -> 1us\n");
    } else {
        bpf_printk("set_timeslice FAIL: ret=%d\n", ret);
    }

    return 0;
}

/* struct_ops: on_task_init - capture handles */
SEC("struct_ops/on_task_init")
int BPF_PROG(on_task_init, struct nv_gpu_task_init_ctx *init_ctx)
{
    struct tsg_entry entry = {};

    if (!init_ctx)
        return 0;

    inc_stat(STAT_TASK_INIT);

    entry.hClient = init_ctx->hClient;
    entry.hTsg = init_ctx->hTsg;
    entry.tsg_id = init_ctx->tsg_id;
    entry.engine_type = init_ctx->engine_type;
    entry.preempt_count = 0;

    bpf_map_update_elem(&tsg_map, &init_ctx->tsg_id, &entry, BPF_ANY);

    bpf_printk("task_init: tsg=%llu engine=%u hClient=0x%x hTsg=0x%x\n",
                init_ctx->tsg_id, init_ctx->engine_type,
                init_ctx->hClient, init_ctx->hTsg);

    return 0;
}

/* struct_ops: on_bind - trigger preempt via bpf_wq */
SEC("struct_ops/on_bind")
int BPF_PROG(on_bind, struct nv_gpu_bind_ctx *bind_ctx)
{
    struct tsg_entry *entry;
    u32 key = 0;
    struct preempt_work_ctx *wctx;

    if (!bind_ctx)
        return 0;

    inc_stat(STAT_BIND);

    /* Look up TSG handles */
    entry = bpf_map_lookup_elem(&tsg_map, &bind_ctx->tsg_id);
    if (!entry || entry->hClient == 0)
        return 0;

    /* Only preempt GRAPHICS engine (type 1) TSGs */
    if (entry->engine_type != 1)
        return 0;

    /* Schedule deferred preempt via bpf_wq */
    wctx = bpf_map_lookup_elem(&preempt_wq_map, &key);
    if (!wctx)
        return 0;

    wctx->hClient = entry->hClient;
    wctx->hTsg = entry->hTsg;

    if (bpf_wq_set_callback_impl(&wctx->wq, preempt_wq_callback, 0, NULL) == 0) {
        bpf_wq_start(&wctx->wq, 0);
        bpf_printk("bind: scheduled preempt for tsg=%llu\n", bind_ctx->tsg_id);
    }

    entry->preempt_count++;

    return 0;
}

/* struct_ops: on_task_destroy - cleanup */
SEC("struct_ops/on_task_destroy")
int BPF_PROG(on_task_destroy, struct nv_gpu_task_destroy_ctx *destroy_ctx)
{
    if (!destroy_ctx)
        return 0;

    bpf_map_delete_elem(&tsg_map, &destroy_ctx->tsg_id);
    bpf_printk("task_destroy: tsg=%llu\n", destroy_ctx->tsg_id);

    return 0;
}

SEC(".struct_ops.link")
struct nv_gpu_sched_ops test_preempt_ops = {
    .on_task_init    = (void *)on_task_init,
    .on_bind         = (void *)on_bind,
    .on_task_destroy = (void *)on_task_destroy,
};

char _license[] SEC("license") = "GPL";
