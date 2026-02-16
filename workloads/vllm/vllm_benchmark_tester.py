#!/usr/bin/env python3
import os
import json
import time
import subprocess
import sys
import matplotlib.pyplot as plt
import numpy as np
from datetime import datetime
import re

# Add scheduler module to path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', 'scheduler')))
from scheduler_runner import SchedulerRunner, SchedulerBenchmark


class VLLMBenchmarkTester(SchedulerBenchmark):
    def __init__(self, num_prompts=100, dataset_path=None,
                 output_file='results/vllm_results.json', repeat=1, bench_cmd=None):
        super().__init__()
        self.num_prompts = num_prompts
        self.dataset_path = dataset_path or "/home/yunwei37/workspace/schedcp/workloads/vllm/datasets/ShareGPT_V3_unfiltered_cleaned_split.json"
        self.output_file = output_file
        self.repeat = repeat
        self.results = {}
        self.venv_activate = "~/workspace/.venv/bin/activate"
        self.bench_cmd = bench_cmd  # Custom benchmark command

        # Create results directory if it doesn't exist
        os.makedirs(os.path.dirname(self.output_file), exist_ok=True)

        # Check if dataset exists (only for default vLLM mode)
        if not bench_cmd and not os.path.exists(self.dataset_path):
            raise FileNotFoundError(f"Dataset not found: {self.dataset_path}")

    def parse_benchmark_output(self, output):
        """Parse vLLM benchmark output and extract metrics."""
        metrics = {}

        # Patterns to match metrics
        patterns = {
            'successful_requests': r'Successful requests:\s+(\d+)',
            'benchmark_duration': r'Benchmark duration \(s\):\s+([\d.]+)',
            'total_input_tokens': r'Total input tokens:\s+(\d+)',
            'total_generated_tokens': r'Total generated tokens:\s+(\d+)',
            'request_throughput': r'Request throughput \(req/s\):\s+([\d.]+)',
            'output_token_throughput': r'Output token throughput \(tok/s\):\s+([\d.]+)',
            'peak_output_token_throughput': r'Peak output token throughput \(tok/s\):\s+([\d.]+)',
            'peak_concurrent_requests': r'Peak concurrent requests:\s+([\d.]+)',
            'total_token_throughput': r'Total Token throughput \(tok/s\):\s+([\d.]+)',
            'mean_ttft': r'Mean TTFT \(ms\):\s+([\d.]+)',
            'median_ttft': r'Median TTFT \(ms\):\s+([\d.]+)',
            'p99_ttft': r'P99 TTFT \(ms\):\s+([\d.]+)',
            'mean_tpot': r'Mean TPOT \(ms\):\s+([\d.]+)',
            'median_tpot': r'Median TPOT \(ms\):\s+([\d.]+)',
            'p99_tpot': r'P99 TPOT \(ms\):\s+([\d.]+)',
            'mean_itl': r'Mean ITL \(ms\):\s+([\d.]+)',
            'median_itl': r'Median ITL \(ms\):\s+([\d.]+)',
            'p99_itl': r'P99 ITL \(ms\):\s+([\d.]+)',
        }

        for key, pattern in patterns.items():
            match = re.search(pattern, output)
            if match:
                value = match.group(1)
                # Convert to appropriate type
                if '.' in value:
                    metrics[key] = float(value)
                else:
                    metrics[key] = int(value)

        return metrics

    def _cleanup_schedulers(self):
        """Ensure all scheduler processes are stopped"""
        try:
            # Stop all tracked schedulers
            self.runner.stop_all_schedulers()
            # Kill any remaining scx_ processes
            subprocess.run(['sudo', 'pkill', '-9', 'scx_'], check=False, capture_output=True)
            time.sleep(0.5)
        except Exception as e:
            print(f"Warning during scheduler cleanup: {e}")

    def run_vllm_benchmark(self, scheduler_name=None):
        """Run benchmark with optional scheduler"""
        scheduler = None

        try:
            # Start scheduler if specified
            if scheduler_name:
                # Ensure no schedulers are running before starting
                self._cleanup_schedulers()

                print(f"Starting scheduler: {scheduler_name}")
                try:
                    scheduler = self.runner.start_scheduler(scheduler_name)
                    if not scheduler:
                        print(f"Failed to start scheduler {scheduler_name}")
                        return None
                    time.sleep(2)  # Give scheduler time to initialize
                except Exception as e:
                    print(f"Error starting scheduler {scheduler_name}: {e}")
                    self._cleanup_schedulers()  # Clean up on error
                    return None

            # Use custom command if provided, otherwise default to vLLM
            if self.bench_cmd:
                cmd = self.bench_cmd
                print(f"Running: {cmd}")
            else:
                cmd = f". {self.venv_activate} && vllm bench serve " \
                      f"--model Qwen/Qwen3-Coder-30B-A3B-Instruct-FP8 " \
                      f"--dataset-name sharegpt " \
                      f"--num-prompts {self.num_prompts} " \
                      f"--dataset-path {self.dataset_path}"
                print(f"Running: vllm bench serve with {self.num_prompts} prompts")

            # Run benchmark and capture output
            start_time = time.time()
            result = subprocess.run(
                cmd,
                shell=True,
                executable="/bin/bash",
                capture_output=True,
                text=True
            )
            end_time = time.time()

            # Print output for monitoring
            print(result.stdout)
            if result.stderr:
                print(result.stderr, file=sys.stderr)

            if result.returncode != 0:
                print(f"Benchmark failed with return code {result.returncode}")
                return None

            # Parse metrics
            metrics = self.parse_benchmark_output(result.stdout)
            metrics['scheduler'] = scheduler_name or 'baseline'
            metrics['num_prompts'] = self.num_prompts
            metrics['success'] = True
            metrics['total_time'] = end_time - start_time

            return metrics

        finally:
            # Stop scheduler if it was started
            if scheduler:
                print(f"Stopping scheduler: {scheduler_name}")
                self.runner.stop_scheduler(scheduler_name)
                time.sleep(1)
                # Double-check cleanup
                self._cleanup_schedulers()

    def run_all_tests(self, skip_baseline=False, specific_schedulers=None):
        """Run tests with all schedulers"""
        schedulers = self.runner.get_available_schedulers()

        if specific_schedulers:
            schedulers = [s for s in schedulers if s in specific_schedulers]

        # Run baseline test
        if not skip_baseline:
            print("\nRunning baseline test (no scheduler)...")
            baseline_results = []
            for i in range(self.repeat):
                if self.repeat > 1:
                    print(f"  Run {i+1}/{self.repeat}")
                result = self.run_vllm_benchmark()
                if result:
                    baseline_results.append(result)

            if baseline_results:
                self.results['baseline'] = self._aggregate_results(baseline_results, 'baseline')
            print("-" * 60)

        # Run tests with each scheduler
        for scheduler in schedulers:
            print(f"\nTesting with scheduler: {scheduler}")
            scheduler_results = []

            for i in range(self.repeat):
                if self.repeat > 1:
                    print(f"  Run {i+1}/{self.repeat}")
                result = self.run_vllm_benchmark(scheduler)
                if result:
                    scheduler_results.append(result)
                else:
                    print(f"  Run {i+1} failed")

            if scheduler_results:
                self.results[scheduler] = self._aggregate_results(scheduler_results, scheduler)
            else:
                print(f"All runs failed for scheduler: {scheduler}")
            print("-" * 60)

        # Save results
        self.save_results()

    def _aggregate_results(self, results, scheduler_name):
        """Aggregate multiple run results"""
        if not results:
            return None

        # Collect all numeric metrics
        metrics_keys = [k for k in results[0].keys() if isinstance(results[0][k], (int, float))]

        aggregated = {
            'scheduler': scheduler_name,
            'num_prompts': self.num_prompts,
            'runs': len(results),
            'raw_results': results
        }

        # Calculate statistics for each metric
        for key in metrics_keys:
            values = [r[key] for r in results if key in r]
            if values:
                aggregated[f'avg_{key}'] = np.mean(values)
                aggregated[f'std_{key}'] = np.std(values)
                aggregated[f'min_{key}'] = np.min(values)
                aggregated[f'max_{key}'] = np.max(values)

        return aggregated

    def _convert_to_native_types(self, obj):
        """Convert NumPy types to native Python types for JSON serialization"""
        if isinstance(obj, dict):
            return {key: self._convert_to_native_types(value) for key, value in obj.items()}
        elif isinstance(obj, list):
            return [self._convert_to_native_types(item) for item in obj]
        elif isinstance(obj, np.integer):
            return int(obj)
        elif isinstance(obj, np.floating):
            return float(obj)
        elif isinstance(obj, np.bool_):
            return bool(obj)
        elif isinstance(obj, np.ndarray):
            return obj.tolist()
        return obj

    def save_results(self):
        """Save test results to JSON file"""
        output_data = {
            'test_info': {
                'timestamp': datetime.now().isoformat(),
                'num_prompts': self.num_prompts,
                'dataset_path': self.dataset_path,
                'repeat': self.repeat
            },
            'results': self.results
        }

        # Convert NumPy types to native Python types
        output_data = self._convert_to_native_types(output_data)

        with open(self.output_file, 'w') as f:
            json.dump(output_data, f, indent=2)

        print(f"\nResults saved to: {self.output_file}")

    def generate_performance_figures(self):
        """Generate performance comparison figures"""
        if not self.results:
            print("No results to plot")
            return

        schedulers = list(self.results.keys())

        # Create figures for key metrics
        self._plot_throughput_comparison(schedulers)
        self._plot_latency_comparison(schedulers)
        self._plot_normalized_performance(schedulers)

        print("Performance figures saved to results/ directory")

    def _plot_throughput_comparison(self, schedulers):
        """Plot throughput metrics comparison"""
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

        # Request throughput
        req_throughputs = [self.results[s].get('avg_request_throughput', 0) for s in schedulers]
        req_stds = [self.results[s].get('std_request_throughput', 0) for s in schedulers]

        bars1 = ax1.bar(schedulers, req_throughputs, yerr=req_stds, capsize=5)
        for bar, val in zip(bars1, req_throughputs):
            height = bar.get_height()
            ax1.text(bar.get_x() + bar.get_width()/2., height,
                    f'{val:.2f}', ha='center', va='bottom')

        ax1.set_xlabel('Scheduler')
        ax1.set_ylabel('Request Throughput (req/s)')
        ax1.set_title(f'Request Throughput Comparison\n{self.num_prompts} prompts')
        ax1.tick_params(axis='x', rotation=45)

        # Token throughput
        token_throughputs = [self.results[s].get('avg_output_token_throughput', 0) for s in schedulers]
        token_stds = [self.results[s].get('std_output_token_throughput', 0) for s in schedulers]

        bars2 = ax2.bar(schedulers, token_throughputs, yerr=token_stds, capsize=5)
        for bar, val in zip(bars2, token_throughputs):
            height = bar.get_height()
            ax2.text(bar.get_x() + bar.get_width()/2., height,
                    f'{val:.1f}', ha='center', va='bottom')

        ax2.set_xlabel('Scheduler')
        ax2.set_ylabel('Output Token Throughput (tok/s)')
        ax2.set_title(f'Token Throughput Comparison\n{self.num_prompts} prompts')
        ax2.tick_params(axis='x', rotation=45)

        plt.tight_layout()
        plt.savefig('results/vllm_throughput_comparison.png', dpi=300, bbox_inches='tight')
        plt.close()

    def _plot_latency_comparison(self, schedulers):
        """Plot latency metrics comparison"""
        fig, axes = plt.subplots(2, 2, figsize=(16, 12))

        # TTFT (Time to First Token)
        ttft_means = [self.results[s].get('avg_mean_ttft', 0) for s in schedulers]
        ttft_p99s = [self.results[s].get('avg_p99_ttft', 0) for s in schedulers]

        x = np.arange(len(schedulers))
        width = 0.35

        bars1 = axes[0, 0].bar(x - width/2, ttft_means, width, label='Mean')
        bars2 = axes[0, 0].bar(x + width/2, ttft_p99s, width, label='P99')

        axes[0, 0].set_xlabel('Scheduler')
        axes[0, 0].set_ylabel('TTFT (ms)')
        axes[0, 0].set_title('Time to First Token')
        axes[0, 0].set_xticks(x)
        axes[0, 0].set_xticklabels(schedulers, rotation=45, ha='right')
        axes[0, 0].legend()

        # TPOT (Time per Output Token)
        tpot_means = [self.results[s].get('avg_mean_tpot', 0) for s in schedulers]
        tpot_p99s = [self.results[s].get('avg_p99_tpot', 0) for s in schedulers]

        bars1 = axes[0, 1].bar(x - width/2, tpot_means, width, label='Mean')
        bars2 = axes[0, 1].bar(x + width/2, tpot_p99s, width, label='P99')

        axes[0, 1].set_xlabel('Scheduler')
        axes[0, 1].set_ylabel('TPOT (ms)')
        axes[0, 1].set_title('Time per Output Token')
        axes[0, 1].set_xticks(x)
        axes[0, 1].set_xticklabels(schedulers, rotation=45, ha='right')
        axes[0, 1].legend()

        # ITL (Inter-Token Latency)
        itl_means = [self.results[s].get('avg_mean_itl', 0) for s in schedulers]
        itl_p99s = [self.results[s].get('avg_p99_itl', 0) for s in schedulers]

        bars1 = axes[1, 0].bar(x - width/2, itl_means, width, label='Mean')
        bars2 = axes[1, 0].bar(x + width/2, itl_p99s, width, label='P99')

        axes[1, 0].set_xlabel('Scheduler')
        axes[1, 0].set_ylabel('ITL (ms)')
        axes[1, 0].set_title('Inter-Token Latency')
        axes[1, 0].set_xticks(x)
        axes[1, 0].set_xticklabels(schedulers, rotation=45, ha='right')
        axes[1, 0].legend()

        # Benchmark duration
        durations = [self.results[s].get('avg_benchmark_duration', 0) for s in schedulers]
        duration_stds = [self.results[s].get('std_benchmark_duration', 0) for s in schedulers]

        bars = axes[1, 1].bar(schedulers, durations, yerr=duration_stds, capsize=5)
        for bar, val in zip(bars, durations):
            height = bar.get_height()
            axes[1, 1].text(bar.get_x() + bar.get_width()/2., height,
                           f'{val:.1f}s', ha='center', va='bottom')

        axes[1, 1].set_xlabel('Scheduler')
        axes[1, 1].set_ylabel('Duration (seconds)')
        axes[1, 1].set_title('Total Benchmark Duration')
        axes[1, 1].tick_params(axis='x', rotation=45)

        plt.tight_layout()
        plt.savefig('results/vllm_latency_comparison.png', dpi=300, bbox_inches='tight')
        plt.close()

    def _plot_normalized_performance(self, schedulers):
        """Plot normalized performance against baseline"""
        if 'baseline' not in self.results:
            return

        fig, axes = plt.subplots(2, 2, figsize=(16, 12))

        baseline = self.results['baseline']

        # Normalized throughput (higher is better)
        req_baseline = baseline.get('avg_request_throughput', 1)
        req_normalized = [(self.results[s].get('avg_request_throughput', 0) / req_baseline) * 100
                         for s in schedulers]

        bars = axes[0, 0].bar(schedulers, req_normalized)
        axes[0, 0].axhline(y=100, color='r', linestyle='--', label='Baseline')
        for bar, val in zip(bars, req_normalized):
            height = bar.get_height()
            axes[0, 0].text(bar.get_x() + bar.get_width()/2., height,
                           f'{val:.1f}%', ha='center', va='bottom')

        axes[0, 0].set_xlabel('Scheduler')
        axes[0, 0].set_ylabel('Performance (% of baseline)')
        axes[0, 0].set_title('Request Throughput (Normalized)')
        axes[0, 0].tick_params(axis='x', rotation=45)
        axes[0, 0].legend()

        # Normalized token throughput (higher is better)
        token_baseline = baseline.get('avg_output_token_throughput', 1)
        token_normalized = [(self.results[s].get('avg_output_token_throughput', 0) / token_baseline) * 100
                           for s in schedulers]

        bars = axes[0, 1].bar(schedulers, token_normalized)
        axes[0, 1].axhline(y=100, color='r', linestyle='--', label='Baseline')
        for bar, val in zip(bars, token_normalized):
            height = bar.get_height()
            axes[0, 1].text(bar.get_x() + bar.get_width()/2., height,
                           f'{val:.1f}%', ha='center', va='bottom')

        axes[0, 1].set_xlabel('Scheduler')
        axes[0, 1].set_ylabel('Performance (% of baseline)')
        axes[0, 1].set_title('Token Throughput (Normalized)')
        axes[0, 1].tick_params(axis='x', rotation=45)
        axes[0, 1].legend()

        # Normalized TTFT (lower is better, so invert)
        ttft_baseline = baseline.get('avg_mean_ttft', 1)
        ttft_normalized = [(ttft_baseline / self.results[s].get('avg_mean_ttft', 1)) * 100
                          for s in schedulers]

        bars = axes[1, 0].bar(schedulers, ttft_normalized)
        axes[1, 0].axhline(y=100, color='r', linestyle='--', label='Baseline')
        for bar, val in zip(bars, ttft_normalized):
            height = bar.get_height()
            axes[1, 0].text(bar.get_x() + bar.get_width()/2., height,
                           f'{val:.1f}%', ha='center', va='bottom')

        axes[1, 0].set_xlabel('Scheduler')
        axes[1, 0].set_ylabel('Performance (% of baseline, higher=better)')
        axes[1, 0].set_title('Mean TTFT (Normalized, inverted)')
        axes[1, 0].tick_params(axis='x', rotation=45)
        axes[1, 0].legend()

        # Speedup factor (benchmark duration)
        duration_baseline = baseline.get('avg_benchmark_duration', 1)
        speedups = [duration_baseline / self.results[s].get('avg_benchmark_duration', 1)
                   for s in schedulers]

        bars = axes[1, 1].bar(schedulers, speedups)
        axes[1, 1].axhline(y=1.0, color='r', linestyle='--', label='No speedup')
        for bar, val in zip(bars, speedups):
            height = bar.get_height()
            axes[1, 1].text(bar.get_x() + bar.get_width()/2., height,
                           f'{val:.2f}x', ha='center', va='bottom')

        axes[1, 1].set_xlabel('Scheduler')
        axes[1, 1].set_ylabel('Speedup Factor')
        axes[1, 1].set_title('Benchmark Duration Speedup')
        axes[1, 1].tick_params(axis='x', rotation=45)
        axes[1, 1].legend()

        plt.tight_layout()
        plt.savefig('results/vllm_normalized_performance.png', dpi=300, bbox_inches='tight')
        plt.close()

    def generate_summary(self):
        """Generate a text summary of the results"""
        if not self.results:
            return "No results available"

        summary = []
        summary.append("vLLM Benchmark Results Summary")
        summary.append("=" * 60)
        summary.append(f"Prompts: {self.num_prompts}")
        summary.append(f"Runs per scheduler: {self.repeat}")
        summary.append("")

        # Sort by request throughput (higher is better)
        sorted_results = sorted(self.results.items(),
                              key=lambda x: x[1].get('avg_request_throughput', 0),
                              reverse=True)

        summary.append("Performance Ranking (by request throughput):")
        for rank, (scheduler, result) in enumerate(sorted_results, 1):
            req_throughput = result.get('avg_request_throughput', 0)
            req_std = result.get('std_request_throughput', 0)
            token_throughput = result.get('avg_output_token_throughput', 0)
            mean_ttft = result.get('avg_mean_ttft', 0)
            runs = result.get('runs', 0)

            summary.append(f"{rank}. {scheduler}: {req_throughput:.2f} ± {req_std:.2f} req/s ({runs} runs)")
            summary.append(f"   Token throughput: {token_throughput:.1f} tok/s")
            summary.append(f"   Mean TTFT: {mean_ttft:.1f} ms")

            if 'baseline' in self.results and scheduler != 'baseline':
                baseline_throughput = self.results['baseline'].get('avg_request_throughput', 1)
                improvement = ((req_throughput / baseline_throughput) - 1) * 100
                summary.append(f"   vs Baseline: {improvement:+.1f}%")

        return "\n".join(summary)
