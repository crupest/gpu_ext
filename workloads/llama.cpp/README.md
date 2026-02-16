# LLaMA.cpp Benchmark Testing Documentation

## Test under 5090 platform

/home/yunwei37/workspace/gpu/schedcp/workloads/llama.cpp/build/bin/llama-server --gpt-oss-120b-default -ncmoe 64 -c 65536

GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 /home/yunwei37/workspace/gpu/schedcp/workloads/llama.cpp/build/bin/llama-server --gpt-oss-120b-default -c 65536

$ uv run vllm bench serve --model  Qwen/Qwen3-30B-A3B-FP8 --dataset-name sharegpt --num-prompts  100 --dataset-path /home/yunwei37/workspace/gpu/schedcp/workloads/vllm/datasets/ShareGPT_V3_unfiltered_cleaned_split.json  --base-url http://127.0.0.1:8013  --max-concurrency=1

Need to build with no VMM support

$ GGML_CUDA_DISABLE_GRAPHS=1 GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 /home/yunwei37/workspace/gpu/schedcp/workloads/llama.cpp/build/bin/llama-server --gpt-oss-120b-default -c 65536

In vllm dir, run

uv run /home/yunwei37/workspace/gpu/schedcp/workloads/vllm/llamacpp_openai_client.py

with UVM memory set to CPU first and unset it:

                // SetPreferredLocation(CPU): Pages stay in system RAM, fetched on demand
                advise_err = cudaMemAdvise(*ptr, size, cudaMemAdviseSetPreferredLocation, cudaCpuDeviceId);

```

Running llama-bench with UVM enabled...
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 build/bin/llama-bench \
        -m /home/yunwei37/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf \
        2>&1 | tee results/gpt-oss-120b-uvm-bench.log
ggml_cuda_init: GGML_CUDA_FORCE_MMQ:    no
ggml_cuda_init: GGML_CUDA_FORCE_CUBLAS: no
ggml_cuda_init: found 1 CUDA devices:
  Device 0: NVIDIA GeForce RTX 5090, compute capability 12.0, VMM: yes
| model                          |       size |     params | backend    | ngl |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | --: | --------------: | -------------------: |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           pp512 |        238.48 ± 1.43 |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           tg128 |          7.72 ± 0.01 |

build: 10e97801 (7099)

Benchmark complete! Results saved to: results/gpt-oss-120b-uvm-bench.log
```

UVM set to GPU and other method does not work.

With UVM set to CPU first and then set to access by:

```
ggml_cuda_init: GGML_CUDA_FORCE_MMQ:    no
ggml_cuda_init: GGML_CUDA_FORCE_CUBLAS: no
ggml_cuda_init: found 1 CUDA devices:
  Device 0: NVIDIA GeForce RTX 5090, compute capability 12.0, VMM: yes
| model                          |       size |     params | backend    | ngl |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | --: | --------------: | -------------------: |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           pp512 |        238.45 ± 1.47 |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           tg128 |          7.70 ± 0.01 |

build: 10e97801 (7099)
```


Set CPU first then set to GPU first:

```
ggml_cuda_init: GGML_CUDA_FORCE_CUBLAS: no
ggml_cuda_init: found 1 CUDA devices:
  Device 0: NVIDIA GeForce RTX 5090, compute capability 12.0, VMM: yes
| model                          |       size |     params | backend    | ngl |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | --: | --------------: | -------------------: |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           pp512 |        144.00 ± 1.18 |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           tg128 |         49.31 ± 3.82 |
```


with ncmoe64

```
$ build/bin/llama-bench  -ncmoe 64       -m /home/yunwei37/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf 
ggml_cuda_init: GGML_CUDA_FORCE_MMQ:    no
ggml_cuda_init: GGML_CUDA_FORCE_CUBLAS: no
ggml_cuda_init: found 1 CUDA devices:
  Device 0: NVIDIA GeForce RTX 5090, compute capability 12.0, VMM: yes
| model                          |       size |     params | backend    | ngl |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | --: | --------------: | -------------------: |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           pp512 |        245.63 ± 2.05 |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           tg128 |         16.34 ± 0.03 |

build: 10e97801 (7099)
yunwei37@lab:~/workspace/gpu/schedcp/workloads/llama.cpp$ 
```

