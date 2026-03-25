#!/usr/bin/env python3
"""
4-mode evaluation for timeslice scheduling and uprobe-triggered TSG preempt.

Modes:
  - native
  - timeslice_only
  - kfunc_only
  - timeslice_kfunc
"""

import argparse
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import pandas as pd

SCRIPT_DIR = Path(__file__).parent
REPO_ROOT = Path("/home/yunwei37/workspace/gpu/gpu_ext")
MULTI_STREAM_DIR = REPO_ROOT / "microbench" / "multi-stream"
BENCH_PATH = MULTI_STREAM_DIR / "multi_stream_bench"
TIMESLICE_TOOL = Path("/home/yunwei37/workspace/gpu/gpu_ext/extension/gpu_sched_set_timeslices")
PREEMPT_TOOL = Path("/home/yunwei37/workspace/gpu/gpu_ext/extension/uprobe_preempt_multi")
CLEANUP_TOOL = Path("/home/yunwei37/workspace/gpu/gpu_ext/extension/cleanup_struct_ops_tool")

BENCH_LC = MULTI_STREAM_DIR / "bench_lc"
BENCH_BE = MULTI_STREAM_DIR / "bench_be"

NUM_STREAMS = 4
NUM_KERNELS = 50
WORKLOAD_SIZE = 200_000_000
KERNEL_TYPE = "compute"

LC_PROCS = 2
BE_PROCS = 4
LC_TRIGGER_NAME = "bench_lc"

LC_TIMESLICE = 1_000_000
BE_TIMESLICE = 200

ALL_MODES = ["native", "timeslice_only", "kfunc_only", "timeslice_kfunc"]


def ensure_paths(modes):
    required = [BENCH_PATH, CLEANUP_TOOL]

    if any(mode in ("timeslice_only", "timeslice_kfunc") for mode in modes):
        required.append(TIMESLICE_TOOL)
    if any(mode in ("kfunc_only", "timeslice_kfunc") for mode in modes):
        required.append(PREEMPT_TOOL)

    missing = [str(path) for path in required if not path.exists()]
    if missing:
        raise FileNotFoundError("Missing required paths:\n  " + "\n  ".join(missing))


def setup_benches():
    for target in (BENCH_LC, BENCH_BE):
        if target.exists():
            target.unlink()
        shutil.copy2(BENCH_PATH, target)
        target.chmod(0o755)


def cleanup_benches():
    for target in (BENCH_LC, BENCH_BE):
        if target.exists():
            target.unlink()


def cleanup_struct_ops():
    if CLEANUP_TOOL.exists():
        subprocess.run(["sudo", str(CLEANUP_TOOL)],
                       capture_output=True, text=True, check=False)


def run_bench(binary, output_file):
    cmd = [
        str(binary),
        "-s", str(NUM_STREAMS),
        "-k", str(NUM_KERNELS),
        "-w", str(WORKLOAD_SIZE),
        "-t", KERNEL_TYPE,
        "-o", str(output_file),
    ]
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def start_tool(cmd, name):
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(1)
    if proc.poll() is not None:
        stdout, stderr = proc.communicate(timeout=1)
        raise RuntimeError(
            f"{name} failed to start\n"
            f"stdout:\n{stdout}\n"
            f"stderr:\n{stderr}"
        )
    return proc


def stop_tool(proc, name):
    if proc is None:
        return "", ""

    if proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            return proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            return proc.communicate()

    return proc.communicate()


