
# Benchmark Framework for vLLM, llama.cpp, and Custom Tools

This directory contains a generic benchmarking framework for testing **any benchmark tool** with different Linux schedulers.

## Quick Start

### vLLM Benchmark (Default)

```bash
# Run vLLM benchmark with all schedulers
python3 vllm_bench_start.py --num-prompts 100

# Run with specific schedulers
python3 vllm_bench_start.py --schedulers scx_lavd scx_rusty --num-prompts 50
```

### llama.cpp Benchmark

**Prerequisites**: Start llama.cpp server first (in a separate terminal):

```bash
/path/to/llama-server -m /path/to/model.gguf --port 8080
```

Then run the benchmark:

```bash
# Run llama.cpp benchmark with scheduler testing
python3 vllm_bench_start.py \
  --bench-cmd "python3 llamacpp_openai_client.py --num-prompts 100" \
  --output results/llamacpp_results.json \
  --schedulers scx_lavd scx_rusty
```

### Custom Benchmark Command

You can use any benchmark script that outputs vLLM-compatible metrics:

```bash
python3 vllm_bench_start.py \
  --bench-cmd "python3 /path/to/your/benchmark.py" \
  --output results/custom_results.json
```

## Usage

### Detailed Examples

**vLLM with multiple runs:**
```bash
python3 vllm_bench_start.py \
  --num-prompts 100 \
  --output results/vllm_results.json \
  --repeat 3 \
  --schedulers scx_lavd scx_rusty
```

**llama.cpp with multiple runs:**
```bash
# Make sure llama-server is running first!
python3 vllm_bench_start.py \
  --bench-cmd "python3 llamacpp_openai_client.py --num-prompts 100" \
  --output results/llamacpp_results.json \
  --repeat 3 \
  --schedulers scx_lavd scx_rusty
```

**llama.cpp standalone (no scheduler testing):**
```bash
# Direct run without scheduler framework (activate venv first)
source ~/workspace/.venv/bin/activate && \
  python3 llamacpp_openai_client.py --num-prompts 100 --server-url http://localhost:8080
```

### Requirements for Custom Benchmark Commands

Your benchmark command should output metrics in vLLM-compatible format:

```
Successful requests: 100
Benchmark duration (s): 45.2
Request throughput (req/s): 2.21
Output token throughput (tok/s): 123.4
Mean TTFT (ms): 1234.5
Median TTFT (ms): 1200.0
P99 TTFT (ms): 2000.0
Mean TPOT (ms): 10.5
...
```

The framework will automatically parse these metrics.

## Original vLLM Commands

vllm serve Qwen/Qwen3-Next-80B-A3B-Instruct-FP8  --cpu-offload-gb 30

vllm serve Qwen/Qwen3-Coder-30B-A3B-Instruct-FP8  --cpu-offload-gb 30

source ~/workspace/.venv/bin/activate && vllm bench serve --model Qwen/Qwen3-Coder-30B-A3B-Instruct-FP8 --dataset-name sharegpt --num-prompts 100 --dataset_path /home/yunwei37/workspace/schedcp/workloads/vllm/datasets/ShareGPT_V3_unfiltered_cleaned_split.json

## 30b model

## On off cpu analysis

baseline:

```
First time without cache:
============ Serving Benchmark Result ============
---------------Time to First Token----------------
Mean TTFT (ms):                          26484.33  
Median TTFT (ms):                        26525.51  
P99 TTFT (ms):                           47519.02  
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          1007.14   
Median TPOT (ms):                        671.52    
P99 TPOT (ms):                           4050.65   
---------------Inter-token Latency----------------
Mean ITL (ms):                           607.20    
Median ITL (ms):                         564.65    
P99 ITL (ms):                            4057.08   
==================================================
Second time with cache:
GPU KV cache usage: 5.9%, Prefix cache hit rate: 64.1%
============ Serving Benchmark Result ============
Mean TTFT (ms):                          1968.72   
Median TTFT (ms):                        1985.99   
P99 TTFT (ms):                           1997.20   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          557.48    
Median TPOT (ms):                        572.26    
P99 TPOT (ms):                           584.75    
---------------Inter-token Latency----------------
Mean ITL (ms):                           528.56    
Median ITL (ms):                         565.86    
P99 ITL (ms):                            606.28    
==================================================
```

PCIe GEN 5@16x RX: 53.88 GiB/s TX: 8.039 GiB/s

