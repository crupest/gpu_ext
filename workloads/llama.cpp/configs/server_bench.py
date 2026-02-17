#!/usr/bin/env python3
"""llama-server + ShareGPT benchmark atomic runner: single config, single run, JSON output.

Usage:
    uv run python configs/server_bench.py --output results/server_default.json
    uv run python configs/server_bench.py --uvm --output results/server_uvm.json

Starts llama-server, sends ShareGPT prompts via OpenAI-compatible streaming API,
measures TTFT/TPOT/throughput, stops server.

Same script works with or without eBPF kernel module loaded.
"""
import argparse
import asyncio
import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path
from statistics import mean, median

import aiohttp

SCRIPT_DIR = Path(__file__).resolve().parent
WORKLOAD_DIR = SCRIPT_DIR.parent
WORKLOADS_DIR = WORKLOAD_DIR.parent
LLAMA_SERVER = WORKLOAD_DIR / "build" / "bin" / "llama-server"
DEFAULT_MODEL = Path.home() / ".cache/llama.cpp/ggml-org_gpt-oss-20b-GGUF_gpt-oss-20b-mxfp4.gguf"
DATASET_DIR = WORKLOAD_DIR / "datasets"
DATASET_PATH = DATASET_DIR / "sharegpt_vicuna.json"

SERVER_STARTUP_TIMEOUT = 300
SERVER_CHECK_INTERVAL = 5


def cleanup_gpu():
    cleanup_script = WORKLOADS_DIR / "cleanup_gpu.py"
    if cleanup_script.exists():
        subprocess.run([sys.executable, str(cleanup_script)], capture_output=True)
        time.sleep(2)


def load_sharegpt(path: Path, num_prompts: int) -> list[dict]:
    """Load ShareGPT conversations and extract prompt/response pairs."""
    data = json.loads(path.read_text())
    prompts = []
    for conv in data:
        turns = conv.get("conversations", [])
        if len(turns) >= 2:
            human = turns[0]
            assistant = turns[1]
            if human.get("from") in ("human", "user") and assistant.get("from") in ("gpt", "assistant"):
                prompts.append({
                    "prompt": human["value"],
                    "expected_tokens": len(assistant["value"].split()),
                })
        if len(prompts) >= num_prompts:
            break
    return prompts


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


async def benchmark_single(session: aiohttp.ClientSession, base_url: str,
                           model_name: str, prompt: str, max_tokens: int) -> dict:
    """Send one streaming chat completion request, measure TTFT and generation time."""
    payload = {
        "model": model_name,
        "messages": [{"role": "user", "content": prompt}],
        "max_tokens": max_tokens,
        "stream": True,
    }

    t_start = time.monotonic()
    t_first_token = None
    generated_tokens = 0

    try:
        async with session.post(f"{base_url}/v1/chat/completions", json=payload) as resp:
            if resp.status != 200:
                body = await resp.text()
                return {"error": f"HTTP {resp.status}: {body[:200]}"}

            async for line in resp.content:
                decoded = line.decode("utf-8").strip()
                if not decoded.startswith("data: "):
                    continue
                data_str = decoded[6:]
                if data_str == "[DONE]":
                    break
                try:
                    chunk = json.loads(data_str)
                    delta = chunk.get("choices", [{}])[0].get("delta", {})
                    content = delta.get("content", "")
                    if content and t_first_token is None:
                        t_first_token = time.monotonic()
                    if content:
                        generated_tokens += 1
                except json.JSONDecodeError:
                    continue
    except Exception as e:
        return {"error": str(e)}

    t_end = time.monotonic()

    if t_first_token is None:
        return {"error": "no tokens generated"}

    ttft_ms = (t_first_token - t_start) * 1000
    total_s = t_end - t_start
    tpot_ms = ((t_end - t_first_token) * 1000 / max(generated_tokens - 1, 1)) if generated_tokens > 1 else 0.0

    return {
        "ttft_ms": ttft_ms,
        "tpot_ms": tpot_ms,
        "total_s": total_s,
        "generated_tokens": generated_tokens,
    }