def analyze_run(lc_files, be_files):
    lc_lat_us = []
    be_kernels = 0
    be_duration_ms = 0.0

    for path in lc_files:
        if not path.exists():
            raise FileNotFoundError(f"Missing LC CSV: {path}")
        df = pd.read_csv(path)
        lc_lat_us.extend(df["launch_latency_ms"].to_numpy() * 1000.0)

    for path in be_files:
        if not path.exists():
            raise FileNotFoundError(f"Missing BE CSV: {path}")
        df = pd.read_csv(path)
        be_kernels += len(df)
        be_duration_ms += df["duration_ms"].sum()

    if not lc_lat_us:
        raise RuntimeError("No LC latency samples collected")
    if be_duration_ms <= 0:
        raise RuntimeError("Invalid BE duration in CSV output")

    lc_lat_us = np.array(lc_lat_us)

    return {
        "lc_samples": int(len(lc_lat_us)),
        "be_kernels": int(be_kernels),
        "lc_p99_us": float(np.percentile(lc_lat_us, 99)),
        "lc_over_200us": int(np.sum(lc_lat_us > 200.0)),
        "lc_over_1ms": int(np.sum(lc_lat_us > 1000.0)),
        "be_throughput_kps": float(be_kernels / (be_duration_ms / 1000.0)),
    }


def run_experiment(mode, run_id, output_dir):
    lc_files = []
    be_files = []
    bench_procs = []
    timeslice_proc = None
    preempt_proc = None

    cleanup_struct_ops()
    time.sleep(0.5)

    try:
        if mode in ("timeslice_only", "timeslice_kfunc"):
            timeslice_proc = start_tool(
                [
                    "sudo", str(TIMESLICE_TOOL),
                    "-p", f"bench_lc:{LC_TIMESLICE}",
                    "-p", f"bench_be:{BE_TIMESLICE}",
                ],
                "gpu_sched_set_timeslices",
            )

        if mode in ("kfunc_only", "timeslice_kfunc"):
            preempt_proc = start_tool(
                [
                    "sudo", str(PREEMPT_TOOL),
                    "--be-name", "bench_be",
                    "--lc-name", LC_TRIGGER_NAME,
                ],
                "uprobe_preempt_multi",
            )

        for i in range(max(LC_PROCS, BE_PROCS)):
            if i < LC_PROCS:
                path = output_dir / f"{mode}_run{run_id}_lc_{i}.csv"
                lc_files.append(path)
                bench_procs.append((f"lc_{i}", run_bench(BENCH_LC, path)))

            if i < BE_PROCS:
                path = output_dir / f"{mode}_run{run_id}_be_{i}.csv"
                be_files.append(path)
                bench_procs.append((f"be_{i}", run_bench(BENCH_BE, path)))

        for label, proc in bench_procs:
            stdout, stderr = proc.communicate()
            if proc.returncode != 0:
                raise RuntimeError(
                    f"Benchmark {label} failed with code {proc.returncode}\n"
                    f"stdout:\n{stdout}\n"
                    f"stderr:\n{stderr}"
                )

        metrics = analyze_run(lc_files, be_files)

    finally:
        for _, proc in bench_procs:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()

        preempt_stdout, preempt_stderr = stop_tool(preempt_proc, "uprobe_preempt_multi")
        timeslice_stdout, timeslice_stderr = stop_tool(timeslice_proc, "gpu_sched_set_timeslices")
        cleanup_struct_ops()

        if preempt_proc is not None and preempt_stdout:
            for line in preempt_stdout.splitlines():
                if "Final Stats" in line or "Latency Summary" in line or ":" in line:
                    print(f"    [preempt] {line}")
        if preempt_proc is not None and preempt_stderr.strip():
            print(f"    [preempt stderr] {preempt_stderr.strip()}")

        if timeslice_proc is not None and timeslice_stdout:
            for line in timeslice_stdout.splitlines():
                if "policy_hit" in line or "timeslice_mod" in line:
                    print(f"    [timeslice] {line.strip()}")
        if timeslice_proc is not None and timeslice_stderr.strip():
            print(f"    [timeslice stderr] {timeslice_stderr.strip()}")

    return metrics


def summarize_mode(results):
    return {
        "runs": len(results),
        "lc_p99_us_mean": float(np.mean([row["lc_p99_us"] for row in results])),
        "lc_over_200us_mean": float(np.mean([row["lc_over_200us"] for row in results])),
        "lc_over_1ms_mean": float(np.mean([row["lc_over_1ms"] for row in results])),
        "be_throughput_kps_mean": float(np.mean([row["be_throughput_kps"] for row in results])),
    }


