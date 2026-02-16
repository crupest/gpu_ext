#!/usr/bin/env python3
"""
ShareGPT evaluation script for llama.cpp server
Tests throughput and latency with real-world conversation patterns
"""

import os
import sys
import json
import time
import requests
import argparse
import subprocess
import asyncio
import aiohttp
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass
from datetime import datetime
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from concurrent.futures import ThreadPoolExecutor, as_completed
from tqdm import tqdm
import platform
import psutil
import socket

# Add the scheduler module to the path (relative to repository root)
from pathlib import Path
repo_root = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(repo_root))
from scheduler import SchedulerRunner, SchedulerBenchmark


@dataclass
class BenchmarkResult:
    """Container for benchmark results"""
    prompt: str
    response: str
    prompt_tokens: int
    response_tokens: int
    ttft_ms: float  # Time to first token
    total_time_ms: float
    tokens_per_second: float
    success: bool
    error: Optional[str] = None


class LlamaServerBenchmark(SchedulerBenchmark):
    """Benchmark llama.cpp server with ShareGPT dataset"""
    
    def __init__(self, server_binary: str, model_path: str, 
                 server_port: int = 8080, 
                 scheduler_runner: SchedulerRunner = None,
                 server_log_file: str = None,
                 n_threads: int = 8,
                 n_parallel: int = 4):
        super().__init__(scheduler_runner)
        
        self.server_binary = server_binary
        self.model_path = model_path
        self.server_port = server_port
        self.server_url = f"http://localhost:{server_port}"
        self.server_process = None
        self.server_log_file = server_log_file
        
        # Server configuration
        self.server_config = {
            "ctx_size": 4096,
            "n_batch": 512,
            "n_threads": n_threads,
            "cont_batching": True,
            "flash_attn": True,
            "n_parallel": n_parallel,  # Number of parallel slots for concurrent requests
        }
    
    def start_server(self, scheduler_name: Optional[str] = None) -> bool:
        """Start llama.cpp server with specified scheduler"""
        cmd = [
            self.server_binary,
            "-m", self.model_path,
            "--port", str(self.server_port),
            "--host", "0.0.0.0",
            "-c", str(self.server_config["ctx_size"]),
            "-b", str(self.server_config["n_batch"]),
            "-t", str(self.server_config["n_threads"]),
            "--parallel", str(self.server_config["n_parallel"]),
        ]
        
        if self.server_config.get("cont_batching"):
            cmd.append("--cont-batching")
        
        if self.server_config.get("flash_attn"):
            cmd.append("--flash-attn")
        
        print(f"Starting llama.cpp server on port {self.server_port}")
        print(f"Command: {' '.join(cmd)}")
        
        try:
            # Setup logging
            stdout = subprocess.PIPE
            stderr = subprocess.PIPE
            if self.server_log_file:
                log_dir = os.path.dirname(self.server_log_file)
                if log_dir:
                    os.makedirs(log_dir, exist_ok=True)
                stdout = open(self.server_log_file + ".stdout", 'w')
                stderr = open(self.server_log_file + ".stderr", 'w')
            
            if scheduler_name and self.runner:
                # Use scheduler runner to start with specific scheduler
                self.server_process = subprocess.Popen(
                    cmd,
                    stdout=stdout,
                    stderr=stderr
                )
                # Set scheduler affinity if needed
                # Note: This is a simplified approach. In a real scenario,
                # you might need to use the actual scheduler API
            else:
                # Start with default scheduler
                self.server_process = subprocess.Popen(
                    cmd,
                    stdout=stdout,
                    stderr=stderr
                )
            
            # Wait for server to be ready
            return self._wait_for_server()
            
        except Exception as e:
            print(f"Failed to start server: {e}")
            return False
    
    def _wait_for_server(self, timeout: int = 30) -> bool:
        """Wait for server to be ready"""
        start_time = time.time()
        
        while time.time() - start_time < timeout:
            try:
                response = requests.get(f"{self.server_url}/health")
                if response.status_code == 200:
                    print("Server is ready!")
                    return True
            except requests.exceptions.ConnectionError:
                pass
            
            time.sleep(1)
        
        print("Server failed to start within timeout")
        return False
    
    def stop_server(self):
        """Stop the llama.cpp server"""
        if self.server_process:
            print("Stopping server...")
            
            # Close log files if they were opened
            if self.server_log_file and self.server_process:
                if hasattr(self.server_process.stdout, 'close'):
                    self.server_process.stdout.close()
                if hasattr(self.server_process.stderr, 'close'):
                    self.server_process.stderr.close()
            
            self.server_process.terminate()
            self.server_process.wait(timeout=10)
            self.server_process = None
    
    async def _send_request_async(self, session: aiohttp.ClientSession, 
                                  prompt: str, max_tokens: int = 200) -> BenchmarkResult:
        """Send async request to server and measure performance"""
        payload = {
            "prompt": prompt,
            "max_tokens": max_tokens,
            "temperature": 0.7,
            "stream": True
        }
        
        start_time = time.time()
        ttft = None
        tokens = []
        
        try:
            async with session.post(f"{self.server_url}/completion", 
                                  json=payload) as response:
                if response.status != 200:
                    error_text = await response.text()
                    return BenchmarkResult(
                        prompt=prompt,
                        response="",
                        prompt_tokens=len(prompt.split()),
                        response_tokens=0,
                        ttft_ms=0,
                        total_time_ms=0,
                        tokens_per_second=0,
                        success=False,
                        error=f"HTTP {response.status}: {error_text}"
                    )
                
                full_response = ""
                async for line in response.content:
                    if line:
                        try:
                            line_str = line.decode('utf-8').strip()
                            if line_str.startswith("data: "):
                                json_str = line_str[6:]
                                if json_str == "[DONE]":
                                    break
                                
                                data = json.loads(json_str)
                                if "content" in data:
                                    if ttft is None:
                                        ttft = (time.time() - start_time) * 1000
                                    
                                    token = data["content"]
                                    tokens.append(token)
                                    full_response += token
                        except:
                            continue
                
                total_time = (time.time() - start_time) * 1000
                num_tokens = len(tokens)
                tps = num_tokens / (total_time / 1000) if total_time > 0 else 0
                
                return BenchmarkResult(
                    prompt=prompt,
                    response=full_response,
                    prompt_tokens=len(prompt.split()),
                    response_tokens=num_tokens,
                    ttft_ms=ttft or 0,
                    total_time_ms=total_time,
                    tokens_per_second=tps,
                    success=True
                )
                
        except Exception as e:
            return BenchmarkResult(
                prompt=prompt,
                response="",
                prompt_tokens=len(prompt.split()),
                response_tokens=0,
                ttft_ms=0,
                total_time_ms=0,
                tokens_per_second=0,
                success=False,
                error=str(e)
            )
    
    def send_request(self, prompt: str, max_tokens: int = 200) -> BenchmarkResult:
        """Send synchronous request to server"""
        payload = {
            "prompt": prompt,
            "max_tokens": max_tokens,
            "temperature": 0.7,
            "stream": False
        }
        
        start_time = time.time()
        
        try:
            response = requests.post(f"{self.server_url}/completion", json=payload)
            total_time = (time.time() - start_time) * 1000
            
            if response.status_code != 200:
                return BenchmarkResult(
                    prompt=prompt,
                    response="",
                    prompt_tokens=len(prompt.split()),
                    response_tokens=0,
                    ttft_ms=0,
                    total_time_ms=total_time,
                    tokens_per_second=0,
                    success=False,
                    error=f"HTTP {response.status_code}"
                )
            
            data = response.json()
            content = data.get("content", "")
            num_tokens = len(content.split())
            tps = num_tokens / (total_time / 1000) if total_time > 0 else 0
            
            return BenchmarkResult(
                prompt=prompt,
                response=content,
                prompt_tokens=len(prompt.split()),
                response_tokens=num_tokens,
                ttft_ms=total_time / 2,  # Approximate for non-streaming
                total_time_ms=total_time,
                tokens_per_second=tps,
                success=True
            )
            
        except Exception as e:
            return BenchmarkResult(
                prompt=prompt,
                response="",
                prompt_tokens=len(prompt.split()),
                response_tokens=0,
                ttft_ms=0,
                total_time_ms=0,
                tokens_per_second=0,
                success=False,
                error=str(e)
            )
    
    async def run_concurrent_benchmark(self, prompts: List[str], 
                                     max_concurrent: int = 10) -> List[BenchmarkResult]:
        """Run concurrent benchmark with multiple prompts"""
        results = []
        
        connector = aiohttp.TCPConnector(limit=max_concurrent)
        async with aiohttp.ClientSession(connector=connector) as session:
            tasks = []
            
            for prompt in prompts:
                task = self._send_request_async(session, prompt)
                tasks.append(task)
            
            # Process with progress bar
            for coro in tqdm(asyncio.as_completed(tasks), 
                           total=len(tasks), 
                           desc="Processing requests"):
                result = await coro
                results.append(result)
        
        # Wait a bit to ensure all responses are fully received
        await asyncio.sleep(1)
        
        return results
    
    def load_sharegpt_dataset(self, dataset_path: str, num_samples: int = 100) -> List[str]:
        """Load ShareGPT dataset and extract prompts"""
        with open(dataset_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        
        prompts = []
        count = 0
        for item in data:
            if count >= num_samples:
                break
                
            if isinstance(item, dict) and 'prompt' in item:
                prompts.append(item['prompt'])
                count += 1
            elif isinstance(item, dict) and 'conversations' in item:
                # ShareGPT Vicuna format
                for conv in item['conversations']:
                    if conv.get('from') in ['human', 'user']:
                        prompts.append(conv['value'])
                        count += 1
                        break
        
        print(f"Loaded {len(prompts)} prompts from ShareGPT dataset")
        return prompts
    
    def analyze_results(self, results: List[BenchmarkResult]) -> Dict:
        """Analyze benchmark results"""
        successful_results = [r for r in results if r.success]
        
        if not successful_results:
            return {"error": "No successful results"}
        
        ttfts = [r.ttft_ms for r in successful_results]
        total_times = [r.total_time_ms for r in successful_results]
        tps_values = [r.tokens_per_second for r in successful_results]
        
        return {
            "total_requests": len(results),
            "successful_requests": len(successful_results),
            "failed_requests": len(results) - len(successful_results),
            "ttft_mean": np.mean(ttfts),
            "ttft_median": np.median(ttfts),
            "ttft_p99": np.percentile(ttfts, 99),
            "total_time_mean": np.mean(total_times),
            "total_time_median": np.median(total_times),
            "total_time_p99": np.percentile(total_times, 99),
            "tokens_per_second_mean": np.mean(tps_values),
            "tokens_per_second_median": np.median(tps_values),
            "total_tokens_generated": sum(r.response_tokens for r in successful_results),
            "avg_response_tokens": np.mean([r.response_tokens for r in successful_results])
        }
    
    def run_benchmark_suite(self, dataset_path: str, 
                          schedulers: List[str] = None,
                          num_samples: int = 100,
                          max_concurrent: int = 10) -> Dict:
        """Run complete benchmark suite across multiple schedulers"""
        prompts = self.load_sharegpt_dataset(dataset_path, num_samples)
        
        if not prompts:
            print("No prompts loaded from dataset")
            return {}
        
        results = {}
        
        # Test default scheduler
        print("\nTesting default scheduler...")
        if self.start_server():
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            
            benchmark_results = loop.run_until_complete(
                self.run_concurrent_benchmark(prompts, max_concurrent)
            )
            
            results["default"] = self.analyze_results(benchmark_results)
            results["default"]["raw_results"] = benchmark_results
            
            # Ensure all requests are processed before stopping
            time.sleep(2)
            self.stop_server()
            time.sleep(5)  # Wait before next test
        
        # Test each scheduler
        if schedulers and self.runner:
            for scheduler in schedulers:
                print(f"\nTesting scheduler: {scheduler}")
                
                if self.start_server(scheduler):
                    loop = asyncio.new_event_loop()
                    asyncio.set_event_loop(loop)
                    
                    benchmark_results = loop.run_until_complete(
                        self.run_concurrent_benchmark(prompts, max_concurrent)
                    )
                    
                    results[scheduler] = self.analyze_results(benchmark_results)
                    results[scheduler]["raw_results"] = benchmark_results
                    
                    # Ensure all requests are processed before stopping
                    time.sleep(2)
                    self.stop_server()
                    time.sleep(5)  # Wait before next test
        
        return results
    
    def get_system_info(self) -> Dict:
        """Collect system information"""
        try:
            # Get CPU info
            cpu_info = {
                "cpu_count": psutil.cpu_count(logical=False),
                "cpu_count_logical": psutil.cpu_count(logical=True),
                "cpu_freq": psutil.cpu_freq().current if psutil.cpu_freq() else "N/A",
                "cpu_percent": psutil.cpu_percent(interval=1)
            }
            
            # Get memory info
            mem = psutil.virtual_memory()
            mem_info = {
                "total_memory_gb": round(mem.total / (1024**3), 2),
                "available_memory_gb": round(mem.available / (1024**3), 2),
                "memory_percent": mem.percent
            }
            
            # Get system info
            uname = platform.uname()
            system_info = {
                "platform": platform.system(),
                "platform_release": platform.release(),
                "platform_version": platform.version(),
                "architecture": platform.machine(),
                "hostname": socket.gethostname(),
                "processor": uname.processor or "N/A",
                "python_version": platform.python_version()
            }
            
            # Get kernel info using uname command
            try:
                kernel_info = subprocess.check_output(['uname', '-a'], text=True).strip()
            except:
                kernel_info = "N/A"
            
            return {
                "system": system_info,
                "cpu": cpu_info,
                "memory": mem_info,
                "kernel": kernel_info,
                "timestamp": datetime.now().isoformat()
            }
        except Exception as e:
            return {"error": str(e), "timestamp": datetime.now().isoformat()}
    
    def generate_report(self, results: Dict, output_dir: str = "results", 
                       dataset_name: str = None, config_info: Dict = None):
        """Generate comprehensive benchmark report"""
        os.makedirs(output_dir, exist_ok=True)
        
        # Get system info
        system_info = self.get_system_info()
        
        # Save raw results with metadata
        results_with_metadata = {
            "system_info": system_info,
            "dataset": dataset_name,
            "config": config_info,
            "results": {}
        }
        
        # Remove raw_results for JSON serialization
        for scheduler, data in results.items():
            results_with_metadata["results"][scheduler] = {k: v for k, v in data.items() if k != "raw_results"}
            
        results_file = os.path.join(output_dir, "sharegpt_llama_server_results.json")
        with open(results_file, 'w') as f:
            json.dump(results_with_metadata, f, indent=2)
        
        # Generate comparison plots
        self._generate_comparison_plots(results, output_dir)
        
        # Generate summary report
        self._generate_summary_report(results, output_dir, system_info, dataset_name, config_info)
    
    def _generate_comparison_plots(self, results: Dict, output_dir: str):
        """Generate comparison plots"""
        schedulers = list(results.keys())
        
        # Extract metrics
        ttft_means = [results[s].get("ttft_mean", 0) for s in schedulers]
        ttft_p99s = [results[s].get("ttft_p99", 0) for s in schedulers]
        tps_means = [results[s].get("tokens_per_second_mean", 0) for s in schedulers]
        success_rates = [
            results[s].get("successful_requests", 0) / results[s].get("total_requests", 1) * 100
            for s in schedulers
        ]
        
        # Create figure
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(15, 12))
        fig.suptitle('ShareGPT Benchmark Results - llama.cpp Server', fontsize=16)
        
        # TTFT comparison
        x = np.arange(len(schedulers))
        width = 0.35
        
        ax1.bar(x - width/2, ttft_means, width, label='Mean', alpha=0.8)
        ax1.bar(x + width/2, ttft_p99s, width, label='P99', alpha=0.8)
        ax1.set_xlabel('Scheduler')
        ax1.set_ylabel('Time to First Token (ms)')
        ax1.set_title('TTFT Comparison')
        ax1.set_xticks(x)
        ax1.set_xticklabels(schedulers, rotation=45)
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # Throughput comparison
        ax2.bar(schedulers, tps_means, color='green', alpha=0.8)
        ax2.set_xlabel('Scheduler')
        ax2.set_ylabel('Tokens per Second')
        ax2.set_title('Throughput Comparison')
        ax2.tick_params(axis='x', rotation=45)
        ax2.grid(True, alpha=0.3)
        
        # Success rate
        ax3.bar(schedulers, success_rates, color='blue', alpha=0.8)
        ax3.set_xlabel('Scheduler')
        ax3.set_ylabel('Success Rate (%)')
        ax3.set_title('Request Success Rate')
        ax3.tick_params(axis='x', rotation=45)
        ax3.grid(True, alpha=0.3)
        ax3.set_ylim(0, 105)
        
        # Combined score (normalized)
        if len(schedulers) > 1:
            # Normalize metrics (higher is better for all)
            norm_ttft = 1 - (np.array(ttft_means) / max(ttft_means))  # Lower is better, so invert
            norm_tps = np.array(tps_means) / max(tps_means) if max(tps_means) > 0 else np.zeros(len(tps_means))
            norm_success = np.array(success_rates) / 100
            
            # Combined score
            combined_score = (norm_ttft * 0.3 + norm_tps * 0.5 + norm_success * 0.2)
            
            bars = ax4.bar(schedulers, combined_score, color='orange', alpha=0.8)
            ax4.set_xlabel('Scheduler')
            ax4.set_ylabel('Combined Performance Score')
            ax4.set_title('Overall Performance Score\n(30% TTFT, 50% Throughput, 20% Success)')
            ax4.tick_params(axis='x', rotation=45)
            ax4.grid(True, alpha=0.3)
            
            # Add value labels
            for bar in bars:
                height = bar.get_height()
                ax4.text(bar.get_x() + bar.get_width()/2., height,
                        f'{height:.2f}',
                        ha='center', va='bottom', fontsize=8)
        
        plt.tight_layout()
        
        # Save figure
        figure_path = os.path.join(output_dir, "sharegpt_llama_server_performance.png")
        plt.savefig(figure_path, dpi=300, bbox_inches='tight')
        print(f"Performance figure saved to {figure_path}")
    
    def _generate_summary_report(self, results: Dict, output_dir: str, 
                                system_info: Dict, dataset_name: str, config_info: Dict):
        """Generate text summary report"""
        report_path = os.path.join(output_dir, "sharegpt_benchmark_summary.txt")
        
        with open(report_path, 'w') as f:
            f.write("ShareGPT Benchmark Results - llama.cpp Server\n")
            f.write("=" * 60 + "\n\n")
            
            # Write system info
            f.write("System Information:\n")
            f.write("-" * 30 + "\n")
            if "error" not in system_info:
                f.write(f"Platform: {system_info['system']['platform']} {system_info['system']['platform_release']}\n")
                f.write(f"Architecture: {system_info['system']['architecture']}\n")
                f.write(f"CPU: {system_info['cpu']['cpu_count']} cores ({system_info['cpu']['cpu_count_logical']} logical)\n")
                f.write(f"Memory: {system_info['memory']['total_memory_gb']} GB total\n")
                f.write(f"Kernel: {system_info['kernel']}\n")
            else:
                f.write(f"Error collecting system info: {system_info['error']}\n")
            
            # Write test config
            f.write("\nTest Configuration:\n")
            f.write("-" * 30 + "\n")
            f.write(f"Dataset: {dataset_name or 'Unknown'}\n")
            if config_info:
                f.write(f"Samples: {config_info.get('num_samples', 'N/A')}\n")
                f.write(f"Max Concurrent: {config_info.get('max_concurrent', 'N/A')}\n")
                f.write(f"Model: {config_info.get('model', 'N/A')}\n")
            f.write(f"Timestamp: {system_info.get('timestamp', 'N/A')}\n")
            f.write("\n" + "=" * 60 + "\n\n")
            
            for scheduler, data in results.items():
                if "error" in data:
                    f.write(f"{scheduler}: ERROR - {data['error']}\n\n")
                    continue
                
                f.write(f"Scheduler: {scheduler}\n")
                f.write("-" * 30 + "\n")
                f.write(f"Total Requests: {data.get('total_requests', 0)}\n")
                f.write(f"Successful: {data.get('successful_requests', 0)}\n")
                f.write(f"Failed: {data.get('failed_requests', 0)}\n")
                f.write(f"\nTTFT (ms):\n")
                f.write(f"  Mean: {data.get('ttft_mean', 0):.2f}\n")
                f.write(f"  Median: {data.get('ttft_median', 0):.2f}\n")
                f.write(f"  P99: {data.get('ttft_p99', 0):.2f}\n")
                f.write(f"\nThroughput:\n")
                f.write(f"  Mean TPS: {data.get('tokens_per_second_mean', 0):.2f}\n")
                f.write(f"  Total Tokens: {data.get('total_tokens_generated', 0)}\n")
                f.write(f"  Avg Response Length: {data.get('avg_response_tokens', 0):.1f} tokens\n")
                f.write("\n" + "=" * 60 + "\n\n")
        
        print(f"Summary report saved to {report_path}")


