# gpu_ext 项目状态分析 + 新论文方向：跨 CPU-GPU eBPF 资源协调

## Context

gpu_ext 是一个通过 eBPF struct_ops 扩展 NVIDIA GPU 驱动的系统。当前论文已投稿待审。用户希望在 gpu_ext 基础上产出一篇**全新的**系统顶会论文，方向选择：**跨 CPU-GPU 子系统的统一 eBPF 资源协调**（sched_ext + gpu_ext）。

---

## 一、当前 gpu_ext 状态总结

### 可用资产

| 资产 | 状态 | 可直接复用 |
|------|------|-----------|
| NVIDIA UVM 内核模块 + 5 个 BPF hook | 可用 | 是 — 作为 GPU 侧基础设施 |
| 20 个 eBPF 策略（eviction/prefetch/scheduling） | 部分可用 | 是 — 作为策略库 |
| 4 个 Workload benchmark（llama.cpp/vLLM/PyTorch/FAISS） | 脚本就绪 | 是 — 作为评估基础 |
| GPU 调度 hooks（timeslice/preemption） | 原型 | 需扩展 |
| Device-side eBPF（SIMT verifier） | 主要是设计 | 不直接复用 |

### 关键限制

- ~~API 完整性 2/10~~ → **已解决**: BPF CO-RE 可读取所有 chunk 属性，实际完整性 7/10
- 唯一需要修复: depopulate hook 条件拦截 (5 LOC)
- 实验 ~20% 完成 — 新论文需要全新的实验设计
- GPU 调度仅 timeslice 控制 — 需深化

---

## 二、新论文方向：跨 CPU-GPU eBPF 资源协调

### 2.1 核心洞察（Key Insight）

**GPU workload 性能同时取决于 CPU 和 GPU 两侧的资源管理决策，但现有系统将两者独立管理。**

具体体现：
1. **UVM page fault 处理依赖 CPU**: GPU 触发 page fault → CPU interrupt → CPU worker thread 处理 → PCIe 数据传输。CPU 调度直接影响 fault latency（host-side overhead 是传输时间的 7x）。
2. **GPU kernel launch 依赖 CPU thread**: CPU 线程被降调度 → GPU kernel launch 延迟 → tail latency 增加。
3. **Multi-tenant 竞争跨越 CPU-GPU**: LC inference tenant 的 GPU 内存被 BE training tenant 的 page fault 驱逐，同时两者的 CPU 线程也在争抢 CPU 时间。
4. **PCIe 带宽是共享资源**: CPU prefetch 和 GPU eviction 都用 PCIe，但两侧策略互不知情。

**sched_ext（Linux 6.12+）和 gpu_ext 分别用 BPF struct_ops 管理 CPU 和 GPU，但两者之间没有协调。** 这是一个明确的系统缺口。

### 2.2 论文定位

**Title**: `xCoord: Coordinated CPU-GPU Resource Management via Cross-Subsystem eBPF`

**一句话 claim**: 通过 BPF maps 在 sched_ext（CPU 调度）和 gpu_ext（GPU 内存/调度）之间共享状态，实现跨子系统协调策略，在 multi-tenant GPU 场景下将 LC tail latency 降低 X%，同时 BE throughput 提升 Y%。

**与 gpu_ext 论文的区别**:
- gpu_ext: "GPU driver as programmable OS subsystem"（单子系统可编程性）
- **xCoord**: "Cross-subsystem coordination between CPU scheduler and GPU driver via eBPF"（跨子系统协调）
- gpu_ext 专注 GPU 内部策略；xCoord 专注 CPU↔GPU 策略联动

### 2.3 系统架构

