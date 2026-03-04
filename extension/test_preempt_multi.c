// SPDX-License-Identifier: GPL-2.0
/*
 * test_preempt_multi.c - Multi-context GPU preempt demonstration
 *
 * Proves that TSG preemption benefits competing GPU tasks:
 *
 *   Test E: Two equal contexts competing for GPU. Preempting context B's
 *           TSG gives context A more GPU time (throughput shift).
 *
 *   Test F: Context A runs short kernels (latency-sensitive), context B
 *           runs long kernels (throughput). Without preempt, A waits up
 *           to one timeslice (~16ms). With preempt, A's scheduling wait
 *           drops to ~350us (preempt latency).
 *
 * Build:
 *   make test_preempt_multi   (in extension/ directory)
 *
 * Run:
 *   sudo ./test_preempt_multi
 *
 * No kernel modification needed.
 */

#include <math.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "gpu_preempt.h"
#include "test_preempt_multi.skel.h"

static volatile int g_running = 1;

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

/* Compute stats from worker samples */
static void compute_stats(uint64_t *samples, uint32_t n,
			   uint64_t *out_avg, uint64_t *out_min,
			   uint64_t *out_max, double *out_stddev)
{
	if (n == 0) {
		*out_avg = *out_min = *out_max = 0;
		*out_stddev = 0;
		return;
	}

	uint64_t sum = 0, mn = UINT64_MAX, mx = 0;
	for (uint32_t i = 0; i < n; i++) {
		sum += samples[i];
		if (samples[i] < mn) mn = samples[i];
		if (samples[i] > mx) mx = samples[i];
	}
	*out_avg = sum / n;
	*out_min = mn;
	*out_max = mx;

	double mean = (double)sum / n;
	double var = 0;
	for (uint32_t i = 0; i < n; i++) {
		double d = (double)samples[i] - mean;
		var += d * d;
	}
	*out_stddev = sqrt(var / n);
}

/* Collect N kernel samples from a worker */
static uint32_t collect_samples(struct gp_worker *w, uint32_t n,
				uint64_t *out, int timeout_sec)
{
	pthread_mutex_lock(&w->lock);
	w->sample_idx = 0;
	w->recording = 1;
	pthread_mutex_unlock(&w->lock);

	uint64_t deadline = gp_get_time_us() + (uint64_t)timeout_sec * 1000000;

	while (g_running && gp_get_time_us() < deadline) {
		pthread_mutex_lock(&w->lock);
		uint32_t got = w->sample_idx;
		pthread_mutex_unlock(&w->lock);
		if (got >= n) break;
		usleep(10000);
	}

	pthread_mutex_lock(&w->lock);
	w->recording = 0;
	uint32_t got = w->sample_idx < n ? w->sample_idx : n;
	for (uint32_t i = 0; i < got; i++)
		out[i] = w->samples[i];
	pthread_mutex_unlock(&w->lock);

	return got;
}

/* Collect samples while continuously preempting a TSG */
static uint32_t collect_samples_with_preempt(struct gp_worker *w, uint32_t n,
					     uint64_t *out,
					     int nvidia_fd,
					     uint32_t hClient, uint32_t hTsg,
					     uint64_t *preempt_count_out,
					     int timeout_sec)
{
	pthread_mutex_lock(&w->lock);
	w->sample_idx = 0;
	w->recording = 1;
	pthread_mutex_unlock(&w->lock);

	uint64_t preempts = 0;
	uint64_t deadline = gp_get_time_us() + (uint64_t)timeout_sec * 1000000;

	while (g_running && gp_get_time_us() < deadline) {
		gp_preempt(nvidia_fd, hClient, hTsg);
		preempts++;

		pthread_mutex_lock(&w->lock);
		uint32_t got = w->sample_idx;
		pthread_mutex_unlock(&w->lock);
		if (got >= n) break;
	}

	pthread_mutex_lock(&w->lock);
	w->recording = 0;
	uint32_t got = w->sample_idx < n ? w->sample_idx : n;
	for (uint32_t i = 0; i < got; i++)
		out[i] = w->samples[i];
	pthread_mutex_unlock(&w->lock);

	*preempt_count_out = preempts;
	return got;
}

