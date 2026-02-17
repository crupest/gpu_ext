#!/usr/bin/env python3
"""llama-server + ShareGPT benchmark atomic runner: single config, single run, JSON output.

Usage:
    uv run python configs/server_bench.py --output results/server_default.json
    uv run python configs/server_bench.py --uvm --output results/server_uvm.json

Starts llama-server, benchmarks with `vllm bench serve` (from vllm workload),
parses output, stops server.

Same script works with or without eBPF kernel module loaded.
"""
import argparse
import json
import os
import re
import signal
import socket
import subprocess
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
WORKLOAD_DIR = SCRIPT_DIR.parent
WORKLOADS_DIR = WORKLOAD_DIR.parent
VLLM_WORKLOAD_DIR = WORKLOADS_DIR / "vllm"
LLAMA_SERVER = WORKLOAD_DIR / "build" / "bin" / "llama-server"
DEFAULT_MODEL = Path.home() / ".cache/llama.cpp/ggml-org_gpt-oss-20b-GGUF_gpt-oss-20b-mxfp4.gguf"
DATASET_DIR = WORKLOAD_DIR / "datasets"
DATASET_PATH = DATASET_DIR / "sharegpt_vicuna.json"
# gpt-oss GGUF has no HF tokenizer; use Qwen (locally cached) for token counting
DEFAULT_TOKENIZER = "Qwen/Qwen3-30B-A3B-FP8"

SERVER_STARTUP_TIMEOUT = 300
SERVER_CHECK_INTERVAL = 5


def cleanup_gpu():
    cleanup_script = WORKLOADS_DIR / "cleanup_gpu.py"
    if cleanup_script.exists():
        subprocess.run([sys.executable, str(cleanup_script)], capture_output=True)
        time.sleep(2)


def wait_for_server(host: str, port: int, timeout: int, process=None) -> bool:
    start = time.time()
    while time.time() - start < timeout:
        if process and process.poll() is not None:
            return False
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2)
            if sock.connect_ex((host, port)) == 0:
                sock.close()
                try:
                    req = urllib.request.Request(f"http://{host}:{port}/health")
                    with urllib.request.urlopen(req, timeout=5) as resp:
                        if resp.status == 200:
                            return True
                except Exception:
                    pass
            else:
                sock.close()
        except Exception:
            pass
        elapsed = int(time.time() - start)
        print(f"  Waiting for server... ({elapsed}s / {timeout}s)", file=sys.stderr)
        time.sleep(SERVER_CHECK_INTERVAL)
    return False


def stop_server(process):
    if process and process.poll() is None:
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            process.wait(timeout=30)
        except Exception:
            try:
                os.killpg(os.getpgid(process.pid), signal.SIGKILL)
                process.wait(timeout=10)
            except Exception:
                pass


def parse_vllm_bench_output(output: str) -> dict:
    """Parse vllm bench serve output into metrics dict."""
    metrics = {}
    patterns = {
        "successful_requests": r"Successful requests:\s+(\d+)",
        "benchmark_duration_s": r"Benchmark duration \(s\):\s+([\d.]+)",
        "total_input_tokens": r"Total input tokens:\s+(\d+)",
        "total_generated_tokens": r"Total generated tokens:\s+(\d+)",
        "request_throughput_rps": r"Request throughput \(req/s\):\s+([\d.]+)",
        "output_throughput_tok_s": r"Output token throughput \(tok/s\):\s+([\d.]+)",
        "mean_ttft_ms": r"Mean TTFT \(ms\):\s+([\d.]+)",
        "median_ttft_ms": r"Median TTFT \(ms\):\s+([\d.]+)",
        "p99_ttft_ms": r"P99 TTFT \(ms\):\s+([\d.]+)",
        "mean_tpot_ms": r"Mean TPOT \(ms\):\s+([\d.]+)",
        "median_tpot_ms": r"Median TPOT \(ms\):\s+([\d.]+)",
        "p99_tpot_ms": r"P99 TPOT \(ms\):\s+([\d.]+)",
    }
    for key, pattern in patterns.items():
        match = re.search(pattern, output)
        if match:
            val = match.group(1)
            metrics[key] = float(val) if "." in val else int(val)
    return metrics


