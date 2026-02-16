#!/usr/bin/env python3
"""
vLLM benchmark wrapper using vllm bench serve CLI.
Supports scheduler testing and parses benchmark metrics.

NOTE: For multi-scheduler testing with automated result comparison and visualization,
use vllm_bench_start.py instead. This script is for single scheduler runs.
"""

import subprocess
import argparse
import sys
import re
import json
from pathlib import Path

# Add parent directory to path for scheduler runner (relative to repository root)
repo_root = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(repo_root))

try:
    from scheduler.scheduler_runner import SchedulerRunner
except ImportError:
    print("Warning: Could not import scheduler_runner. Running without scheduler switching.")
    SchedulerRunner = None


def parse_benchmark_output(output):
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


def run_benchmark(num_prompts=100, dataset_path=None):
    """Run vLLM benchmark using CLI and parse output."""
    venv_activate = "~/workspace/.venv/bin/activate"

    if dataset_path is None:
        dataset_path = "/home/yunwei37/workspace/schedcp/workloads/vllm/datasets/ShareGPT_V3_unfiltered_cleaned_split.json"

    cmd = f". {venv_activate} && vllm bench serve " \
          f"--model Qwen/Qwen3-Coder-30B-A3B-Instruct-FP8 " \
          f"--dataset-name sharegpt " \
          f"--num-prompts {num_prompts} " \
          f"--dataset-path {dataset_path}"

    print(f"Running command: {cmd}\n")

    result = subprocess.run(
        cmd,
        shell=True,
        executable="/bin/bash",
        capture_output=True,
        text=True
    )

    # Print stdout in real-time style
    print(result.stdout)

    if result.stderr:
        print(result.stderr, file=sys.stderr)

    # Parse metrics from output
    metrics = parse_benchmark_output(result.stdout)

    return result.returncode, metrics


def print_metrics_summary(metrics):
    """Print parsed metrics in a clean format."""
    if not metrics:
        print("\nNo metrics parsed from output.")
        return

    print("\n" + "="*60)
    print("PARSED METRICS SUMMARY")
    print("="*60)

    # Throughput metrics
    print("\nThroughput:")
    if 'request_throughput' in metrics:
        print(f"  Request throughput: {metrics['request_throughput']:.2f} req/s")
    if 'output_token_throughput' in metrics:
        print(f"  Output token throughput: {metrics['output_token_throughput']:.2f} tok/s")
    if 'total_token_throughput' in metrics:
        print(f"  Total token throughput: {metrics['total_token_throughput']:.2f} tok/s")

    # Latency metrics
    print("\nLatency (Time to First Token):")
    if 'mean_ttft' in metrics:
        print(f"  Mean TTFT: {metrics['mean_ttft']:.2f} ms")
    if 'median_ttft' in metrics:
        print(f"  Median TTFT: {metrics['median_ttft']:.2f} ms")
    if 'p99_ttft' in metrics:
        print(f"  P99 TTFT: {metrics['p99_ttft']:.2f} ms")

    print("\nLatency (Time per Output Token):")
    if 'mean_tpot' in metrics:
        print(f"  Mean TPOT: {metrics['mean_tpot']:.2f} ms")
    if 'median_tpot' in metrics:
        print(f"  Median TPOT: {metrics['median_tpot']:.2f} ms")
    if 'p99_tpot' in metrics:
        print(f"  P99 TPOT: {metrics['p99_tpot']:.2f} ms")

    # Summary stats
    print("\nSummary:")
    if 'successful_requests' in metrics:
        print(f"  Successful requests: {metrics['successful_requests']}")
    if 'benchmark_duration' in metrics:
        print(f"  Duration: {metrics['benchmark_duration']:.2f} s")
    if 'total_input_tokens' in metrics:
        print(f"  Total input tokens: {metrics['total_input_tokens']:,}")
    if 'total_generated_tokens' in metrics:
        print(f"  Total generated tokens: {metrics['total_generated_tokens']:,}")

    print("="*60)


def main():
    parser = argparse.ArgumentParser(description="vLLM benchmark wrapper")
    parser.add_argument("--num-prompts", type=int, default=100,
                        help="Number of prompts to test")
    parser.add_argument("--dataset-path", type=str,
                        default="/home/yunwei37/workspace/schedcp/workloads/vllm/datasets/ShareGPT_V3_unfiltered_cleaned_split.json",
                        help="Path to dataset")
    parser.add_argument("--test-scheduler", type=str, default=None,
                        help="Test with a specific scheduler (e.g., scx_lavd)")
    parser.add_argument("--output-json", type=str, default=None,
                        help="Save metrics to JSON file")

    args = parser.parse_args()

    # Setup scheduler if requested
    runner = None
    scheduler_proc = None
    if args.test_scheduler and SchedulerRunner:
        runner = SchedulerRunner()
        print(f"\nSetting up scheduler: {args.test_scheduler}")
        try:
            scheduler_proc = runner.start_scheduler(args.test_scheduler)
            print(f"Successfully started {args.test_scheduler}\n")
        except Exception as e:
            print(f"Failed to start {args.test_scheduler}: {e}")
            return 1

    try:
        # Run benchmark
        returncode, metrics = run_benchmark(
            num_prompts=args.num_prompts,
            dataset_path=args.dataset_path
        )

        # Print parsed metrics
        print_metrics_summary(metrics)

        # Save to JSON if requested
        if args.output_json and metrics:
            output_data = {
                'scheduler': args.test_scheduler,
                'num_prompts': args.num_prompts,
                'metrics': metrics
            }
            with open(args.output_json, 'w') as f:
                json.dump(output_data, f, indent=2)
            print(f"\nMetrics saved to: {args.output_json}")

        return returncode

    finally:
        # Clean up scheduler
        if args.test_scheduler and runner and scheduler_proc:
            print("\nStopping scheduler...")
            runner.stop_scheduler(args.test_scheduler, scheduler_proc)


if __name__ == "__main__":
    sys.exit(main())