Off cpu analysis overhead:

```
Mean TTFT (ms):                          2133.11   
Median TTFT (ms):                        2150.46   
P99 TTFT (ms):                           2165.65   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          557.58    
Median TPOT (ms):                        572.58    
P99 TPOT (ms):                           584.97    
---------------Inter-token Latency----------------
Mean ITL (ms):                           529.41    
Median ITL (ms):                         566.26    
P99 ITL (ms):                            605.66   
```

On cpu analysis(freq = 1000hz) overhead:

```
============ Serving Benchmark Result ============
---------------Time to First Token----------------
Mean TTFT (ms):                          2100.81   
Median TTFT (ms):                        2117.80   
P99 TTFT (ms):                           2125.77   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          555.89    
Median TPOT (ms):                        571.05    
P99 TPOT (ms):                           584.30    
---------------Inter-token Latency----------------
Mean ITL (ms):                           526.99    
Median ITL (ms):                         563.93    
P99 ITL (ms):                            601.92    
==================================================
```

Wall clock analysis overhead:

```
Mean TTFT (ms):                          2120.45   
Median TTFT (ms):                        2138.19   
P99 TTFT (ms):                           2146.78   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          556.52    
Median TPOT (ms):                        571.69    
P99 TPOT (ms):                           584.04    
---------------Inter-token Latency----------------
Mean ITL (ms):                           527.54    
Median ITL (ms):                         566.13    
P99 ITL (ms):                            602.84    
==================================================
```

### cupti overhead

```
Mean TTFT (ms):                          1982.50   
Median TTFT (ms):                        1998.57   
P99 TTFT (ms):                           2011.80   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          557.85    
Median TPOT (ms):                        573.27    
P99 TPOT (ms):                           583.86    
---------------Inter-token Latency----------------
Mean ITL (ms):                           529.97    
Median ITL (ms):                         566.91    
P99 ITL (ms):                            603.44    
```

## Run with Nsight systems， graph level

nsys profile --trace=cuda,nvtx,osrt vllm serve Qwen/Qwen3-Coder-30B-A3B-Instruct-FP8  --cpu-offload-gb 30


First time without cache:

```bash
Mean TTFT (ms):                          26869.03  
Median TTFT (ms):                        28862.87  
P99 TTFT (ms):                           48154.56  
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          1013.33   
Median TPOT (ms):                        671.86    
P99 TPOT (ms):                           4106.31   
---------------Inter-token Latency----------------
Mean ITL (ms):                           607.11    
Median ITL (ms):                         562.30    
P99 ITL (ms):                            4105.58   
```
Second time with cache:

```bash   
---------------Time to First Token----------------
Mean TTFT (ms):                          2116.34   
Median TTFT (ms):                        2133.06   
P99 TTFT (ms):                           2147.45   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          557.04    
Median TPOT (ms):                        572.35    
P99 TPOT (ms):                           584.09    
---------------Inter-token Latency----------------
Mean ITL (ms):                           527.81    
Median ITL (ms):                         565.16    
P99 ITL (ms):                            601.60   
```

## Run with Nsight systems， node level

nsys profile --trace=cuda,nvtx,osrt --cuda-graph=node vllm serve Qwen/Qwen3-Coder-30B-A3B-Instruct-FP8  --cpu-offload-gb 30

First time without cache:

```bash
---------------Time to First Token----------------
Mean TTFT (ms):                          26917.42  
Median TTFT (ms):                        24808.93  
P99 TTFT (ms):                           48309.66  
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          1022.32   
Median TPOT (ms):                        674.86    
P99 TPOT (ms):                           4131.08   
---------------Inter-token Latency----------------
Mean ITL (ms):                           609.46    
Median ITL (ms):                         564.78    
P99 ITL (ms):                            4103.13   
```

Second time with cache:

```bash
---------------Time to First Token----------------
Mean TTFT (ms):                          2115.85   
Median TTFT (ms):                        2132.31   
P99 TTFT (ms):                           2147.75   
-----Time per Output Token (excl. 1st token)------
Mean TPOT (ms):                          569.89    
Median TPOT (ms):                        574.45    
P99 TPOT (ms):                           585.72    
---------------Inter-token Latency----------------
Mean ITL (ms):                           564.27    
Median ITL (ms):                         574.86    
P99 ITL (ms):                            607.39   
```
