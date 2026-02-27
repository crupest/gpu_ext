/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Combined Always-Max Prefetch + MRU Expert Eviction Policy
 *
 * Theory: For cyclic access patterns (LLM decode: layer 0→35→0→35...),
 * LRU is the worst eviction policy — it evicts the item needed soonest.
 * MRU (Most Recently Used) is optimal for cyclic patterns: the most
 * recently used item has the maximum distance to its next use.
 *
 * But pure MRU is catastrophic because it also evicts attention weights
 * (T1 chunks) which are needed EVERY step, not just once per cycle.
 *
 * Solution: T1 chunks (high frequency) → protect (move_tail)
 *           Non-T1 chunks (experts)    → MRU (move_head = evict sooner)
 *
 * Combined with always_max prefetch for intra-VA-block optimization.
 */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "uvm_types.h"
#include "bpf_testmod.h"

char _license[] SEC("license") = "GPL";

/* T1 frequency threshold: chunks accessed >= this many times are T1 */
#define T1_FREQ_THRESHOLD 3

/* Direct-mapped per-CPU access counters */
#define COUNTER_SLOTS 16384
#define COUNTER_MASK (COUNTER_SLOTS - 1)

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

/* ===== PREFETCH: always_max ===== */

SEC("struct_ops/uvm_prefetch_before_compute")
int BPF_PROG(uvm_prefetch_before_compute,
             uvm_page_index_t page_index,
             uvm_perf_prefetch_bitmap_tree_t *bitmap_tree,
             uvm_va_block_region_t *max_prefetch_region,
             uvm_va_block_region_t *result_region)
{
    uvm_page_index_t max_first = BPF_CORE_READ(max_prefetch_region, first);
    uvm_page_index_t max_outer = BPF_CORE_READ(max_prefetch_region, outer);
    bpf_uvm_set_va_block_region(result_region, max_first, max_outer);
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

/* ===== EVICTION: T1-protect + MRU-for-experts ===== */

SEC("struct_ops/uvm_pmm_chunk_activate")
int BPF_PROG(uvm_pmm_chunk_activate,
             uvm_pmm_gpu_t *pmm,
             uvm_gpu_chunk_t *chunk,
             struct list_head *list)
{
    return 0; /* kernel default */
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
        /* T1 chunk: always-needed (attention, embeddings) → protect */
        bpf_uvm_pmm_chunk_move_tail(chunk, list);
        return 1; /* BYPASS */
    }

    /* Non-T1 (expert chunks): MRU eviction
     * Move to HEAD = evict first. For cyclic access patterns,
     * the most recently used item has maximum distance to next use.
     */
    bpf_uvm_pmm_chunk_move_head(chunk, list);
    return 1; /* BYPASS */
}

SEC("struct_ops/uvm_pmm_eviction_prepare")
int BPF_PROG(uvm_pmm_eviction_prepare,
             uvm_pmm_gpu_t *pmm,
             struct list_head *va_block_used,
             struct list_head *va_block_unused)
{
    return 0;
}

/* ===== Registration ===== */

SEC("struct_ops/uvm_bpf_test_trigger_kfunc")
int BPF_PROG(uvm_bpf_test_trigger_kfunc, const char *buf, int len)
{
    return 0;
}

SEC(".struct_ops")
struct uvm_gpu_ext uvm_ops_max_mru_expert = {
    .uvm_bpf_test_trigger_kfunc = (void *)uvm_bpf_test_trigger_kfunc,
    .uvm_prefetch_before_compute = (void *)uvm_prefetch_before_compute,
    .uvm_prefetch_on_tree_iter = (void *)uvm_prefetch_on_tree_iter,
    .uvm_pmm_chunk_activate = (void *)uvm_pmm_chunk_activate,
    .uvm_pmm_chunk_used = (void *)uvm_pmm_chunk_used,
    .uvm_pmm_eviction_prepare = (void *)uvm_pmm_eviction_prepare,
};
