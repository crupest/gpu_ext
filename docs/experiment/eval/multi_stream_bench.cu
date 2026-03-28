// Simple multi-stream CUDA benchmark used by the multi-tenant scheduler eval.
//
// It emits one CSV row per kernel launch with the schema expected by
// docs/gpu-ext/eval/multi-tenant-scheduler/kfunc_preempt_test.py.

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct LaunchRecord {
    int stream_id;
    int kernel_id;
    int priority;
    std::string kernel_type;
    double enqueue_time_ms;
    double start_time_ms;
    double end_time_ms;
    double duration_ms;
    double launch_latency_ms;
    double e2e_latency_ms;
    long long host_launch_us;
    long long host_sync_us;
};

struct Options {
    int num_streams = 4;
    int num_kernels = 50;
    std::size_t workload_size = 32u * 1024u * 1024u;
    std::string kernel_type = "compute";
    std::string output_path;
};

inline long long now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

inline double epoch_ms(long long epoch_us, long long base_us) {
    return static_cast<double>(epoch_us - base_us) / 1000.0;
}

void die_usage(const char *prog) {
    std::cerr
        << "Usage: " << prog << " -s STREAMS -k KERNELS -w WORKLOAD -t TYPE -o CSV\n"
        << "  -s STREAMS   Number of CUDA streams per process (default: 4)\n"
        << "  -k KERNELS   Number of kernels per stream (default: 50)\n"
        << "  -w WORKLOAD  Workload size in elements (default: 33554432)\n"
        << "  -t TYPE      Kernel type label in CSV (default: compute)\n"
        << "  -o CSV       Output CSV path\n";
    std::exit(1);
}

void check(cudaError_t err, const char *what) {
    if (err != cudaSuccess) {
        std::cerr << what << ": " << cudaGetErrorString(err) << '\n';
        std::exit(1);
    }
}

__global__ void busy_compute_kernel(float *buf, std::size_t n,
                                    unsigned long long target_cycles,
                                    unsigned int salt) {
    const unsigned int idx = blockIdx.x * blockDim.x + threadIdx.x;
    const float seed = static_cast<float>((idx ^ salt) & 0xFFFFu) * 1e-5f;
    float acc = 1.0f + seed;
    const unsigned long long start = clock64();

    while ((clock64() - start) < target_cycles) {
        acc = fmaf(acc, 1.00000011921f, 0.0009765625f);
        acc = fmaf(acc, 0.99999988079f, 0.125f);
        acc = fmaf(acc, 1.00000023842f, 0.03125f);
        acc = fmaf(acc, 0.99999994039f, 0.0078125f);
    }

    if (idx < n) {
        buf[idx] = acc + buf[idx];
    }
}

Options parse_args(int argc, char **argv) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto need_value = [&](const char *flag) -> const char * {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << '\n';
                die_usage(argv[0]);
            }
            return argv[++i];
        };

        if (arg == "-s") {
            opts.num_streams = std::stoi(need_value("-s"));
        } else if (arg == "-k") {
            opts.num_kernels = std::stoi(need_value("-k"));
        } else if (arg == "-w") {
            opts.workload_size = static_cast<std::size_t>(
                std::stoull(need_value("-w")));
        } else if (arg == "-t") {
            opts.kernel_type = need_value("-t");
        } else if (arg == "-o") {
            opts.output_path = need_value("-o");
        } else if (arg == "-h" || arg == "--help") {
            die_usage(argv[0]);
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            die_usage(argv[0]);
        }
    }

    if (opts.num_streams <= 0 || opts.num_kernels <= 0 ||
        opts.workload_size == 0 || opts.output_path.empty()) {
        die_usage(argv[0]);
    }

    return opts;
}

}  // namespace

