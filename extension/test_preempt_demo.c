// SPDX-License-Identifier: GPL-2.0
/*
 * test_preempt_demo.c - Self-contained GPU preempt demo
 *
 * Single binary combining:
 *   1. BPF kprobe  - auto-captures TSG handles (hClient/hTsg) at CUDA init
 *   2. CUDA driver API - launches long-running GPU kernel
 *   3. ioctl preempt   - preempts the GPU kernel, measures latency & effect
 *
 * Build:
 *   make test_preempt_demo   (in extension/ directory)
 *
 * Run:
 *   sudo ./test_preempt_demo
 *
 * Requires:
 *   - nvidia module (stock or custom — no kernel modification needed)
 *   - libcuda.so (installed with nvidia driver)
 *
 * No kernel modification needed: handle capture uses ioctl interception,
 * preempt uses CUDA's own fd (same nvfp → passes RM security check).
 */

#include <math.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "gpu_preempt.h"
#include "test_preempt_demo.skel.h"

/* ─── Global state ─── */
static volatile int g_running = 1;
static volatile int g_worker_ready = 0;
static int g_nvidia_fd = -1;

/* Worker thread stats */
struct worker_stats {
	uint64_t kernel_count;
	uint64_t total_time_us;
	uint64_t last_kernel_us;
	uint64_t samples[GP_MAX_SAMPLES];
	uint32_t sample_idx;
	int      recording;
};

static struct worker_stats g_stats = {};
static pthread_mutex_t g_stats_lock = PTHREAD_MUTEX_INITIALIZER;

/* ─── Helpers ─── */

static void signal_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

static int libbpf_print_fn(enum libbpf_print_level level, const char *format,
			    va_list args)
{
	if (level == LIBBPF_DEBUG)
		return 0;
	return vfprintf(stderr, format, args);
}

/* ─── GPU worker thread ─── */

static void *gpu_worker(void *arg)
{
	CUcontext ctx = (CUcontext)arg;
	CUmodule module;
	CUfunction kernel;
	CUdeviceptr d_output;
	uint64_t iterations = 100000000ULL; /* ~300ms per kernel on RTX 5090 */

	GP_CHECK_CUDA(cuCtxSetCurrent(ctx));
	GP_CHECK_CUDA(cuModuleLoadData(&module, gp_ptx_source));
	GP_CHECK_CUDA(cuModuleGetFunction(&kernel, module, "busy_loop"));
	GP_CHECK_CUDA(cuMemAlloc(&d_output, sizeof(uint64_t)));

	g_worker_ready = 1;

	while (g_running) {
		void *args[] = { &iterations, &d_output };
		uint64_t start = gp_get_time_us();

		CUresult res = cuLaunchKernel(kernel,
					      128, 1, 1,  /* grid: 128 blocks */
					      256, 1, 1,  /* block: 256 threads */
					      0, 0, args, NULL);
		if (res != CUDA_SUCCESS)
			break;

		cuCtxSynchronize();
		uint64_t dur = gp_get_time_us() - start;

		pthread_mutex_lock(&g_stats_lock);
		g_stats.kernel_count++;
		g_stats.total_time_us += dur;
		g_stats.last_kernel_us = dur;
		if (g_stats.recording && g_stats.sample_idx < GP_MAX_SAMPLES)
			g_stats.samples[g_stats.sample_idx++] = dur;
		pthread_mutex_unlock(&g_stats_lock);
	}

	cuMemFree(d_output);
	cuModuleUnload(module);
	return NULL;
}

/* ─── Main ─── */