async def run_benchmark(base_url: str, model_name: str, prompts: list[dict],
                        max_tokens: int, max_concurrency: int, request_rate: float) -> dict:
    """Run all prompts against the server."""
    connector = aiohttp.TCPConnector(limit=max_concurrency)
    timeout = aiohttp.ClientTimeout(total=600)
    async with aiohttp.ClientSession(connector=connector, timeout=timeout) as session:
        results = []
        sem = asyncio.Semaphore(max_concurrency)

        async def send_one(prompt_data: dict):
            async with sem:
                r = await benchmark_single(session, base_url, model_name,
                                           prompt_data["prompt"], max_tokens)
                results.append(r)

        tasks = []
        for i, p in enumerate(prompts):
            tasks.append(asyncio.create_task(send_one(p)))
            if request_rate > 0 and i < len(prompts) - 1:
                await asyncio.sleep(1.0 / request_rate)

        await asyncio.gather(*tasks)

    # Compute stats from successful results
    ok = [r for r in results if "error" not in r]
    errors = [r for r in results if "error" in r]

    if not ok:
        return {"successful_requests": 0, "failed_requests": len(errors)}

    ttfts = [r["ttft_ms"] for r in ok]
    tpots = [r["tpot_ms"] for r in ok if r["tpot_ms"] > 0]
    total_tokens = sum(r["generated_tokens"] for r in ok)
    total_time = max(r["total_s"] for r in ok)

    def percentile(data, p):
        if not data:
            return 0.0
        sorted_d = sorted(data)
        idx = int(len(sorted_d) * p / 100)
        return sorted_d[min(idx, len(sorted_d) - 1)]

    return {
        "successful_requests": len(ok),
        "failed_requests": len(errors),
        "total_generated_tokens": total_tokens,
        "total_time_s": round(total_time, 2),
        "output_throughput_tok_s": round(total_tokens / total_time, 2) if total_time > 0 else 0,
        "mean_ttft_ms": round(mean(ttfts), 2),
        "median_ttft_ms": round(median(ttfts), 2),
        "p99_ttft_ms": round(percentile(ttfts, 99), 2),
        "mean_tpot_ms": round(mean(tpots), 2) if tpots else 0,
        "median_tpot_ms": round(median(tpots), 2) if tpots else 0,
        "p99_tpot_ms": round(percentile(tpots, 99), 2) if tpots else 0,
    }


def run_server_bench(model: str, uvm: bool, ctx: int, port: int,
                     prompts: int, max_tokens: int, max_concurrency: int,
                     request_rate: float) -> dict:
    """Start server, benchmark, stop server, return result."""
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

        print(f"Server ready. Loading {prompts} ShareGPT prompts...", file=sys.stderr)

        # Download dataset if needed
        if not DATASET_PATH.exists():
            print("Downloading ShareGPT dataset...", file=sys.stderr)
            subprocess.run(
                [sys.executable, str(WORKLOAD_DIR / "download_sharegpt.py"), "--dataset", "vicuna"],
                cwd=str(WORKLOAD_DIR),
            )

        sharegpt_prompts = load_sharegpt(DATASET_PATH, prompts)
        print(f"Loaded {len(sharegpt_prompts)} prompts. Running benchmark...", file=sys.stderr)

        model_name = Path(model).stem
        base_url = f"http://127.0.0.1:{port}"

        start = time.time()
        metrics = asyncio.run(run_benchmark(
            base_url, model_name, sharegpt_prompts,
            max_tokens, max_concurrency, request_rate,
        ))
        elapsed = time.time() - start

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
            "max_tokens": max_tokens,
            "max_concurrency": max_concurrency,
            "request_rate": request_rate,
        },
        "metrics": metrics,
        "timestamp": datetime.now().isoformat(),
        "duration_s": round(elapsed, 2),
    }


def main():
    parser = argparse.ArgumentParser(description="llama-server + ShareGPT benchmark single run")
    parser.add_argument("--model", default=str(DEFAULT_MODEL), help="Model path")
    parser.add_argument("--uvm", action="store_true", help="Enable UVM")
    parser.add_argument("--ctx", type=int, default=65536, help="Context size")
    parser.add_argument("--port", type=int, default=8013, help="Server port")
    parser.add_argument("--prompts", type=int, default=100, help="Number of ShareGPT prompts")
    parser.add_argument("--max-tokens", type=int, default=512, help="Max tokens per response")
    parser.add_argument("--max-concurrency", type=int, default=1, help="Max concurrent requests")
    parser.add_argument("--request-rate", type=float, default=0.0, help="Requests per second (0=no limit)")
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

    if not args.no_cleanup:
        cleanup_gpu()

    result = run_server_bench(
        args.model, args.uvm, args.ctx, args.port,
        args.prompts, args.max_tokens, args.max_concurrency,
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
