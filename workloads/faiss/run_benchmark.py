#!/usr/bin/env python3
"""
FAISS Benchmark Runner
Runs CPU and GPU benchmarks with configurable parameters and structured output.
"""

import argparse
import subprocess
import sys
import json
import time
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional

# Benchmark configurations
BENCHMARKS = {
    "cpu": {
        "script": "bench_polysemous_1bn.py",
        "configs": [
            {
                "name": "SIFT10M-IVF4096",
                "dataset": "SIFT10M",
                "index": "IVF4096,Flat",
                "params": ["nprobe=16"],
                "description": "10M vectors, IVF with flat quantization"
            },
            {
                "name": "SIFT100M-IVF4096",
                "dataset": "SIFT100M",
                "index": "IVF4096,Flat",
                "params": ["nprobe=16"],
                "description": "100M vectors, IVF with flat quantization"
            },
            {
                "name": "SIFT10M-IMI-PQ",
                "dataset": "SIFT10M",
                "index": "IMI2x12,PQ16",
                "params": ["nprobe=16"],
                "description": "10M vectors, Multi-Index with Product Quantization"
            }
        ]
    },
    "gpu": {
        "script": "bench_gpu_1bn.py",
        "configs": [
            {
                "name": "SIFT10M-IVF4096",
                "dataset": "SIFT10M",
                "index": "IVF4096,Flat",
                "params": ["-nprobe", "1,4,16,64"],
                "description": "10M vectors, IVF with multiple nprobe values"
            },
            {
                "name": "SIFT100M-IVF4096",
                "dataset": "SIFT100M",
                "index": "IVF4096,Flat",
                "params": ["-nprobe", "1,4,16,64"],
                "description": "100M vectors, IVF with multiple nprobe values"
            }
        ]
    }
}

