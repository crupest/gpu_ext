#!/usr/bin/env python3
"""vLLM serve + benchmark atomic runner: single mode, single run, JSON output.

Usage:
    uv run python configs/serve_bench.py --mode cpu_offload --output results/cpu_offload.json
    uv run python configs/serve_bench.py --mode uvm --output results/uvm_baseline.json

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

VLLM_SERVER_DIR = os.environ.get("VLLM_SERVER_DIR", str(Path.home() / "workspace/vllm"))
DATASET_PATH = os.environ.get(
    "DATASET_PATH",
    str(WORKLOAD_DIR / "datasets" / "ShareGPT_V3_unfiltered_cleaned_split.json"),
)
MODEL = "Qwen/Qwen3-30B-A3B-FP8"

# Mode configurations
MODE_CONFIGS = {
    "cpu_offload": {
        "server_cmd": f"uv run vllm serve {MODEL} --enforce-eager --cpu-offload-gb 8",
        "env": {},
    },
    "uvm": {
        "server_cmd": f"uv run vllm serve {MODEL} --enforce-eager --max-num-seqs 16",
        "env": {"VLLM_USE_UVM": "1"},
    },
}

SERVER_STARTUP_TIMEOUT = 600
SERVER_CHECK_INTERVAL = 5


def cleanup_gpu():
    cleanup_script = WORKLOADS_DIR / "cleanup_gpu.py"
    if cleanup_script.exists():
        subprocess.run([sys.executable, str(cleanup_script)], capture_output=True)
        time.sleep(2)


def wait_for_server(host="127.0.0.1", port=8000, timeout=SERVER_STARTUP_TIMEOUT, process=None):
    """Wait for vLLM server to be ready."""
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
    """Stop vLLM server."""
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


def parse_benchmark_output(output: str) -> dict:
    """Parse vLLM benchmark output into metrics dict."""
    metrics = {}
    patterns = {
        "successful_requests": r"Successful requests:\s+(\d+)",
        "benchmark_duration_s": r"Benchmark duration \(s\):\s+([\d.]+)",
        "total_input_tokens": r"Total input tokens:\s+(\d+)",
        "total_generated_tokens": r"Total generated tokens:\s+(\d+)",
        "request_throughput_rps": r"Request throughput \(req/s\):\s+([\d.]+)",
        "output_throughput_tok_s": r"Output token throughput \(tok/s\):\s+([\d.]+)",
        "peak_throughput_tok_s": r"Peak output token throughput \(tok/s\):\s+([\d.]+)",
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


def run_serve_bench(mode: str, prompts: int) -> dict:
    """Start server, run benchmark, stop server, return result."""
    config = MODE_CONFIGS[mode]

    # Start server
    env = os.environ.copy()
    env.update(config["env"])

    print(f"Starting vLLM server (mode={mode})...", file=sys.stderr)
    server_proc = subprocess.Popen(
        config["server_cmd"],
        shell=True,
        cwd=VLLM_SERVER_DIR,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        preexec_fn=os.setsid,
    )

    try:
        if not wait_for_server(process=server_proc):
            print("ERROR: Server failed to start", file=sys.stderr)
            stop_server(server_proc)
            sys.exit(1)

        print(f"Server ready. Running benchmark ({prompts} prompts)...", file=sys.stderr)

        # Run benchmark
        bench_cmd = (
            f"uv run vllm bench serve "
            f"--model {MODEL} "
            f"--dataset-name sharegpt "
            f"--dataset-path {DATASET_PATH} "
            f"--num-prompts {prompts}"
        )

        start = time.time()
        bench_result = subprocess.run(
            bench_cmd, shell=True, cwd=str(WORKLOAD_DIR),
            capture_output=True, text=True,
        )
        elapsed = time.time() - start

        if bench_result.returncode != 0:
            print(f"Benchmark failed (exit {bench_result.returncode}):", file=sys.stderr)
            print(bench_result.stderr[-2000:], file=sys.stderr)
            stop_server(server_proc)
            sys.exit(1)

        bench_output = bench_result.stdout + bench_result.stderr
        metrics = parse_benchmark_output(bench_output)

    finally:
        print("Stopping server...", file=sys.stderr)
        stop_server(server_proc)
        time.sleep(3)

    return {
        "workload": "vllm",
        "config": f"serve_{mode}",
        "params": {
            "mode": mode,
            "model": MODEL,
            "prompts": prompts,
        },
        "metrics": metrics,
        "timestamp": datetime.now().isoformat(),
        "duration_s": round(elapsed, 2),
        "raw": {"bench_output": bench_output},
    }


def main():
    parser = argparse.ArgumentParser(description="vLLM serve + benchmark single run")
    parser.add_argument("--mode", default="cpu_offload", choices=list(MODE_CONFIGS.keys()),
                        help="Server mode")
    parser.add_argument("--prompts", type=int, default=100, help="Number of prompts")
    parser.add_argument("--output", "-o", help="Output JSON path (default: stdout)")
    parser.add_argument("--no-cleanup", action="store_true", help="Skip GPU cleanup")
    args = parser.parse_args()

    if not Path(VLLM_SERVER_DIR).exists():
        print(f"ERROR: vLLM not found at {VLLM_SERVER_DIR}", file=sys.stderr)
        sys.exit(1)

    if not args.no_cleanup:
        cleanup_gpu()

    result = run_serve_bench(args.mode, args.prompts)

    output_json = json.dumps(result, indent=2)
    if args.output:
        Path(args.output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.output).write_text(output_json)
        print(f"Result written to {args.output}", file=sys.stderr)
    else:
        print(output_json)


if __name__ == "__main__":
    main()