```
┌─────────────────────────────────────────────┐
│              User-space Control Plane         │
│   Policy Loader + Map Configuration + SLO    │
└────────────┬──────────────────┬──────────────┘
             │                  │
    ┌────────▼────────┐ ┌──────▼────────┐
    │   sched_ext BPF  │ │  gpu_ext BPF   │
    │                  │ │                │
    │ select_cpu()     │ │ chunk_activate │
    │ enqueue()        │ │ chunk_used     │
    │ dispatch()       │ │ evict_prepare  │
    │ running()        │ │ prefetch       │
    │ stopping()       │ │ sched_init     │
    └────────┬────────┘ └──────┬────────┘
             │                  │
      ┌──────▼──────────────────▼──────┐
      │        Shared BPF Maps          │
      │  (pinned in /sys/fs/bpf/)       │
      │                                 │
      │  gpu_state_map:                 │
      │    pid → {fault_rate, mem_usage,│
      │           eviction_count,       │
      │           prefetch_pending}     │
      │                                 │
      │  cpu_state_map:                 │
      │    pid → {cpu_priority,         │
      │           is_running,           │
      │           core_id,              │
      │           bandwidth_quota}      │
      │                                 │
      │  coordination_map:              │
      │    pid → {combined_priority,    │
      │           slo_target,           │
      │           tenant_class}         │
      └────────────────────────────────┘
```

### 2.4 协调策略设计

#### 策略 1: GPU-Aware CPU Scheduling

**场景**: Multi-tenant GPU，LC inference + BE training 共存

**机制**: gpu_ext 将 per-PID GPU 状态（fault rate、memory pressure、eviction count）写入 `gpu_state_map`。sched_ext 在 `select_cpu()` / `enqueue()` 中读取该 map，据此调整：
- **Fault handler 优先**: 当 GPU fault rate 高时，提升处理 UVM fault 的 kthread 的 CPU 优先级
- **LC tenant boost**: GPU 正在为 LC tenant 做 prefetch/compute 时，boost 该 tenant 的 CPU 线程（减少 kernel launch delay）
- **BE tenant throttle**: GPU 内存压力大时，降低 BE tenant 的 CPU 调度优先级（减少其 GPU 访问频率，缓解 thrashing）

#### 策略 2: CPU-Aware GPU Memory Management

**场景**: CPU-intensive preprocessing → GPU inference pipeline

**机制**: sched_ext 将 per-PID CPU 状态（is_running、core_id、vruntime）写入 `cpu_state_map`。gpu_ext 在 `evict_prepare()` 中读取：
- **Running process protection**: 如果 process 正在 CPU 上运行（即将 launch GPU kernel），保护其 GPU 内存不被 evict
- **Sleeping process demotion**: 如果 process 已被 CPU deschedule，其 GPU 内存优先 evict
- **NUMA-aware prefetch**: 根据 process 所在 CPU core 的 NUMA domain 选择 prefetch 策略

#### 策略 3: Coordinated Anti-Thrashing

**场景**: 多 tenant 争抢 GPU memory 导致 thrashing

**机制**: 双向协调
- gpu_ext 检测到 tenant A 的 fault rate 异常高 → 写入 `coordination_map`
- sched_ext 读取 → 临时降低 tenant A 的 CPU scheduling 频率
- 效果: 减少 tenant A 的 GPU 访问频率 → 降低 fault rate → 所有 tenant 收益

#### 策略 4: SLO-Driven Resource Partitioning

**场景**: LC tenant 有 latency SLO（如 P99 < 50ms）

**机制**:
- Control plane 设置 SLO target 在 `coordination_map`
- gpu_ext 持续监测 LC tenant 的 page fault latency
- 如果 latency 接近 SLO → 信号传递给 sched_ext → 抢占 BE tenant 的 CPU 时间 + 提升 LC 的 GPU 内存优先级
- 形成闭环: SLO → monitoring → cross-subsystem adjustment → SLO 满足

### 2.5 为什么这是新颖的

| 对比 | 已有工作 | xCoord |
|------|----------|--------|
| sched_ext | 仅 CPU 调度，GPU-unaware | CPU 调度 + GPU 状态感知 |
| gpu_ext | 仅 GPU 策略，CPU-unaware | GPU 策略 + CPU 状态感知 |
| GPREEMPT | 修改 GPU 驱动做 preemption，不协调 CPU | 跨子系统协调 |
| MIG/MPS | 静态 GPU 分区 | 动态、策略驱动的资源共享 |
| Pegasus | Xen hypervisor 级 CPU-GPU co-scheduling | OS 内核级 eBPF 协调（更轻量、更灵活） |

**关键差异**: 没有系统通过 BPF maps 实现 sched_ext ↔ GPU driver 的在线协调。sched_ext 的 GPU awareness 是 roadmap 但无人实现。

---

## 三、技术可行性分析

### 3.1 sched_ext 侧（高可行性）

