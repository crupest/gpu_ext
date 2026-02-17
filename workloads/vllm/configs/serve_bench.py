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
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
WORKLOAD_DIR = SCRIPT_DIR.parent
WORKLOADS_DIR = WORKLOAD_DIR.parent
sys.path.insert(0, str(WORKLOADS_DIR / "scripts"))
from common import cleanup_gpu, wait_for_server, stop_server, parse_vllm_bench_output

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
        if not wait_for_server(timeout=SERVER_STARTUP_TIMEOUT,
                              check_interval=SERVER_CHECK_INTERVAL, process=server_proc):
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
        metrics = parse_vllm_bench_output(bench_output)

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