class BenchmarkRunner:
    def __init__(self, base_dir: Path, venv_dir: Path):
        self.base_dir = base_dir
        self.benchs_dir = base_dir / "faiss" / "benchs"
        self.venv_python = venv_dir / "bin" / "python"
        self.results = []

    def run_benchmark(self, mode: str, config: Dict) -> Dict:
        """Run a single benchmark configuration."""
        print(f"\n{'='*60}")
        print(f"Running {mode.upper()} Benchmark: {config['name']}")
        print(f"{'='*60}")
        print(f"Description: {config['description']}")
        print(f"Dataset: {config['dataset']}")
        print(f"Index: {config['index']}")
        print(f"Parameters: {' '.join(config['params'])}")
        print(f"{'='*60}\n")

        # Build command
        script = self.benchs_dir / BENCHMARKS[mode]["script"]
        cmd = [
            str(self.venv_python),
            str(script),
            config["dataset"],
            config["index"]
        ] + config["params"]

        # Run benchmark
        start_time = time.time()
        try:
            result = subprocess.run(
                cmd,
                cwd=self.benchs_dir,
                capture_output=True,
                text=True,
                timeout=3600  # 1 hour timeout
            )
            elapsed_time = time.time() - start_time

            # Parse output
            success = result.returncode == 0
            # Combine stdout and stderr for parsing (some output may go to stderr)
            output = result.stdout + "\n" + result.stderr

            # Extract metrics from output
            metrics = self._parse_output(output, mode)

            benchmark_result = {
                "name": config["name"],
                "mode": mode,
                "dataset": config["dataset"],
                "index": config["index"],
                "parameters": config["params"],
                "success": success,
                "elapsed_time": elapsed_time,
                "metrics": metrics,
                "timestamp": datetime.now().isoformat(),
                "raw_output": result.stdout if success else result.stderr
            }

            if success:
                print(f"✓ Benchmark completed in {elapsed_time:.1f}s")
                self._print_metrics(metrics)
            else:
                print(f"✗ Benchmark failed after {elapsed_time:.1f}s")
                print(f"Error: {result.stderr[:500]}")

            return benchmark_result

        except subprocess.TimeoutExpired:
            elapsed_time = time.time() - start_time
            print(f"✗ Benchmark timed out after {elapsed_time:.1f}s")
            return {
                "name": config["name"],
                "mode": mode,
                "success": False,
                "elapsed_time": elapsed_time,
                "error": "Timeout",
                "timestamp": datetime.now().isoformat()
            }

    def _parse_output(self, output: str, mode: str) -> Dict:
        """Parse benchmark output to extract metrics."""
        metrics = {}

        for line in output.split('\n'):
            line = line.strip()

            # Parse training time: "train done in 11.712 s"
            if 'train done in' in line:
                try:
                    time_str = line.split('train done in')[1].strip().split()[0]
                    metrics['train_time_s'] = float(time_str)
                except:
                    pass

            # Parse add/indexing time: "Add done in 31.915 s"
            if 'Add done in' in line or 'add done in' in line:
                try:
                    time_str = line.split('done in')[1].strip().split()[0]
                    metrics['add_time_s'] = float(time_str)
                except:
                    pass

            # Parse query results: "nprobe=16  0.9193 0.9193 0.9193    1.031    0.00"
            # Format: nprobe=X  R@1 R@10 R@100  time  %pass
            if 'nprobe=' in line and not line.startswith('#'):
                try:
                    parts = line.split()
                    if len(parts) >= 5:
                        nprobe_str = parts[0]  # "nprobe=16"
                        nprobe = nprobe_str.split('=')[1]
                        r_at_1 = float(parts[1])
                        r_at_10 = float(parts[2])
                        r_at_100 = float(parts[3])
                        query_time = float(parts[4])

                        if 'queries' not in metrics:
                            metrics['queries'] = {}

                        metrics['queries'][f'nprobe_{nprobe}'] = {
                            'R@1': r_at_1,
                            'R@10': r_at_10,
                            'R@100': r_at_100,
                            'query_time_s': query_time
                        }
                except:
                    pass

        return metrics

    def _print_metrics(self, metrics: Dict):
        """Pretty print metrics."""
        if not metrics:
            print("\nNo metrics extracted from output")
            return

        print("\nMetrics:")
        if 'train_time_s' in metrics:
            print(f"  Training time: {metrics['train_time_s']:.2f}s")
        if 'add_time_s' in metrics:
            print(f"  Indexing time: {metrics['add_time_s']:.2f}s")
        if 'queries' in metrics:
            print("  Query performance:")
            for nprobe, data in metrics['queries'].items():
                r1 = data.get('R@1', 0)
                r10 = data.get('R@10', 0)
                r100 = data.get('R@100', 0)
                qtime = data.get('query_time_s', 0)
                print(f"    {nprobe}: R@1={r1:.4f}, R@10={r10:.4f}, R@100={r100:.4f}, time={qtime:.3f}s")

    def run_benchmarks(self, modes: List[str], configs: Optional[List[str]] = None):
        """Run multiple benchmarks."""
        print(f"\n{'='*60}")
        print(f"FAISS Benchmark Suite")
        print(f"{'='*60}")
        print(f"Modes: {', '.join(modes)}")
        print(f"Base directory: {self.base_dir}")
        print(f"Python: {self.venv_python}")
        print(f"{'='*60}\n")

        for mode in modes:
            if mode not in BENCHMARKS:
                print(f"✗ Unknown mode: {mode}")
                continue

            mode_configs = BENCHMARKS[mode]["configs"]

            # Filter by config names if specified
            if configs:
                mode_configs = [c for c in mode_configs if c["name"] in configs]

            for config in mode_configs:
                result = self.run_benchmark(mode, config)
                self.results.append(result)

        return self.results

    def save_results(self, output_file: Path):
        """Save results to JSON file."""
        with open(output_file, 'w') as f:
            json.dump(self.results, f, indent=2)
        print(f"\n✓ Results saved to {output_file}")

    def print_summary(self):
        """Print summary of all results."""
        print(f"\n{'='*60}")
        print(f"Benchmark Summary")
        print(f"{'='*60}\n")

        successful = [r for r in self.results if r.get('success')]
        failed = [r for r in self.results if not r.get('success')]

        print(f"Total: {len(self.results)} benchmarks")
        print(f"Successful: {len(successful)}")
        print(f"Failed: {len(failed)}\n")

        if successful:
            print("Successful benchmarks:")
            for result in successful:
                metrics = result.get('metrics', {})
                elapsed = result.get('elapsed_time', 0)
                print(f"  {result['name']} ({result['mode']})")
                print(f"    Total time: {elapsed:.1f}s")
                if 'train_time' in metrics:
                    print(f"    Train: {metrics['train_time']:.1f}s", end='')
                if 'add_time' in metrics:
                    print(f", Index: {metrics['add_time']:.1f}s", end='')
                print()

        if failed:
            print("\nFailed benchmarks:")
            for result in failed:
                error = result.get('error', 'Unknown error')
                print(f"  {result['name']} ({result['mode']}): {error}")

