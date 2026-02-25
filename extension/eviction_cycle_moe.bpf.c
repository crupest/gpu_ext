/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cycle-Aware MoE Eviction Policy (Lightweight, Minimal-Overhead)
 *
 * For LLM MoE inference where model >> VRAM:
 * - T1 chunks (attention, embeddings) are accessed every decode step → protect
 * - Expert/other chunks → let kernel default handle (no overhead)
 *
 * Key insight: Only intervene to protect T1 chunks. Let the kernel's
 * default eviction handle everything else. This minimizes BPF overhead
 * while preventing the critical failure mode (attention weight thrashing).
 *
 * Performance-critical: No hash maps. Per-CPU array for O(1) counting.
 * Only T1 chunks get explicit list manipulation (move_tail).
 * Non-T1 chunks: return 0 (kernel default).
 */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "uvm_types.h"
#include "bpf_testmod.h"

char _license[] SEC("license") = "GPL";

/* ========================================================================
 * Configuration
 * ======================================================================== */

/* After this many accesses, a chunk is considered T1 (always-needed). */
#define T1_FREQ_THRESHOLD 3

/* Direct-mapped counter array size (power of 2). */
#define COUNTER_SLOTS 16384
#define COUNTER_MASK (COUNTER_SLOTS - 1)

/* ========================================================================
 * Maps — all O(1) lookups, no hash maps
 * ======================================================================== */

/* Direct-mapped per-CPU access counters. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, COUNTER_SLOTS);
    __type(key, u32);
    __type(value, u8);
} access_counts SEC(".maps");

/* ========================================================================
 * Helpers
 * ======================================================================== */

/* Convert chunk pointer to scalar for hashing.
 * BPF verifier prohibits pointer arithmetic, so we use probe_read. */
static __always_inline u32 chunk_hash(uvm_gpu_chunk_t *chunk)
{
    u64 ptr = 0;
    bpf_probe_read_kernel(&ptr, sizeof(ptr), &chunk);
    return (u32)((ptr >> 6) ^ (ptr >> 18)) & COUNTER_MASK;
}

/* ========================================================================
 * Eviction Policy Hooks
 * ======================================================================== */

SEC("struct_ops/uvm_pmm_chunk_activate")
int BPF_PROG(uvm_pmm_chunk_activate,
             uvm_pmm_gpu_t *pmm,
             uvm_gpu_chunk_t *chunk,
             struct list_head *list)
{
    /* Let kernel default handle activation. No intervention needed. */
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
    if (count) {
        u8 c = *count;
        if (c < 255)
            *count = c + 1;

        if (c + 1 >= T1_FREQ_THRESHOLD) {
            /* T1: frequently accessed chunk → protect from eviction */
            bpf_uvm_pmm_chunk_move_tail(chunk, list);
            return 1; /* BYPASS: we handled it */
        }
    }

    /* Non-T1: let kernel default handle it (zero overhead) */
    return 0;
}

SEC("struct_ops/uvm_pmm_eviction_prepare")
int BPF_PROG(uvm_pmm_eviction_prepare,
             uvm_pmm_gpu_t *pmm,
             struct list_head *va_block_used,
             struct list_head *va_block_unused)
{
    return 0;
}

/* ========================================================================
 * Struct ops registration
 * ======================================================================== */

SEC(".struct_ops")
struct uvm_gpu_ext uvm_ops_cycle_moe = {
    .uvm_bpf_test_trigger_kfunc = (void *)NULL,
    .uvm_prefetch_before_compute = (void *)NULL,
    .uvm_prefetch_on_tree_iter = (void *)NULL,
    .uvm_pmm_chunk_activate = (void *)uvm_pmm_chunk_activate,
    .uvm_pmm_chunk_used = (void *)uvm_pmm_chunk_used,
    .uvm_pmm_eviction_prepare = (void *)uvm_pmm_eviction_prepare,
};
