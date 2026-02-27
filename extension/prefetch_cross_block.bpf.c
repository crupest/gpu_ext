/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cross-VA-Block Prefetch + Always-Max Intra-Block + Passive MRU Eviction
 *
 * This policy combines:
 * 1) always_max intra-block prefetch (entire VA block on any fault)
 * 2) Cross-block prefetch: on each fault, request async migration of
 *    the next N adjacent VA blocks (2MB each) via deferred work queue
 * 3) Passive MRU eviction (T1 protect, non-T1 freeze LRU position)
 *
 * The cross-block prefetch uses new kfuncs:
 *   bpf_uvm_get_block_start_va() / bpf_uvm_get_block_end_va()
 *   bpf_uvm_request_prefetch_range(addr, length)
 */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "uvm_types.h"
#include "bpf_testmod.h"

char _license[] SEC("license") = "GPL";

#define T1_FREQ_THRESHOLD 3
#define COUNTER_SLOTS 16384
#define COUNTER_MASK (COUNTER_SLOTS - 1)

/* Number of adjacent 2MB VA blocks to prefetch ahead */
#define PREFETCH_AHEAD_BLOCKS 2
/* VA block size = 2MB */
#define VA_BLOCK_SIZE (2ULL * 1024 * 1024)

extern u64 bpf_uvm_get_block_start_va(void) __ksym __weak;
extern u64 bpf_uvm_get_block_end_va(void) __ksym __weak;
extern void bpf_uvm_request_prefetch_range(u64 addr, u64 length) __ksym __weak;

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, COUNTER_SLOTS);
    __type(key, u32);
    __type(value, u8);
} access_counts SEC(".maps");

static __always_inline u32 chunk_hash(uvm_gpu_chunk_t *chunk)
{
    u64 ptr = 0;
    bpf_probe_read_kernel(&ptr, sizeof(ptr), &chunk);
    return (u32)((ptr >> 6) ^ (ptr >> 18)) & COUNTER_MASK;
}

/* ===== PREFETCH: always_max + cross-block ===== */

SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute,
             uvm_page_index_t page_index,
             uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
             uvm_va_block_region_t *max_prefetch_region,
             uvm_va_block_region_t *result_region)
{
    /* 1) Intra-block: always_max — prefetch entire VA block */
    uvm_page_index_t max_first = BPF_CORE_READ(max_prefetch_region, first);
    uvm_page_index_t max_outer = BPF_CORE_READ(max_prefetch_region, outer);
    bpf_uvm_set_va_block_region(result_region, max_first, max_outer);

    /* 2) Cross-block: request prefetch of adjacent block(s) ahead */
    u64 block_end = bpf_uvm_get_block_end_va();
    if (block_end > 0) {
        u64 next_addr = block_end + 1;
        int i;

        /* Request up to PREFETCH_AHEAD_BLOCKS adjacent blocks.
         * Each request is one VA block (2MB). Bounded by loop. */
        for (i = 0; i < PREFETCH_AHEAD_BLOCKS && i < 4; i++) {
            bpf_uvm_request_prefetch_range(next_addr, VA_BLOCK_SIZE);
            next_addr += VA_BLOCK_SIZE;
        }
    }

    return 1; /* BYPASS */
}

SEC("struct_ops/uvm_prefetch_on_tree_iter")
int BPF_PROG(uvm_prefetch_on_tree_iter,
             uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
             uvm_va_block_region_t *max_prefetch_region,
             uvm_va_block_region_t *current_region,
             unsigned int counter,
             uvm_va_block_region_t *prefetch_region)
{
    return 0;
}

/* ===== EVICTION: T1-protect + passive MRU ===== */

SEC("struct_ops/uvm_pmm_chunk_activate")
int BPF_PROG(uvm_pmm_chunk_activate,
             uvm_pmm_gpu_t *pmm,
             uvm_gpu_chunk_t *chunk,
             struct list_head *list)
{
    return 0;
}

SEC("struct_ops/uvm_pmm_chunk_used")
int BPF_PROG(uvm_pmm_chunk_used,
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
        bpf_uvm_pmm_chunk_move_tail(chunk, list);
        return 1;
    }

    /* Non-T1: return BYPASS but DON'T move.
     * This prevents the kernel's default LRU refresh (move to tail).
     * The chunk stays at its current list position, naturally drifting
     * toward HEAD as newer chunks are added at TAIL. */
    return 1; /* BYPASS, no move = passive MRU */
}

SEC("struct_ops/uvm_pmm_eviction_prepare")
int BPF_PROG(uvm_pmm_eviction_prepare,
             uvm_pmm_gpu_t *pmm,
             struct list_head *va_block_used,
             struct list_head *va_block_unused)
{
    return 0;
}

SEC("struct_ops/uvm_bpf_test_trigger_kfunc")
int BPF_PROG(uvm_bpf_test_trigger_kfunc, const char *buf, int len)
{
    return 0;
}

SEC(".struct_ops")
struct uvm_gpu_ext uvm_ops_cross_block = {
    .uvm_bpf_test_trigger_kfunc = (void *)uvm_bpf_test_trigger_kfunc,
    .uvm_prefetch_before_compute = (void *)uvm_prefetch_before_compute,
    .uvm_prefetch_on_tree_iter = (void *)uvm_prefetch_on_tree_iter,
    .uvm_pmm_chunk_activate = (void *)uvm_pmm_chunk_activate,
    .uvm_pmm_chunk_used = (void *)uvm_pmm_chunk_used,
    .uvm_pmm_eviction_prepare = (void *)uvm_pmm_eviction_prepare,
};