def list_configs():
    """List all available benchmark configurations."""
    print(f"\n{'='*60}")
    print(f"Available Benchmark Configurations")
    print(f"{'='*60}\n")

    for mode, data in BENCHMARKS.items():
        print(f"{mode.upper()} Benchmarks:")
        for config in data["configs"]:
            print(f"  {config['name']}")
            print(f"    Dataset: {config['dataset']}")
            print(f"    Index: {config['index']}")
            print(f"    Description: {config['description']}")
            print()

def main():
    parser = argparse.ArgumentParser(
        description="FAISS Benchmark Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Run all CPU benchmarks
  python run_benchmark.py --mode cpu

  # Run all GPU benchmarks
  python run_benchmark.py --mode gpu

  # Run both CPU and GPU benchmarks
  python run_benchmark.py --mode cpu gpu

  # Run specific configuration
  python run_benchmark.py --mode cpu --config SIFT10M-IVF4096

  # List all available configurations
  python run_benchmark.py --list

  # Save results to custom file
  python run_benchmark.py --mode gpu --output results.json
        """
    )

    parser.add_argument(
        '--mode',
        nargs='+',
        choices=['cpu', 'gpu'],
        help='Benchmark mode(s) to run'
    )
    parser.add_argument(
        '--config',
        nargs='+',
        help='Specific configuration(s) to run (e.g., SIFT10M-IVF4096)'
    )
    parser.add_argument(
        '--list',
        action='store_true',
        help='List all available configurations'
    )
    parser.add_argument(
        '--output',
        type=Path,
        default=Path('benchmark_results.json'),
        help='Output file for results (default: benchmark_results.json)'
    )
    parser.add_argument(
        '--base-dir',
        type=Path,
        default=Path(__file__).parent,
        help='Base directory (default: current directory)'
    )
    parser.add_argument(
        '--venv',
        type=Path,
        default=Path(__file__).parent / '.venv',
        help='Virtual environment directory (default: .venv)'
    )

    args = parser.parse_args()

    if args.list:
        list_configs()
        return 0

    if not args.mode:
        parser.print_help()
        print("\nError: --mode is required (or use --list)")
        return 1

    # Check virtual environment
    if not args.venv.exists():
        print(f"Error: Virtual environment not found at {args.venv}")
        print("Please create it with: uv venv")
        return 1

    # Run benchmarks
    runner = BenchmarkRunner(args.base_dir, args.venv)

    try:
        runner.run_benchmarks(args.mode, args.config)
        runner.print_summary()
        runner.save_results(args.output)
        return 0
    except KeyboardInterrupt:
        print("\n\nBenchmark interrupted by user")
        runner.print_summary()
        return 1
    except Exception as e:
        print(f"\nError: {e}")
        import traceback
        traceback.print_exc()
        return 1

if __name__ == '__main__':
    sys.exit(main())
