/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cross-VA-Block Prefetch v2: BPF Workqueue + Always-Max + Passive MRU
 *
 * Uses kprobe on uvm_perf_prefetch_get_hint_va_block to capture va_block
 * and va_space into a per-CPU map. The struct_ops hook reads this context
 * and schedules cross-block prefetch via bpf_wq.
 *
 * No kernel module changes needed for context — only bpf_gpu_migrate_range()
 * kfunc is required (action kfunc that calls internal uvm_migrate).
 *
 * Pattern reference: extension/prefetch_trace.bpf.c
 */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "uvm_types.h"
#include "bpf_testmod.h"

char _license[] SEC("license") = "GPL";

#define T1_FREQ_THRESHOLD 3
#define COUNTER_SLOTS 16384
#define COUNTER_MASK (COUNTER_SLOTS - 1)

/* VA block size = 2MB */
#define VA_BLOCK_SIZE (2ULL * 1024 * 1024)

/* ===== Per-CPU context from kprobe ===== */

struct va_block_ctx {
    u64 va_start;
    u64 va_end;
    u64 va_space;   /* opaque handle for bpf_gpu_migrate_range() */
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, u32);
    __type(value, struct va_block_ctx);
} va_block_cache SEC(".maps");

/* ===== Maps ===== */

/* Rate-limit: track last prefetched block VA */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, u32);
    __type(value, u64);
} last_prefetch_block SEC(".maps");

/* Per-CPU access frequency counters for eviction */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, COUNTER_SLOTS);
    __type(key, u32);
    __type(value, u8);
} access_counts SEC(".maps");

/* Cross-block prefetch request data + embedded bpf_wq */
struct prefetch_data {
    u64 va_space;
    u64 addr;
    u64 length;
    struct bpf_wq work;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, int);
    __type(value, struct prefetch_data);
} wq_map SEC(".maps");

/* ===== Helpers ===== */

static __always_inline u32 chunk_hash(uvm_gpu_chunk_t *chunk)
{
    u64 ptr = 0;
    bpf_probe_read_kernel(&ptr, sizeof(ptr), &chunk);
    return (u32)((ptr >> 6) ^ (ptr >> 18)) & COUNTER_MASK;
}

/* ===== kprobe: capture va_block context ===== */

/*
 * uvm_perf_prefetch_get_hint_va_block is called BEFORE the struct_ops hook.
 * We capture va_block info (start, end, va_space) into per-CPU map.
 * The struct_ops hook reads this on the same CPU (preemption disabled by rcu).
 *
 * See prefetch_trace.bpf.c for the established pattern.
 */
SEC("kprobe/uvm_perf_prefetch_get_hint_va_block")
int BPF_KPROBE(capture_va_block,
               uvm_va_block_t *va_block)
{
    u32 key = 0;
    struct va_block_ctx *info = bpf_map_lookup_elem(&va_block_cache, &key);
    if (!info)
        return 0;

    if (va_block) {
        info->va_start = BPF_CORE_READ(va_block, start);
        info->va_end = BPF_CORE_READ(va_block, end);

        /* Navigate: va_block → managed_range → va_range.va_space */
        uvm_va_range_managed_t *managed = BPF_CORE_READ(va_block, managed_range);
        if (managed) {
            uvm_va_space_t *vs = BPF_CORE_READ(managed, va_range.va_space);
            u64 vs_val = 0;
            bpf_probe_read_kernel(&vs_val, sizeof(vs_val), &vs);
            info->va_space = vs_val;
        } else {
            info->va_space = 0;
        }
    } else {
        info->va_start = 0;
        info->va_end = 0;
        info->va_space = 0;
    }

    return 0;
}

/* ===== bpf_wq callback: process context, sleepable ===== */

static int do_prefetch(void *map, int *key, void *value)
{
    struct prefetch_data *data = value;
    if (data && data->va_space && data->length)
        bpf_gpu_migrate_range(data->va_space, data->addr, data->length);
    return 0;
}

/* ===== PREFETCH: always_max + bpf_wq cross-block ===== */