**API 完备**:
- `select_cpu()`, `enqueue()`, `dispatch()` — 控制 task 放置和优先级
- `running()`, `stopping()` — 感知 task 执行状态
- `BPF_MAP_TYPE_TASK_STORAGE` — per-task 自定义状态
- 支持 pinned BPF maps — 可与 gpu_ext 共享

**已有参考实现**:
- `scx_layered` — 按 task 分类应用不同策略
- `scx_rusty` — hybrid BPF/user-space 负载均衡
- `scx_lavd` — latency-criticality 感知调度

**工程量**: 实现一个 GPU-aware sched_ext scheduler ~500-800 LOC BPF + ~300 LOC userspace

### 3.2 gpu_ext 侧（高可行性，BPF CO-RE 已就绪）

**已有**:
- eviction/prefetch hooks — 可读取 `cpu_state_map` 做决策
- per-PID tracking（`eviction_freq_pid_decay.bpf.c`）— 可写入 `gpu_state_map`
- GPU scheduling hooks（timeslice 控制）— 可配合 sched_ext
- **BPF CO-RE 完整支持** — 可读取 chunk address, VA block, 链表遍历

**需补齐**:
- 写入 shared map 的逻辑 — 在每个 hook 中更新 `gpu_state_map`
- 元数据清理 — 使用 `BPF_MAP_TYPE_LRU_HASH` 或在 `eviction_prepare` 中手动清理

**工程量**:
- 内核修改: **0 LOC** ✅
- BPF 策略修改: ~250 LOC (添加 shared map 写入)

### 3.3 共享状态（高可行性）

**BPF maps pinning**:
```bash
# gpu_ext 策略创建并 pin map
bpftool map create /sys/fs/bpf/gpu_state_map type hash key 4 value 32 entries 1024
# sched_ext scheduler 打开同一 map
int fd = bpf_obj_get("/sys/fs/bpf/gpu_state_map");
```

**一致性**: BPF maps 提供原子读写（per-entry），足够 µs-ms 级策略决策。不需要强一致性。

### 3.4 风险点

| 风险 | 概率 | 缓解 |
|------|------|------|
| sched_ext 性能 overhead 过高 | 低 | sched_ext 已在 Meta 生产部署，hot-path overhead ~100ns |
| 共享 map 更新延迟影响决策 | 中 | GPU 策略以 ms 为单位，map 更新是 µs 级，延迟可接受 |
| GPU 调度 hooks 不够深 | 中 | 论文可聚焦 memory coordination（已有），scheduling 作为 bonus |
| 内核版本要求 (6.12+) | 低 | 当前内核 6.15.11 已支持 sched_ext |
| 工程复杂度超预期 | 中 | 分阶段实现，先做 strategy 1 验证概念 |

---

## 四、评估设计

### 4.1 Research Questions

- **RQ1 (Coordination Benefit)**: 跨子系统协调能否改善 multi-tenant GPU 性能？（vs. 独立策略）
- **RQ2 (Policy Space)**: 哪些协调策略在哪些场景下有效？
- **RQ3 (Overhead)**: 跨子系统通信的开销是多少？
- **RQ4 (Generality)**: 协调框架能否支持多种策略组合？

### 4.2 实验矩阵

**Scenario 1: Multi-Tenant LC+BE** (主要评估)
- Config: llama.cpp inference (LC) + PyTorch GNN training (BE)
- Baselines: (a) no policy, (b) gpu_ext only, (c) sched_ext only, (d) xCoord coordinated
- Metrics: LC P99 TPOT, BE epoch time, PCIe bandwidth utilization

**Scenario 2: UVM Fault Handler Prioritization**
- Config: Single-tenant GPU workload with heavy UVM fault
- Baselines: (a) default CFS, (b) sched_ext no GPU awareness, (c) xCoord with fault-aware CPU scheduling
- Metrics: page fault latency, total throughput

**Scenario 3: Pipeline Workload**
- Config: CPU preprocessing → GPU inference → CPU postprocessing
- Baselines: (a) default, (b) xCoord with pipeline-aware scheduling
- Metrics: end-to-end latency, GPU utilization

**Scenario 4: Anti-Thrashing**
- Config: 3 tenants 共享 GPU，memory heavily oversubscribed
- Baselines: (a) no policy, (b) gpu_ext eviction only, (c) xCoord + CPU throttling
- Metrics: total fault rate, per-tenant throughput fairness