def parse_modes(arg):
    modes = [mode.strip() for mode in arg.split(",") if mode.strip()]
    invalid = [mode for mode in modes if mode not in ALL_MODES]
    if invalid:
        raise argparse.ArgumentTypeError(
            f"Invalid modes: {', '.join(invalid)}. Valid modes: {', '.join(ALL_MODES)}"
        )
    return modes or ALL_MODES


def print_summary_table(mode_summary):
    print()
    print("=" * 96)
    print("SUMMARY")
    print("=" * 96)
    print(f"{'Mode':<18} {'Runs':>4} {'LC P99 (us)':>14} {'LC >200us':>12} {'LC >1ms':>10} {'BE Tput (k/s)':>16}")
    print("-" * 96)
    for mode in ALL_MODES:
        if mode not in mode_summary:
            continue
        row = mode_summary[mode]
        print(f"{mode:<18} {row['runs']:>4} {row['lc_p99_us_mean']:>14.1f} "
              f"{row['lc_over_200us_mean']:>12.1f} {row['lc_over_1ms_mean']:>10.1f} "
              f"{row['be_throughput_kps_mean']:>16.2f}")


def main():
    parser = argparse.ArgumentParser(description="Evaluate timeslice and kfunc preempt modes")
    parser.add_argument("--runs", type=int, default=10, help="Runs per mode (default: 10)")
    parser.add_argument(
        "--modes",
        type=parse_modes,
        default=ALL_MODES,
        help="Comma-separated modes (default: native,timeslice_only,kfunc_only,timeslice_kfunc)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=SCRIPT_DIR / "kfunc_preempt_results",
        help="Directory for raw CSVs and summaries",
    )
    args = parser.parse_args()

    output_dir = args.output_dir
    modes = args.modes if isinstance(args.modes, list) else ALL_MODES
    per_run_rows = []
    mode_results = {mode: [] for mode in modes}

    ensure_paths(modes)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 72)
    print("KFUNC PREEMPT EVALUATION")
    print("=" * 72)
    print(f"Modes:      {', '.join(modes)}")
    print(f"Runs/mode:  {args.runs}")
    print(f"Workload:   {LC_PROCS} LC + {BE_PROCS} BE, {NUM_STREAMS} streams, {NUM_KERNELS} kernels, {WORKLOAD_SIZE} elems")
    print(f"Output dir: {output_dir}")
    print()

    setup_benches()

    try:
        for run_id in range(args.runs):
            for mode in modes:
                print(f"[run {run_id:02d}] {mode}")
                metrics = run_experiment(mode, run_id, output_dir)
                metrics["mode"] = mode
                metrics["run"] = run_id
                per_run_rows.append(metrics)
                mode_results[mode].append(metrics)
                print(f"  LC P99={metrics['lc_p99_us']:.1f}us "
                      f"LC>200us={metrics['lc_over_200us']} "
                      f"LC>1ms={metrics['lc_over_1ms']} "
                      f"BE tput={metrics['be_throughput_kps']:.2f} k/s")
                time.sleep(1)
    finally:
        cleanup_struct_ops()
        cleanup_benches()

    per_run_df = pd.DataFrame(per_run_rows)
    per_run_csv = output_dir / "per_run_summary.csv"
    per_run_df.to_csv(per_run_csv, index=False)

    mode_summary_rows = []
    mode_summary = {}
    for mode in modes:
        summary = summarize_mode(mode_results[mode])
        summary["mode"] = mode
        mode_summary_rows.append(summary)
        mode_summary[mode] = summary

    mode_summary_df = pd.DataFrame(mode_summary_rows)
    mode_summary_csv = output_dir / "mode_summary.csv"
    mode_summary_df.to_csv(mode_summary_csv, index=False)

    print_summary_table(mode_summary)
    print()
    print(f"Wrote: {per_run_csv}")
    print(f"Wrote: {mode_summary_csv}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        cleanup_struct_ops()
        cleanup_benches()
        sys.exit(130)