int main(int argc, char **argv)
{
	struct test_preempt_multi_bpf *skel = NULL;
	CUdevice device;
	CUcontext ctx_a, ctx_b;
	struct gp_worker worker_a, worker_b;
	int nvidia_fd = -1;
	int err = 0;

	(void)argc; (void)argv;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	printf("=== GPU Preempt Multi-Context Test ===\n\n");

	/* ── Phase 1: Load BPF probes ── */
	printf("[Phase 1] Loading BPF probes...\n");

	libbpf_set_print(libbpf_print_fn);

	skel = test_preempt_multi_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}
	skel->rodata->target_pid = getpid();

	err = test_preempt_multi_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF: %d\n", err);
		goto cleanup;
	}
	err = test_preempt_multi_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF: %d\n", err);
		goto cleanup;
	}
	printf("  BPF probes attached (PID %d)\n\n", getpid());

	int count_fd = bpf_map__fd(skel->maps.tsg_count);
	int tsg_fd = bpf_map__fd(skel->maps.tsg_map);

	/* ── Phase 2: Create context A ── */
	printf("[Phase 2] Creating CUDA context A...\n");

	GP_CHECK_CUDA(cuInit(0));
	GP_CHECK_CUDA(cuDeviceGet(&device, 0));

	char gpu_name[256];
	GP_CHECK_CUDA(cuDeviceGetName(gpu_name, sizeof(gpu_name), device));
	printf("  GPU: %s\n", gpu_name);

	GP_CHECK_CUDA(cuCtxCreate(&ctx_a, 0, device));
	gp_cuda_warmup(ctx_a);
	usleep(100000);

	/* Read TSGs for context A */
	uint32_t zero = 0, count_a = 0;
	bpf_map_lookup_elem(count_fd, &zero, &count_a);

	uint32_t a_hClient = 0, a_hTsg = 0;
	for (uint32_t i = 0; i < count_a; i++) {
		struct tsg_entry e;
		if (bpf_map_lookup_elem(tsg_fd, &i, &e) == 0) {
			printf("    [A:%u] hClient=0x%x hTsg=0x%x engine=%s\n",
			       i, e.hClient, e.hTsg, gp_engine_str(e.engine_type));
			if (e.engine_type == 1) {
				a_hClient = e.hClient;
				a_hTsg = e.hTsg;
			}
		}
	}

	if (!a_hClient) {
		fprintf(stderr, "No GR TSG found for context A!\n");
		err = 1;
		goto cleanup_ctx;
	}
	printf("  Context A GR TSG: hClient=0x%x hTsg=0x%x\n\n",
	       a_hClient, a_hTsg);

	/* ── Phase 3: Create context B ── */
	printf("[Phase 3] Creating CUDA context B...\n");

	GP_CHECK_CUDA(cuCtxCreate(&ctx_b, 0, device));
	gp_cuda_warmup(ctx_b);
	usleep(100000);

	/* Read new TSGs for context B */
	uint32_t count_b = 0;
	bpf_map_lookup_elem(count_fd, &zero, &count_b);

	uint32_t b_hClient = 0, b_hTsg = 0;
	for (uint32_t i = count_a; i < count_b; i++) {
		struct tsg_entry e;
		if (bpf_map_lookup_elem(tsg_fd, &i, &e) == 0) {
			printf("    [B:%u] hClient=0x%x hTsg=0x%x engine=%s\n",
			       i, e.hClient, e.hTsg, gp_engine_str(e.engine_type));
			if (e.engine_type == 1) {
				b_hClient = e.hClient;
				b_hTsg = e.hTsg;
			}
		}
	}

	if (!b_hClient) {
		fprintf(stderr, "No GR TSG found for context B!\n");
		err = 1;
		goto cleanup_ctx;
	}
	printf("  Context B GR TSG: hClient=0x%x hTsg=0x%x\n\n",
	       b_hClient, b_hTsg);

	/* Find CUDA's nvidia fd */
	nvidia_fd = gp_find_cuda_fd(a_hClient, a_hTsg, 1);
	if (nvidia_fd < 0) {
		fprintf(stderr, "Cannot find CUDA's nvidia fd!\n");
		err = 1;
		goto cleanup_ctx;
	}
	printf("\n");

	/* ══════════════════════════════════════════════════════════════
	 * Test E: Two equal contexts — preempt one, other gets more GPU
	 * ══════════════════════════════════════════════════════════════ */
	if (g_running) {
		printf("═══════════════════════════════════════════════════════\n");
		printf("[Test E] Two equal contexts — preempt B → A gets more GPU\n");
		printf("═══════════════════════════════════════════════════════\n");

		uint64_t equal_iters = 100000000ULL; /* ~300ms per kernel */

		gp_worker_init(&worker_a, ctx_a, equal_iters, &g_running);
		gp_worker_init(&worker_b, ctx_b, equal_iters, &g_running);

		gp_worker_start(&worker_a);
		gp_worker_start(&worker_b);
		gp_worker_wait_ready(&worker_a);
		gp_worker_wait_ready(&worker_b);

		/* Warmup: let both compete for a bit */
		sleep(2);

		/* Phase 1: No preempt — measure A's kernel times */
		printf("  Phase 1: No preempt (both competing equally)...\n");
		uint64_t samples_e1[GP_MAX_SAMPLES];
		uint32_t n_e1 = collect_samples(&worker_a, 10, samples_e1, 30);

		uint64_t e1_avg, e1_min, e1_max;
		double e1_std;
		compute_stats(samples_e1, n_e1, &e1_avg, &e1_min, &e1_max, &e1_std);
		printf("  A without preempt: %u kernels, avg=%lu min=%lu max=%lu stddev=%.0f us\n",
		       n_e1, (unsigned long)e1_avg, (unsigned long)e1_min,
		       (unsigned long)e1_max, e1_std);

		/* Also measure B baseline */
		uint64_t samples_b1[GP_MAX_SAMPLES];
		uint32_t n_b1 = collect_samples(&worker_b, 10, samples_b1, 30);
		uint64_t b1_avg, b1_min, b1_max;
		double b1_std;
		compute_stats(samples_b1, n_b1, &b1_avg, &b1_min, &b1_max, &b1_std);
		printf("  B without preempt: %u kernels, avg=%lu us\n",
		       n_b1, (unsigned long)b1_avg);

		/* Phase 2: Continuously preempt B — measure A's improvement */
		if (g_running) {
			printf("\n  Phase 2: Continuously preempt B...\n");
			uint64_t samples_e2[GP_MAX_SAMPLES];
			uint64_t preempt_count = 0;
			uint32_t n_e2 = collect_samples_with_preempt(
				&worker_a, 10, samples_e2,
				nvidia_fd, b_hClient, b_hTsg,
				&preempt_count, 30);

			uint64_t e2_avg, e2_min, e2_max;
			double e2_std;
			compute_stats(samples_e2, n_e2, &e2_avg, &e2_min, &e2_max, &e2_std);
			printf("  A with B preempted: %u kernels, avg=%lu min=%lu max=%lu stddev=%.0f us\n",
			       n_e2, (unsigned long)e2_avg, (unsigned long)e2_min,
			       (unsigned long)e2_max, e2_std);
			printf("  Preempts issued: %lu\n", (unsigned long)preempt_count);

			if (e1_avg > 0) {
				double change = 100.0 * ((double)e2_avg - e1_avg) / e1_avg;
				printf("\n  *** RESULT E: A kernel time %+.1f%% (%lu → %lu us) ***\n",
				       change, (unsigned long)e1_avg, (unsigned long)e2_avg);
				if (change < -5.0)
					printf("  → PREEMPTING B GIVES A MORE GPU TIME\n");
				else if (change > 5.0)
					printf("  → Unexpected: A slowed down (preempt overhead?)\n");
				else
					printf("  → No significant change\n");
			}
		}

		/* Stop workers for Test E */
		g_running = 0;
		gp_worker_join(&worker_a);
		gp_worker_join(&worker_b);
		g_running = 1; /* Reset for Test F */
		printf("\n");
	}

	/* ══════════════════════════════════════════════════════════════
	 * Test F: Short-kernel A + long-kernel B — preempt reduces
	 *         A's scheduling wait from ~timeslice to ~preempt_latency
	 * ══════════════════════════════════════════════════════════════ */
	if (g_running) {
		printf("═══════════════════════════════════════════════════════\n");
		printf("[Test F] Short kernel A + long kernel B — preempt reduces A's latency\n");
		printf("═══════════════════════════════════════════════════════\n");

		/*
		 * A: 1M iterations (~3ms per kernel) — latency-sensitive
		 * B: 100M iterations (~300ms per kernel) — throughput task
		 *
		 * Without preempt: A must wait for B's timeslice to expire
		 * before getting GPU. Observed A time ≈ kernel_time + sched_wait.
		 *
		 * With preempt of B: B gets preempted, A runs immediately.
		 * A's sched_wait drops from ~16ms to ~350us.
		 */
		uint64_t a_iters = 1000000ULL;     /* ~3ms */
		uint64_t b_iters = 100000000ULL;   /* ~300ms */

		gp_worker_init(&worker_a, ctx_a, a_iters, &g_running);
		gp_worker_init(&worker_b, ctx_b, b_iters, &g_running);

		gp_worker_start(&worker_a);
		gp_worker_start(&worker_b);
		gp_worker_wait_ready(&worker_a);
		gp_worker_wait_ready(&worker_b);

		/* Warmup */
		sleep(2);

		/* Phase 1: No preempt — A's time includes scheduling waits */
		printf("  Phase 1: No preempt (A=short kernel, B=long kernel)...\n");
		uint64_t samples_f1[GP_MAX_SAMPLES];
		uint32_t n_f1 = collect_samples(&worker_a, 20, samples_f1, 60);

		uint64_t f1_avg, f1_min, f1_max;
		double f1_std;
		compute_stats(samples_f1, n_f1, &f1_avg, &f1_min, &f1_max, &f1_std);
		printf("  A without preempt: %u kernels, avg=%lu min=%lu max=%lu stddev=%.0f us\n",
		       n_f1, (unsigned long)f1_avg, (unsigned long)f1_min,
		       (unsigned long)f1_max, f1_std);
		printf("  (includes scheduling wait behind B's long kernels)\n");

		/* Phase 2: Preempt B — A should get lower latency */
		if (g_running) {
			printf("\n  Phase 2: Continuously preempt B (A should get lower latency)...\n");
			uint64_t samples_f2[GP_MAX_SAMPLES];
			uint64_t preempt_count = 0;
			uint32_t n_f2 = collect_samples_with_preempt(
				&worker_a, 20, samples_f2,
				nvidia_fd, b_hClient, b_hTsg,
				&preempt_count, 60);

			uint64_t f2_avg, f2_min, f2_max;
			double f2_std;
			compute_stats(samples_f2, n_f2, &f2_avg, &f2_min, &f2_max, &f2_std);
			printf("  A with B preempted: %u kernels, avg=%lu min=%lu max=%lu stddev=%.0f us\n",
			       n_f2, (unsigned long)f2_avg, (unsigned long)f2_min,
			       (unsigned long)f2_max, f2_std);
			printf("  Preempts issued: %lu\n", (unsigned long)preempt_count);

			if (f1_avg > 0) {
				double change = 100.0 * ((double)f2_avg - f1_avg) / f1_avg;
				printf("\n  *** RESULT F: A kernel time %+.1f%% (%lu → %lu us) ***\n",
				       change, (unsigned long)f1_avg, (unsigned long)f2_avg);
				if (change < -10.0)
					printf("  → PREEMPT REDUCES A's SCHEDULING WAIT\n");
				else if (change > 10.0)
					printf("  → Unexpected: A slowed down\n");
				else
					printf("  → No significant change (sched_wait may already be low)\n");
			}
		}

		g_running = 0;
		gp_worker_join(&worker_a);
		gp_worker_join(&worker_b);
		printf("\n");
	}

	printf("=== Multi-context test complete ===\n");

cleanup_ctx:
	cuCtxDestroy(ctx_b);
	cuCtxDestroy(ctx_a);
cleanup:
	test_preempt_multi_bpf__destroy(skel);
	return err < 0 ? -err : err;
}