int main(int argc, char **argv)
{
	struct test_preempt_demo_bpf *skel = NULL;
	CUdevice device;
	CUcontext cuda_ctx;
	pthread_t worker;
	int err;

	(void)argc; (void)argv;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	printf("=== GPU Preempt Demo (BPF + CUDA + ioctl, single binary) ===\n\n");

	/* ── Phase 1: Load BPF kprobe ── */
	printf("[Phase 1] Loading BPF probes (nvidia_unlocked_ioctl kprobe/kretprobe + nv_gpu_sched_task_init)...\n");

	libbpf_set_print(libbpf_print_fn);

	skel = test_preempt_demo_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	/* Filter: only capture TSGs from our own process */
	skel->rodata->target_pid = getpid();

	err = test_preempt_demo_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF: %d\n", err);
		goto cleanup;
	}

	err = test_preempt_demo_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF probes: %d\n", err);
		fprintf(stderr, "Is nvidia module loaded? (stock or custom)\n");
		goto cleanup;
	}

	printf("  BPF probes attached (filtering PID %d)\n\n", getpid());

	/* ── Phase 2: Initialize CUDA ── */
	printf("[Phase 2] Initializing CUDA...\n");

	GP_CHECK_CUDA(cuInit(0));
	GP_CHECK_CUDA(cuDeviceGet(&device, 0));

	char name[256];
	GP_CHECK_CUDA(cuDeviceGetName(name, sizeof(name), device));
	printf("  GPU: %s\n", name);

	GP_CHECK_CUDA(cuCtxCreate(&cuda_ctx, 0, device));

	/* Small warmup to ensure all TSGs are created */
	gp_cuda_warmup(cuda_ctx);

	/* Give kprobe time to fire */
	usleep(100000);

	/* Read captured TSGs from BPF map */
	int count_fd = bpf_map__fd(skel->maps.tsg_count);
	int tsg_fd = bpf_map__fd(skel->maps.tsg_map);
	uint32_t zero = 0, tsg_count = 0;

	bpf_map_lookup_elem(count_fd, &zero, &tsg_count);
	printf("  Captured %u TSG(s):\n", tsg_count);

	uint32_t gr_hClient = 0, gr_hTsg = 0;

	for (uint32_t i = 0; i < tsg_count && i < 64; i++) {
		struct tsg_entry e;
		if (bpf_map_lookup_elem(tsg_fd, &i, &e) == 0) {
			printf("    [%u] hClient=0x%x hTsg=0x%x engine=%s(%u) tsg_id=%llu\n",
			       i, e.hClient, e.hTsg,
			       gp_engine_str(e.engine_type), e.engine_type,
			       (unsigned long long)e.tsg_id);

			/* Pick last GR (COMPUTE) engine TSG (application, not system) */
			if (e.engine_type == 1) {
				gr_hClient = e.hClient;
				gr_hTsg = e.hTsg;
			}
		}
	}

	if (!gr_hClient) {
		fprintf(stderr, "\nNo GR (COMPUTE) engine TSG found!\n");
		err = 1;
		goto cleanup_cuda;
	}

	printf("\n  Selected GR TSG: hClient=0x%x hTsg=0x%x\n\n", gr_hClient, gr_hTsg);

	/* Find CUDA's nvidia fd (same-process, nvfp matches hClient) */
	g_nvidia_fd = gp_find_cuda_fd(gr_hClient, gr_hTsg, 1);
	if (g_nvidia_fd < 0) {
		fprintf(stderr, "Cannot find CUDA's nvidia fd for preempt!\n");
		err = 1;
		goto cleanup_cuda;
	}

	/* ── Phase 3: Start GPU worker ── */
	printf("[Phase 3] Starting GPU kernel worker thread...\n");

	if (pthread_create(&worker, NULL, gpu_worker, cuda_ctx) != 0) {
		perror("pthread_create");
		err = 1;
		goto cleanup_cuda;
	}

	/* Wait for worker to be ready */
	while (!g_worker_ready && g_running)
		usleep(10000);
	/* Let a few kernels complete for baseline */
	sleep(2);

	uint64_t baseline_kernel_us;
	pthread_mutex_lock(&g_stats_lock);
	baseline_kernel_us = g_stats.last_kernel_us;
	pthread_mutex_unlock(&g_stats_lock);

	printf("  GPU kernel running (baseline iteration: %lu us)\n\n",
	       (unsigned long)baseline_kernel_us);

	/* ── Test A: Preempt latency ── */
	printf("[Test A] Preempt latency (10 rounds)\n");
	{
		uint64_t durations[10];
		int statuses[10];

		for (int i = 0; i < 10 && g_running; i++) {
			uint64_t start = gp_get_time_us();
			statuses[i] = gp_preempt(g_nvidia_fd, gr_hClient, gr_hTsg);
			durations[i] = gp_get_time_us() - start;
			printf("  [%2d] preempt status=%d  duration=%lu us\n",
			       i + 1, statuses[i], (unsigned long)durations[i]);
			usleep(50000); /* 50ms between preempts */
		}

		uint64_t mn = UINT64_MAX, mx = 0, sum = 0;
		int ok = 0;
		for (int i = 0; i < 10; i++) {
			if (statuses[i] == 0) ok++;
			if (durations[i] < mn) mn = durations[i];
			if (durations[i] > mx) mx = durations[i];
			sum += durations[i];
		}
		printf("  → min=%lu avg=%lu max=%lu us, success=%d/10\n\n",
		       (unsigned long)mn, (unsigned long)(sum / 10),
		       (unsigned long)mx, ok);
	}

	/* ── Test B: Timeslice change effect ── */
	if (g_running) {
		printf("[Test B] Timeslice change effect\n");

		/* Measure baseline */
		sleep(2);
		pthread_mutex_lock(&g_stats_lock);
		uint64_t base = g_stats.last_kernel_us;
		uint64_t base_count = g_stats.kernel_count;
		pthread_mutex_unlock(&g_stats_lock);
		printf("  Baseline kernel time: %lu us\n", (unsigned long)base);

		/* Set timeslice to 1us (GPreempt style) */
		int ret = gp_set_timeslice(g_nvidia_fd, gr_hClient, gr_hTsg, 1);
		printf("  Set timeslice=1us: status=%d\n", ret);

		sleep(2);
		pthread_mutex_lock(&g_stats_lock);
		uint64_t after = g_stats.last_kernel_us;
		uint64_t after_count = g_stats.kernel_count;
		pthread_mutex_unlock(&g_stats_lock);

		double delta = base > 0 ? 100.0 * ((double)after - base) / base : 0;
		printf("  After timeslice=1us: %lu us (%+.1f%%)\n",
		       (unsigned long)after, delta);
		printf("  Kernels completed: %lu (baseline) → %lu (timeslice=1us)\n",
		       (unsigned long)base_count, (unsigned long)after_count);

		/* Restore default timeslice */
		ret = gp_set_timeslice(g_nvidia_fd, gr_hClient, gr_hTsg, 16000);
		printf("  Restored timeslice=16000us: status=%d\n\n", ret);
	}

	/* ── Test C: Rapid preempt burst ── */
	if (g_running) {
		printf("[Test C] Rapid preempt burst (100x)\n");

		int ok = 0, fail = 0;
		uint64_t start = gp_get_time_us();

		for (int i = 0; i < 100 && g_running; i++) {
			int ret = gp_preempt(g_nvidia_fd, gr_hClient, gr_hTsg);
			if (ret == 0)
				ok++;
			else
				fail++;
		}

		uint64_t total = gp_get_time_us() - start;
		printf("  100 preempts in %lu us (avg=%lu us/preempt)\n",
		       (unsigned long)total,
		       (unsigned long)(total / 100));
		printf("  success=%d fail=%d\n", ok, fail);
		if (total > 0)
			printf("  throughput: %lu preempts/sec\n",
			       (unsigned long)(100ULL * 1000000 / total));

		/* Verify GPU process still alive */
		usleep(200000);
		pthread_mutex_lock(&g_stats_lock);
		uint64_t final_count = g_stats.kernel_count;
		pthread_mutex_unlock(&g_stats_lock);
		printf("  GPU worker alive: %s (kernels completed: %lu)\n\n",
		       final_count > 0 ? "yes" : "no",
		       (unsigned long)final_count);
	}

	/* ── Test D: Prove preempt interrupts GPU execution ── */
	if (g_running) {
		printf("[Test D] Continuous preempt impact on kernel execution time\n");
		printf("  Phase 1: Measure 10 kernels WITHOUT preempt...\n");

		/* Collect baseline samples */
		pthread_mutex_lock(&g_stats_lock);
		g_stats.sample_idx = 0;
		g_stats.recording = 1;
		pthread_mutex_unlock(&g_stats_lock);

		/* Wait for 10 kernels to complete */
		while (g_running) {
			pthread_mutex_lock(&g_stats_lock);
			uint32_t n = g_stats.sample_idx;
			pthread_mutex_unlock(&g_stats_lock);
			if (n >= 10) break;
			usleep(50000);
		}

		pthread_mutex_lock(&g_stats_lock);
		g_stats.recording = 0;
		uint32_t n_base = g_stats.sample_idx;
		uint64_t base_samples[GP_MAX_SAMPLES];
		for (uint32_t i = 0; i < n_base; i++)
			base_samples[i] = g_stats.samples[i];
		pthread_mutex_unlock(&g_stats_lock);

		/* Compute baseline stats */
		uint64_t base_sum = 0, base_min = UINT64_MAX, base_max = 0;
		for (uint32_t i = 0; i < n_base; i++) {
			base_sum += base_samples[i];
			if (base_samples[i] < base_min) base_min = base_samples[i];
			if (base_samples[i] > base_max) base_max = base_samples[i];
		}
		uint64_t base_avg = n_base > 0 ? base_sum / n_base : 0;
		printf("  Baseline: %u kernels, avg=%lu min=%lu max=%lu us\n",
		       n_base, (unsigned long)base_avg,
		       (unsigned long)base_min, (unsigned long)base_max);

		/* Phase 2: continuous preempt while kernels run */
		printf("  Phase 2: Measure 10 kernels WITH continuous preempt...\n");

		pthread_mutex_lock(&g_stats_lock);
		g_stats.sample_idx = 0;
		g_stats.recording = 1;
		pthread_mutex_unlock(&g_stats_lock);

		uint64_t preempt_count = 0;
		uint64_t preempt_start = gp_get_time_us();

		while (g_running) {
			/* Preempt as fast as possible */
			gp_preempt(g_nvidia_fd, gr_hClient, gr_hTsg);
			preempt_count++;

			pthread_mutex_lock(&g_stats_lock);
			uint32_t n = g_stats.sample_idx;
			pthread_mutex_unlock(&g_stats_lock);
			if (n >= 10) break;
		}

		uint64_t preempt_elapsed = gp_get_time_us() - preempt_start;

		pthread_mutex_lock(&g_stats_lock);
		g_stats.recording = 0;
		uint32_t n_preempted = g_stats.sample_idx;
		uint64_t preempt_samples[GP_MAX_SAMPLES];
		for (uint32_t i = 0; i < n_preempted; i++)
			preempt_samples[i] = g_stats.samples[i];
		pthread_mutex_unlock(&g_stats_lock);

		/* Compute preempted stats */
		uint64_t p_sum = 0, p_min = UINT64_MAX, p_max = 0;
		for (uint32_t i = 0; i < n_preempted; i++) {
			p_sum += preempt_samples[i];
			if (preempt_samples[i] < p_min) p_min = preempt_samples[i];
			if (preempt_samples[i] > p_max) p_max = preempt_samples[i];
		}
		uint64_t p_avg = n_preempted > 0 ? p_sum / n_preempted : 0;

		printf("  Preempted: %u kernels, avg=%lu min=%lu max=%lu us\n",
		       n_preempted, (unsigned long)p_avg,
		       (unsigned long)p_min, (unsigned long)p_max);
		printf("  Preempts issued: %lu in %lu us (%.0f preempts/sec)\n",
		       (unsigned long)preempt_count,
		       (unsigned long)preempt_elapsed,
		       preempt_elapsed > 0 ?
		           (double)preempt_count * 1e6 / preempt_elapsed : 0);

		if (base_avg > 0) {
			double slowdown = 100.0 * ((double)p_avg - base_avg) / base_avg;
			printf("\n  *** RESULT: kernel time %+.1f%% (%lu → %lu us) ***\n",
			       slowdown, (unsigned long)base_avg, (unsigned long)p_avg);
			if (slowdown > 1.0)
				printf("  → PREEMPT IS INTERRUPTING GPU EXECUTION\n");
			else if (slowdown > -1.0)
				printf("  → No measurable impact (preempt overhead hidden by GPU)\n");
			else
				printf("  → Unexpected speedup (noise?)\n");
		}
		printf("\n");
	}

	/* ── Results ── */
	printf("=== Demo complete ===\n");
	printf("Stopping GPU worker...\n");

	g_running = 0;
	pthread_join(worker, NULL);

	pthread_mutex_lock(&g_stats_lock);
	printf("\nGPU worker stats: %lu kernels, avg %lu us/kernel\n",
	       (unsigned long)g_stats.kernel_count,
	       g_stats.kernel_count > 0
		   ? (unsigned long)(g_stats.total_time_us / g_stats.kernel_count)
		   : 0UL);
	pthread_mutex_unlock(&g_stats_lock);

cleanup_cuda:
	cuCtxDestroy(cuda_ctx);
cleanup:
	test_preempt_demo_bpf__destroy(skel);
	return err < 0 ? -err : err;
}
