# gpu_ext Documentation

## Directory Structure

### [`gpu-ext/`](gpu-ext/) — gpu_ext 论文（在审）

GPU driver programmability via eBPF struct_ops. 所有与 gpu_ext 论文相关的文档。

| Subdirectory | Description |
|---|---|
| [`paper/`](gpu-ext/paper/) | LaTeX 论文源文件、图表数据、构建脚本 |
| [`driver_docs/`](gpu-ext/driver_docs/) | NVIDIA UVM 驱动内部分析（架构、LRU、prefetch、scheduling、call graph） |
| [`eval/`](gpu-ext/eval/) | 评估报告（multi-tenant memory/scheduler 实验结果） |
| [`policy/`](gpu-ext/policy/) | 策略推荐矩阵（per-workload 最优 eviction/prefetch 策略） |
| [`profiling/`](gpu-ext/profiling/) | Workload 内存访问模式分析（llama.cpp, faiss, pytorch） |
| [`test-verify/`](gpu-ext/test-verify/) | GPU eBPF 验证分析与 benchmark |
| [`reference/`](gpu-ext/reference/) | 参考资料（eviction 策略总览、chunk trace 格式、related work） |
| [`experiment/`](gpu-ext/experiment/) | 实验记录与脚本架构设计 |
| [`archive/`](gpu-ext/archive/) | 旧版/草稿文档归档 |

### [`xcoord/`](xcoord/) — xCoord 新论文方向

Cross-subsystem CPU-GPU resource coordination via sched_ext + gpu_ext + shared BPF maps.

| File | Description |
|---|---|
| [`xcoord_plan.md`](xcoord/xcoord_plan.md) | ✅ 完整分析与执行计划（零内核修改） |
| [`bpf_core_access_findings.md`](xcoord/bpf_core_access_findings.md) | ✅ BPF CO-RE 发现（无需 kfunc） |