int main(int argc, char **argv) {
    const Options opts = parse_args(argc, argv);

    check(cudaSetDevice(0), "cudaSetDevice");
    check(cudaFree(nullptr), "cudaFree");

    int sm_count = 0;
    int clock_rate_khz = 0;
    int least_priority = 0;
    int greatest_priority = 0;
    check(cudaDeviceGetAttribute(&sm_count, cudaDevAttrMultiProcessorCount, 0),
          "cudaDeviceGetAttribute(sm_count)");
    check(cudaDeviceGetAttribute(&clock_rate_khz, cudaDevAttrClockRate, 0),
          "cudaDeviceGetAttribute(clock_rate)");
    check(cudaDeviceGetStreamPriorityRange(&least_priority, &greatest_priority),
          "cudaDeviceGetStreamPriorityRange");

    const int stream_priority =
        (greatest_priority <= 0 && 0 <= least_priority) ? 0 : least_priority;
    constexpr int kBlockSize = 256;
    int active_blocks_per_sm = 0;
    check(cudaOccupancyMaxActiveBlocksPerMultiprocessor(
              &active_blocks_per_sm, busy_compute_kernel, kBlockSize, 0),
          "cudaOccupancyMaxActiveBlocksPerMultiprocessor");
    const int grid_size = std::max(1, sm_count * std::max(1, active_blocks_per_sm));

    const double target_ms =
        std::clamp(80.0 * static_cast<double>(opts.workload_size) /
                       static_cast<double>(32u * 1024u * 1024u),
                   10.0, 200.0);
    // cudaDevAttrClockRate is reported in kHz, which is also cycles/ms.
    const unsigned long long target_cycles =
        static_cast<unsigned long long>(target_ms * static_cast<double>(clock_rate_khz));

    float *d_buf = nullptr;
    check(cudaMalloc(&d_buf, opts.workload_size * sizeof(float)), "cudaMalloc");
    check(cudaMemset(d_buf, 0, opts.workload_size * sizeof(float)), "cudaMemset");

    std::vector<cudaStream_t> streams(opts.num_streams);
    for (auto &stream : streams) {
        check(cudaStreamCreateWithPriority(&stream, cudaStreamNonBlocking, stream_priority),
              "cudaStreamCreateWithPriority");
    }

    // Warm the CUDA runtime and driver path before collecting timing data.
    busy_compute_kernel<<<grid_size, kBlockSize>>>(d_buf, opts.workload_size,
                                                   target_cycles / 20, 0x1234u);
    check(cudaGetLastError(), "warmup launch");
    check(cudaDeviceSynchronize(), "warmup sync");

    const int total_launches = opts.num_streams * opts.num_kernels;
    std::vector<cudaEvent_t> start_events(total_launches);
    std::vector<cudaEvent_t> end_events(total_launches);
    for (int i = 0; i < total_launches; ++i) {
        check(cudaEventCreate(&start_events[i]), "cudaEventCreate(start)");
        check(cudaEventCreate(&end_events[i]), "cudaEventCreate(end)");
    }

    cudaEvent_t base_event;
    check(cudaEventCreate(&base_event), "cudaEventCreate(base)");
    check(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
    check(cudaEventRecord(base_event, nullptr), "cudaEventRecord(base)");
    check(cudaEventSynchronize(base_event), "cudaEventSynchronize(base)");

    const long long host_base_us = now_us();
    std::vector<LaunchRecord> records;
    records.reserve(total_launches);

    for (int kernel_id = 0; kernel_id < opts.num_kernels; ++kernel_id) {
        for (int stream_id = 0; stream_id < opts.num_streams; ++stream_id) {
            const int slot = stream_id * opts.num_kernels + kernel_id;
            const long long host_launch_us = now_us();
            check(cudaEventRecord(start_events[slot], streams[stream_id]),
                  "cudaEventRecord(start)");
            busy_compute_kernel<<<grid_size, kBlockSize, 0, streams[stream_id]>>>(
                d_buf, opts.workload_size, target_cycles,
                static_cast<unsigned int>(slot * 2654435761u));
            check(cudaGetLastError(), "kernel launch");
            check(cudaEventRecord(end_events[slot], streams[stream_id]),
                  "cudaEventRecord(end)");

            LaunchRecord record{};
            record.stream_id = stream_id;
            record.kernel_id = kernel_id;
            record.priority = stream_priority;
            record.kernel_type = opts.kernel_type;
            record.enqueue_time_ms = epoch_ms(host_launch_us, host_base_us);
            record.host_launch_us = host_launch_us;
            records.push_back(record);
        }
    }

    check(cudaDeviceSynchronize(), "cudaDeviceSynchronize(final)");
    const long long host_sync_us = now_us();

    for (auto &record : records) {
        const int slot = record.stream_id * opts.num_kernels + record.kernel_id;
        float start_ms = 0.0f;
        float end_ms = 0.0f;
        check(cudaEventElapsedTime(&start_ms, base_event, start_events[slot]),
              "cudaEventElapsedTime(start)");
        check(cudaEventElapsedTime(&end_ms, base_event, end_events[slot]),
              "cudaEventElapsedTime(end)");
        record.start_time_ms = start_ms;
        record.end_time_ms = end_ms;
        record.duration_ms = record.end_time_ms - record.start_time_ms;
        record.launch_latency_ms = record.start_time_ms - record.enqueue_time_ms;
        record.e2e_latency_ms = record.end_time_ms - record.enqueue_time_ms;
        record.host_sync_us = host_sync_us;
    }

    std::sort(records.begin(), records.end(),
              [](const LaunchRecord &a, const LaunchRecord &b) {
                  if (a.stream_id != b.stream_id) {
                      return a.stream_id < b.stream_id;
                  }
                  return a.kernel_id < b.kernel_id;
              });

    std::ofstream out(opts.output_path);
    if (!out) {
        std::cerr << "Failed to open output file: " << opts.output_path << '\n';
        return 1;
    }

    out << "stream_id,kernel_id,priority,kernel_type,enqueue_time_ms,start_time_ms,"
           "end_time_ms,duration_ms,launch_latency_ms,e2e_latency_ms,host_launch_us,"
           "host_sync_us\n";
    out << std::fixed << std::setprecision(6);
    for (const auto &record : records) {
        out << record.stream_id << ','
            << record.kernel_id << ','
            << record.priority << ','
            << record.kernel_type << ','
            << record.enqueue_time_ms << ','
            << record.start_time_ms << ','
            << record.end_time_ms << ','
            << record.duration_ms << ','
            << record.launch_latency_ms << ','
            << record.e2e_latency_ms << ','
            << record.host_launch_us << ','
            << record.host_sync_us << '\n';
    }

    for (int i = 0; i < total_launches; ++i) {
        cudaEventDestroy(start_events[i]);
        cudaEventDestroy(end_events[i]);
    }
    cudaEventDestroy(base_event);

    for (auto &stream : streams) {
        cudaStreamDestroy(stream);
    }
    cudaFree(d_buf);
    return 0;
}