SEC("struct_ops/gpu_page_prefetch")
int BPF_PROG(gpu_page_prefetch,
             uvm_page_index_t page_index,
             uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
             uvm_va_block_region_t *max_prefetch_region,
             uvm_va_block_region_t *result_region)
{
    /* 1) Intra-block: always_max — prefetch entire VA block */
    uvm_page_index_t max_first = BPF_CORE_READ(max_prefetch_region, first);
    uvm_page_index_t max_outer = BPF_CORE_READ(max_prefetch_region, outer);
    bpf_gpu_set_prefetch_region(result_region, max_first, max_outer);

    /* 2) Cross-block: read va_block context from per-CPU kprobe cache,
     * schedule async prefetch of next adjacent block via bpf_wq.
     * Rate-limited: only when entering a new VA block. */
    u32 zero = 0;
    struct va_block_ctx *blk = bpf_map_lookup_elem(&va_block_cache, &zero);
    if (!blk || !blk->va_space || !blk->va_end)
        return 1; /* BYPASS (always_max only) */

    u64 va_space = blk->va_space;
    u64 block_end = blk->va_end;

    u64 *last = bpf_map_lookup_elem(&last_prefetch_block, &zero);
    if (last && *last != block_end) {
        *last = block_end;

        int key = 0;
        struct prefetch_data *data = bpf_map_lookup_elem(&wq_map, &key);
        if (data) {
            data->va_space = va_space;
            data->addr = block_end + 1;
            data->length = VA_BLOCK_SIZE;
            bpf_wq_init(&data->work, &wq_map, 0);
            bpf_wq_set_callback(&data->work, do_prefetch, 0);
            bpf_wq_start(&data->work, 0);
        }
    }

    return 1; /* BYPASS */
}

SEC("struct_ops/gpu_page_prefetch_iter")
int BPF_PROG(gpu_page_prefetch_iter,
             uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
             uvm_va_block_region_t *max_prefetch_region,
             uvm_va_block_region_t *current_region,
             unsigned int counter,
             uvm_va_block_region_t *prefetch_region)
{
    return 0;
}

/* ===== EVICTION: T1-protect + passive MRU ===== */

SEC("struct_ops/gpu_block_activate")
int BPF_PROG(gpu_block_activate,
             uvm_pmm_gpu_t *pmm,
             uvm_gpu_chunk_t *chunk,
             struct list_head *list)
{
    return 0;
}

SEC("struct_ops/gpu_block_access")
int BPF_PROG(gpu_block_access,
             uvm_pmm_gpu_t *pmm,
             uvm_gpu_chunk_t *chunk,
             struct list_head *list)
{
    u32 idx = chunk_hash(chunk);
    u8 *count;

    count = bpf_map_lookup_elem(&access_counts, &idx);
    if (!count)
        return 0;

    u8 c = *count;
    if (c < 255)
        *count = c + 1;

    if (c + 1 >= T1_FREQ_THRESHOLD) {
        /* T1: protect */
        bpf_gpu_block_move_tail(chunk, list);
        return 1;
    }

    /* Non-T1: BYPASS without move = passive MRU */
    return 1;
}

SEC("struct_ops/gpu_evict_prepare")
int BPF_PROG(gpu_evict_prepare,
             uvm_pmm_gpu_t *pmm,
             struct list_head *va_block_used,
             struct list_head *va_block_unused)
{
    return 0;
}

SEC("struct_ops/gpu_test_trigger")
int BPF_PROG(gpu_test_trigger, const char *buf, int len)
{
    return 0;
}

SEC(".struct_ops")
struct gpu_mem_ops uvm_ops_cross_block_v2 = {
    .gpu_test_trigger = (void *)gpu_test_trigger,
    .gpu_page_prefetch = (void *)gpu_page_prefetch,
    .gpu_page_prefetch_iter = (void *)gpu_page_prefetch_iter,
    .gpu_block_activate = (void *)gpu_block_activate,
    .gpu_block_access = (void *)gpu_block_access,
    .gpu_evict_prepare = (void *)gpu_evict_prepare,
};