with ncmoe32

```
$ build/bin/llama-bench  -ncmoe 32       -m /home/yunwei37/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_g
pt-oss-120b-mxfp4-00001-of-00003.gguf 
ggml_cuda_init: GGML_CUDA_FORCE_MMQ:    no
ggml_cuda_init: GGML_CUDA_FORCE_CUBLAS: no
ggml_cuda_init: found 1 CUDA devices:
  Device 0: NVIDIA GeForce RTX 5090, compute capability 12.0, VMM: yes
| model                          |       size |     params | backend    | ngl |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | --: | --------------: | -------------------: |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           pp512 |        260.14 ± 2.32 |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           tg128 |         18.18 ± 0.05 |

build: 10e97801 (7099)
```

With prefetching

```
 GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 build/bin/llama-bench         -m /home/yunwei37/.cache/llama.cpp/ggml-org_gpt-oss-120b-GGUF_gpt-oss-120b-mxfp4-00001-of-00003.gguf         2>&1 | tee results/gpt-oss-120b-uvm-bench.log
ggml_cuda_init: GGML_CUDA_FORCE_MMQ:    no
ggml_cuda_init: GGML_CUDA_FORCE_CUBLAS: no
ggml_cuda_init: found 1 CUDA devices:
  Device 0: NVIDIA GeForce RTX 5090, compute capability 12.0, VMM: yes
| model                          |       size |     params | backend    | ngl |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | --: | --------------: | -------------------: |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           pp512 |        229.67 ± 1.35 |
| gpt-oss 120B MXFP4 MoE         |  59.02 GiB |   116.83 B | CUDA       |  99 |           tg128 |         86.89 ± 5.22 |

build: 10e97801 (7099)
yunwei37@lab:~/workspace/gpu/schedcp/workloads
```

## on my laptop, test with intel gpu backend


 /home/yunwei37/ai-os/workloads/llama.cpp/build/bin/llama-bench  -m /home/yunwei37/ai-os/workloads/llama.cpp/models/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf -n 1
| model                          |       size |     params | backend    | threads |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | ------: | --------------: | -------------------: |
| llama 1B Q4_K - Medium         | 636.18 MiB |     1.10 B | CPU        |       2 |           pp512 |         58.31 ± 1.75 |
| llama 1B Q4_K - Medium         | 636.18 MiB |     1.10 B | CPU        |       2 |             tg1 |         27.86 ± 1.62 |

build: 5aa1105da (6082)

yunwei37@victoryang00-ASUS-Zenbook-S-14-UX5406SA-UX5406SA:~/ai-os/workloads/llama.cpp/llama.cpp$ /home/yunwei37/ai-os/workloads/llama.cpp/llama.cpp/build/bin/llama-bench -m /home/yunwei37/ai-os/workloads/llama.cpp/models/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf -n 1

| model                          |       size |     params | backend    | ngl |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | --: | --------------: | -------------------: |
get_memory_info: [warning] ext_intel_free_memory is not supported (export/set ZES_ENABLE_SYSMAN=1 to support), use total memory as free memory
get_memory_info: [warning] ext_intel_free_memory is not supported (export/set ZES_ENABLE_SYSMAN=1 to support), use total memory as free memory
get_memory_info: [warning] ext_intel_free_memory is not supported (export/set ZES_ENABLE_SYSMAN=1 to support), use total memory as free memory
| llama 1B Q4_K - Medium         | 636.18 MiB |     1.10 B | SYCL       |  99 |           pp512 |      629.25 ± 288.96 |
| llama 1B Q4_K - Medium         | 636.18 MiB |     1.10 B | SYCL       |  99 |             tg1 |        23.02 ± 12.81 |

build: 5aa1105da (6082)

llama.cpp/build/bin/llama-server  -hf mradermacher/Qwen3-42B-A3B-2507-Thinking-Abliterated-uncensored-TOTAL-RECALL-v2-Medium-MASTER-CODER-i1-GGUF:Q4_K_M

## control

numactl --interleave=3 python /root/yunwei37/ai-os/workloads/llama.cpp/llamacpp_bench_start.py > llama_test.log
