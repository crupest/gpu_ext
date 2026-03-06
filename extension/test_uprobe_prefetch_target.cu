/*
 * Minimal test target for uprobe-driven GPU prefetch.
 *
 * Allocates UVM memory, triggers a fault to establish va_space in kernel,
 * then calls request_prefetch() which BPF uprobe hooks to do proactive prefetch.
 *
 * Build: nvcc -o test_uprobe_prefetch_target test_uprobe_prefetch_target.cu -lcudart
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>
#include <unistd.h>

/* Hook point for BPF uprobe. Must be noinline so it has a symbol. */
extern "C" __attribute__((noinline))
void request_prefetch(void *addr, size_t length) {
    /* Does nothing — BPF uprobe intercepts this call and does the actual prefetch */
    asm volatile("" :: "r"(addr), "r"(length) : "memory");
}

/* Simple kernel to touch memory */
__global__ void touch_kernel(char *data, size_t n) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        data[idx] += 1;
    }
}

int main(int argc, char **argv) {
    size_t alloc_size = 64 * 1024 * 1024; /* 64 MB default */
    size_t prefetch_offset = 32 * 1024 * 1024; /* prefetch second half */
    size_t prefetch_len = 4 * 1024 * 1024; /* 4 MB */

    if (argc > 1) alloc_size = strtoull(argv[1], NULL, 10) * 1024 * 1024;

    printf("Allocating %zu MB UVM memory...\n", alloc_size / (1024*1024));

    char *data = NULL;
    cudaError_t err = cudaMallocManaged(&data, alloc_size, cudaMemAttachGlobal);
    if (err != cudaSuccess) {
        fprintf(stderr, "cudaMallocManaged failed: %s\n", cudaGetErrorString(err));
        return 1;
    }

    printf("UVM ptr: %p (range: %p - %p)\n", data, data, data + alloc_size);

    /* Touch first page to establish va_space in kernel (triggers page fault) */
    printf("Triggering initial fault to establish va_space...\n");
    touch_kernel<<<1, 1>>>(data, 1);
    cudaDeviceSynchronize();

    printf("va_space should now be captured by kprobe.\n");
    printf("PID: %d\n", getpid());
    printf("\nStarting prefetch test loop...\n");

    for (int round = 0; round < 5; round++) {
        printf("\n--- Round %d ---\n", round);

        /* Request prefetch of a region BEFORE touching it */
        void *prefetch_addr = data + prefetch_offset + round * prefetch_len;
        printf("Requesting prefetch: addr=%p len=%zu\n", prefetch_addr, prefetch_len);
        request_prefetch(prefetch_addr, prefetch_len);

        /* Small delay to let bpf_wq process */
        usleep(10000); /* 10ms */

        /* Now touch the prefetched region — should have fewer faults */
        printf("Touching prefetched region...\n");
        size_t n = prefetch_len;
        touch_kernel<<<(n+255)/256, 256>>>((char*)prefetch_addr, n);
        cudaDeviceSynchronize();

        printf("Done.\n");
    }

    cudaFree(data);
    printf("\nTest complete.\n");
    return 0;
}