**Scenario 5: SLO-Driven**
- Config: LC with P99 SLO target + BE background
- Baselines: (a) no SLO enforcement, (b) gpu_ext priority only, (c) xCoord SLO feedback loop
- Metrics: SLO violation rate, BE degradation

### 4.3 已有可复用的实验基础设施

- `workloads/llama.cpp/configs/bench.py`, `server_bench.py` — LC workload
- `workloads/pytorch/configs/gnn.py` — BE workload
- `scripts/run_trials.py`, `collect_results.py` — multi-trial aggregation
- `workloads/cleanup_gpu.py` — GPU 清理

---

## 五、执行计划（8-11 周）— 零内核修改！

> **重大简化 v2**:
> 1. BPF CO-RE 已支持所有必需功能（`BPF_CORE_READ(chunk, address)` 等），**无需新增 kfunc**。详见 `bpf_core_access_findings.md`。
> 2. Depopulate hook **根本不存在于代码中** (非文档错误)，元数据清理用 `BPF_MAP_TYPE_LRU_HASH` 或在 `eviction_prepare` 中手动清理。
> 3. **零内核修改** — 全部工作在 BPF 程序和用户空间完成！

~~### Phase 0: Minimal Kernel Fix（已删除）~~

**Phase 0 已证明不需要**:
- ❌ ~~修复 depopulate hook~~ → Hook 不存在，`BPF_MAP_TYPE_LRU_HASH` 自动清理元数据
- ❌ ~~新增 kfunc~~ → BPF CO-RE 已提供所有功能

### Phase 1: GPU-side Shared Map Integration（1-2 周）

**目标**: gpu_ext 策略写入 per-PID GPU state 到 shared map

**工作内容**:
1. 定义 `shared_maps.h` — GPU/CPU state 结构体 (~50 LOC)
2. 创建 `eviction_lfu_xcoord.bpf.c` — LFU + shared map 示例 (~250 LOC)
   - 用 `BPF_CORE_READ(chunk, address)` 作为 map key 追踪频率
   - 在 `chunk_used` 中更新 `gpu_state_map` (fault_rate, mem_usage, eviction_count)
   - Pin map 到 `/sys/fs/bpf/gpu_state_map`
3. 用户空间工具 `read_gpu_state` 验证 map 内容 (~100 LOC)

**Deliverable**: 运行 workload 时能从 `/sys/fs/bpf/gpu_state_map` 读到 per-PID GPU 状态

### Phase 2: sched_ext GPU-aware Scheduler 原型（2-3 周）

1. 基于 `scx_layered` 实现 GPU-aware scheduler (~600 LOC BPF)
2. 实现 `select_cpu()`: 读取 `gpu_state_map`，为 GPU fault handler 选择最优 CPU
3. 实现 `enqueue()`: 根据 GPU fault_rate 调整 task 优先级
   - High fault_rate → boost priority (减少 fault latency)
   - Low fault_rate → normal priority
4. 实现 `running()`/`stopping()`: 写入 `cpu_state_map` (is_running, core_id)
5. 单元测试: 同时运行 gpu_ext 和 sched_ext，验证 map 双向通信

**Deliverable**: 完整的 CPU-GPU 状态双向共享框架

### Phase 3: 协调策略实现（2-3 周）

1. **Strategy 1**: GPU fault-aware CPU scheduling
   - sched_ext 读取 `fault_rate` → boost UVM fault handler kthread 优先级
2. **Strategy 2**: CPU state-aware GPU eviction
   - gpu_ext 读取 `is_running` → 保护正在 CPU 上运行进程的 GPU 内存
3. **Strategy 3**: Coordinated anti-thrashing
   - gpu_ext 检测高 fault_rate → 写入 `coordination_map`
   - sched_ext 读取 → 临时降低该进程 CPU 调度频率
4. **Strategy 4**: SLO-driven resource partitioning
   - 用户空间设置 `slo_target` → 双侧策略联动保障 SLO

**Deliverable**: 4 种协调策略全部实现，可独立开关

### Phase 4: 全面评估（2-3 周）

1. **Scenario 1-5** × 10 trials × geomean
   - Multi-tenant LC+BE, UVM fault handler prioritization, Pipeline, Anti-thrashing, SLO-driven