def main():
    parser = argparse.ArgumentParser(description="Benchmark llama.cpp server with ShareGPT")
    parser.add_argument("--server-binary", 
                       default="build/bin/llama-server",
                       help="Path to llama-server binary")
    parser.add_argument("--model", 
                       default="models/Llama-3.2-3B-Instruct-Q4_K_M.gguf",
                       help="Path to model file")
    parser.add_argument("--dataset", 
                       default=None,
                       help="Path to ShareGPT dataset (default: sharegpt_vicuna.json)")
    parser.add_argument("--port", type=int, default=8080,
                       help="Server port")
    parser.add_argument("--num-samples", type=int, default=100,
                       help="Number of samples to test")
    parser.add_argument("--max-concurrent", type=int, default=10,
                       help="Maximum concurrent requests")
    parser.add_argument("--schedulers", nargs="+",
                       help="List of schedulers to test")
    parser.add_argument("--production-only", action="store_true",
                       help="Test only production schedulers")
    parser.add_argument("--output-dir", default=None,
                       help="Output directory for results (auto-generated if not specified)")
    parser.add_argument("--server-logs", action="store_true",
                       help="Save server logs")
    parser.add_argument("--n-threads", type=int, default=8,
                       help="Number of threads for llama.cpp server (default: 8)")
    parser.add_argument("--n-parallel", type=int, default=4,
                       help="Number of parallel slots for concurrent requests (default: 4)")
    
    args = parser.parse_args()
    
    # Set default dataset if not specified
    if args.dataset is None:
        args.dataset = "datasets/sharegpt_vicuna.json"
        if not os.path.exists(args.dataset):
            # Fallback to benchmark dataset
            args.dataset = "datasets/sharegpt_benchmark.json"
    
    # Extract dataset name from path
    dataset_name = os.path.splitext(os.path.basename(args.dataset))[0]
    
    # Generate output directory name if not specified
    if args.output_dir is None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        args.output_dir = f"results/{dataset_name}_s{args.num_samples}_c{args.max_concurrent}_{timestamp}"
    
    # Check if files exist
    if not os.path.exists(args.server_binary):
        print(f"Error: Server binary not found at {args.server_binary}")
        print("Please build llama.cpp first with: make build")
        sys.exit(1)
    
    if not os.path.exists(args.model):
        print(f"Error: Model file not found at {args.model}")
        sys.exit(1)
    
    if not os.path.exists(args.dataset):
        print(f"Error: Dataset not found at {args.dataset}")
        print("Please run download_sharegpt.py first to download the dataset")
        sys.exit(1)
    
    # Setup server log file if requested
    server_log_file = None
    if args.server_logs:
        server_log_file = os.path.join(args.output_dir, "server_logs")
    
    # Create benchmark instance
    benchmark = LlamaServerBenchmark(
        server_binary=args.server_binary,
        model_path=args.model,
        server_port=args.port,
        server_log_file=server_log_file,
        n_threads=args.n_threads,
        n_parallel=args.n_parallel
    )
    
    # Determine schedulers to test
    schedulers = args.schedulers
    if not schedulers and benchmark.runner:
        schedulers = benchmark.runner.get_available_schedulers(args.production_only)
    
    # Run benchmarks
    print("Starting ShareGPT benchmark suite...")
    results = benchmark.run_benchmark_suite(
        dataset_path=args.dataset,
        schedulers=schedulers,
        num_samples=args.num_samples,
        max_concurrent=args.max_concurrent
    )
    
    # Configuration info for report
    config_info = {
        "num_samples": args.num_samples,
        "max_concurrent": args.max_concurrent,
        "model": os.path.basename(args.model),
        "server_binary": os.path.basename(args.server_binary),
        "schedulers_tested": len(results)
    }
    
    # Generate report
    benchmark.generate_report(results, args.output_dir, dataset_name, config_info)
    
    print("\nBenchmark complete!")


if __name__ == "__main__":
    main()