def run_server_bench(model: str, uvm: bool, ctx: int, port: int,
                     prompts: int, max_concurrency: int,
                     request_rate: float) -> dict:
    """Start llama-server, run vllm bench serve, stop server, return result."""
    # Build server command
    cmd = [
        str(LLAMA_SERVER),
        "-m", model,
        "-c", str(ctx),
        "-ngl", "99",
        "--host", "0.0.0.0",
        "--port", str(port),
    ]

    env = os.environ.copy()
    if uvm:
        env["GGML_CUDA_ENABLE_UNIFIED_MEMORY"] = "1"
        env["GGML_CUDA_DISABLE_GRAPHS"] = "1"

    print(f"Starting llama-server (uvm={uvm}, ctx={ctx}, port={port})...", file=sys.stderr)
    server_proc = subprocess.Popen(
        cmd, env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        preexec_fn=os.setsid,
    )

    try:
        if not wait_for_server("127.0.0.1", port, SERVER_STARTUP_TIMEOUT, server_proc):
            print("ERROR: Server failed to start", file=sys.stderr)
            stop_server(server_proc)
            sys.exit(1)

        print(f"Server ready. Running vllm bench serve ({prompts} prompts)...", file=sys.stderr)

        # Download dataset if needed
        if not DATASET_PATH.exists():
            print("Downloading ShareGPT dataset...", file=sys.stderr)
            subprocess.run(
                [sys.executable, str(WORKLOAD_DIR / "download_sharegpt.py"), "--dataset", "vicuna"],
                cwd=str(WORKLOAD_DIR),
            )

        # Use vllm bench serve from the vllm workload as the benchmark client
        model_name = Path(model).stem
        bench_cmd = (
            f"uv run --directory {VLLM_WORKLOAD_DIR} vllm bench serve "
            f"--model {model_name} "
            f"--tokenizer {DEFAULT_TOKENIZER} "
            f"--dataset-name sharegpt "
            f"--dataset-path {DATASET_PATH} "
            f"--base-url http://127.0.0.1:{port} "
            f"--num-prompts {prompts} "
            f"--max-concurrency {max_concurrency} "
            f"--request-rate {request_rate}"
        )

        start = time.time()
        bench_result = subprocess.run(
            bench_cmd, shell=True,
            capture_output=True, text=True,
        )
        elapsed = time.time() - start

        bench_output = bench_result.stdout + bench_result.stderr

        if bench_result.returncode != 0:
            print(f"vllm bench serve failed (exit {bench_result.returncode}):", file=sys.stderr)
            print(bench_output[-2000:], file=sys.stderr)
            stop_server(server_proc)
            sys.exit(1)

        metrics = parse_vllm_bench_output(bench_output)

    finally:
        print("Stopping server...", file=sys.stderr)
        stop_server(server_proc)
        time.sleep(3)

    config_name = "server_bench"
    if uvm:
        config_name += "_uvm"

    return {
        "workload": "llama.cpp",
        "config": config_name,
        "params": {
            "model": Path(model).name,
            "uvm": uvm,
            "ctx": ctx,
            "prompts": prompts,
            "max_concurrency": max_concurrency,
            "request_rate": request_rate,
        },
        "metrics": metrics,
        "timestamp": datetime.now().isoformat(),
        "duration_s": round(elapsed, 2),
        "raw": {"bench_output": bench_output},
    }


def main():
    parser = argparse.ArgumentParser(description="llama-server + ShareGPT benchmark single run")
    parser.add_argument("--model", default=str(DEFAULT_MODEL), help="Model path")
    parser.add_argument("--uvm", action="store_true", help="Enable UVM")
    parser.add_argument("--ctx", type=int, default=65536, help="Context size")
    parser.add_argument("--port", type=int, default=8013, help="Server port")
    parser.add_argument("--prompts", type=int, default=100, help="Number of ShareGPT prompts")
    parser.add_argument("--max-concurrency", type=int, default=1, help="Max concurrent requests")
    parser.add_argument("--request-rate", type=float, default=0.2, help="Requests per second")
    parser.add_argument("--output", "-o", help="Output JSON path (default: stdout)")
    parser.add_argument("--no-cleanup", action="store_true", help="Skip GPU cleanup")
    args = parser.parse_args()

    if not LLAMA_SERVER.exists():
        print(f"ERROR: llama-server not found at {LLAMA_SERVER}", file=sys.stderr)
        print(f"Run: cd {WORKLOAD_DIR} && make build-cuda-no-vmm", file=sys.stderr)
        sys.exit(1)
    if not Path(args.model).exists():
        print(f"ERROR: Model not found at {args.model}", file=sys.stderr)
        sys.exit(1)
    if not VLLM_WORKLOAD_DIR.exists():
        print(f"ERROR: vllm workload not found at {VLLM_WORKLOAD_DIR}", file=sys.stderr)
        sys.exit(1)

    if not args.no_cleanup:
        cleanup_gpu()

    result = run_server_bench(
        args.model, args.uvm, args.ctx, args.port,
        args.prompts, args.max_concurrency,
        args.request_rate,
    )

    output_json = json.dumps(result, indent=2)
    if args.output:
        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text(output_json)
        print(f"Result written to {args.output}", file=sys.stderr)
    else:
        print(output_json)


if __name__ == "__main__":
    main()
