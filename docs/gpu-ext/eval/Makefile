CUDA_HOME ?= /usr/local/cuda
NVCC ?= $(CUDA_HOME)/bin/nvcc
NVCCFLAGS ?= -O3 -std=c++17

.PHONY: all clean

all: multi_stream_bench

multi_stream_bench: multi_stream_bench.cu
	$(NVCC) $(NVCCFLAGS) $< -o $@

clean:
	rm -f multi_stream_bench
