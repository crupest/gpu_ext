#!/usr/bin/env python3
import argparse
import sys
import os

# Add the scheduler module to the path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', 'scheduler')))

from vllm_benchmark_tester import VLLMBenchmarkTester


def main():
    parser = argparse.ArgumentParser(description='Run vLLM benchmark with different schedulers')
    parser.add_argument('--num-prompts', type=int, default=100,
                        help='Number of prompts to test (default: 100)')
    parser.add_argument('--dataset-path', type=str,
                        default='datasets/ShareGPT_V3_unfiltered_cleaned_split.json',
                        help='Path to ShareGPT dataset')
    parser.add_argument('--output', type=str, default='results/vllm_results.json',
                        help='Output file for results (default: results/vllm_results.json)')
    parser.add_argument('--skip-baseline', action='store_true',
                        help='Skip baseline test without scheduler')
    parser.add_argument('--schedulers', nargs='+',
                        help='Specific schedulers to test (default: all)')
    parser.add_argument('--repeat', type=int, default=1,
                        help='Number of times to repeat each test (default: 1)')
    parser.add_argument('--bench-cmd', type=str, default=None,
                        help='Custom benchmark command (e.g., for llama.cpp). If not specified, uses vLLM.')

    args = parser.parse_args()

    # Create tester instance
    tester = VLLMBenchmarkTester(
        num_prompts=args.num_prompts,
        dataset_path=args.dataset_path,
        output_file=args.output,
        repeat=args.repeat,
        bench_cmd=args.bench_cmd
    )

    # Run tests
    bench_type = "custom" if args.bench_cmd else "vLLM"
    print(f"Starting {bench_type} benchmark...")
    if args.bench_cmd:
        print(f"Command: {args.bench_cmd}")
    else:
        print(f"Prompts: {args.num_prompts}")
        print(f"Dataset: {args.dataset_path}")
    print(f"Repeat count: {args.repeat}")
    print("-" * 60)

    # Run benchmark tests
    tester.run_all_tests(skip_baseline=args.skip_baseline, specific_schedulers=args.schedulers)

    # Generate performance figures
    print("\nGenerating performance comparison figures...")
    tester.generate_performance_figures()

    # Print summary
    print("\n" + tester.generate_summary())

    print(f"\nResults saved to: {args.output}")
    print(f"Performance figures saved to: results/")


if __name__ == "__main__":
    main()