2. **Ablation study**: no policy vs gpu_ext only vs sched_ext only vs xCoord coordinated
3. **Overhead measurement**:
   - BPF CO-RE read overhead (expected: <10ns per read)
   - Map access latency (expected: <100ns)
   - Scheduling decision overhead
4. **Sensitivity analysis**: 不同 SLO targets, 不同 memory oversubscription ratios

**Deliverable**: 完整实验数据 + 性能对比图表

### Phase 5: 论文撰写（2-3 周）

1. 新论文框架（引用 gpu_ext 但不复用文本）
2. 重点章节:
   - **Motivation**: CPU-GPU 资源管理的耦合问题
   - **Design**: Shared BPF maps 协调协议，BPF CO-RE 优势
   - **Implementation**: 4 种协调策略的具体实现
   - **Evaluation**: 5 scenarios 完整数据，overhead 分析
3. Related work: sched_ext, gpu_ext, Pegasus, MIG/MPS 对比

**Deliverable**: 可投稿论文 draft

### 关键 Milestone（零内核修改版）

| Week | Milestone | Deliverable |
|------|-----------|-------------|
| 1-2 | GPU shared map | LFU + shared map 示例，pin 到 /sys/fs/bpf/ |
| 3-5 | sched_ext 原型 | GPU-aware scheduler 双向通信验证 |
| 6-8 | 协调策略完成 | 4 种策略全部实现，基础功能验证 |
| 9-10 | 评估完成 | 所有 scenario 有完整数据 + overhead 分析 |
| 11 | 论文 draft | 可投稿状态 |

**总工程量**:
- 内核修改: **0 LOC** ✅ (vs 原计划 55 LOC → 5 LOC → 0 LOC)
- BPF 程序: ~850 LOC (gpu_ext policies + sched_ext scheduler)
- 用户空间: ~100 LOC (测试工具)
- **总计**: ~950 LOC (vs 原计划 ~1255 LOC，减少 24%)

---

## 六、与 gpu_ext 论文的关系

| 维度 | gpu_ext（在审） | xCoord（新论文） |
|------|----------------|-----------------|
| 焦点 | GPU driver 可编程性 | CPU-GPU 跨子系统协调 |
| 技术栈 | gpu_ext BPF hooks + device-side eBPF | sched_ext + gpu_ext + shared maps |
| 评估重点 | 单 tenant 性能优化 | Multi-tenant 协调 |
| 创新点 | GPU driver extensibility | Cross-subsystem eBPF coordination |
| 依赖 | 独立 | 引用 gpu_ext 作为 GPU 侧基础设施 |

**关键**: xCoord 将 gpu_ext 作为组件（GPU 侧 hook），加上 sched_ext（CPU 侧 hook）和协调协议（shared BPF maps），构成完整的跨子系统协调系统。两篇论文互不冲突，xCoord 甚至可以 strengthen gpu_ext 的 story（"gpu_ext 不仅能单独使用，还能与 CPU scheduler 协调"）。

---

## 七、目标会议

| 会议 | Deadline 2026 | 匹配度 | 备注 |
|------|-------------|--------|------|
| **OSDI '27** | ~2026.11 | ★★★★★ | 系统顶会，cross-subsystem design 很 OSDI |
| **SOSP '27** | ~2026.04 (偶数年) | ★★★★ | 需看 SOSP 2027 是否在 cycle |
| **EuroSys '27** | ~2026.10 | ★★★★ | 偏欧洲，接受 system building |
| **ATC '27** | ~2027.01 | ★★★ | 偏实用，可以作为 backup |
| **ASPLOS '27** | ~2026.10 | ★★★★ | 偏 architecture，但接受 OS-level 工作 |

建议优先瞄准 **OSDI '27** 或 **EuroSys '27**。

---

## 八、风险与备选

如果 cross-subsystem coordination 的工程量过大或效果不显著:

**Fallback A**: 缩小范围到 "GPU-aware sched_ext scheduler"（单方向：CPU 感知 GPU）
- 工程量减半
- 仍然有 novelty（首个 GPU-aware sched_ext）
- 可投 EuroSys/ATC

**Fallback B**: 转向方向 D (LLM-focused gpu_ext)
- 复用已有 gpu_ext 基础设施
- 专注 MoE + KV-cache 场景
- 可投 MLSys/EuroSys
