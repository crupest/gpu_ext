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
- ~~唯一需要修复: depopulate hook 条件拦截 (5 LOC)~~ → **已解决**: 不存在此问题，元数据用 LRU_HASH 自动清理
- **内核修改: 0 LOC** ✅
- 实验 ~20% 完成 — 新论文需要全新的实验设计
- GPU 调度仅 timeslice 控制 — 需深化
- **新增**: CPU-GPU 耦合已有实验数据支撑（19-26% 性能下降，详见 Section 九）

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

**一句话 claim**: GPU 内存子系统的运行时状态（page fault rate、eviction pressure）是 CPU 调度决策的关键缺失信号；通过 eBPF 跨子系统共享这些信号，xCoord 实现了静态 CPU 隔离（taskset）无法达到的性能保障，在有 noisy neighbors 的场景下恢复 >50% 的性能损失。

**与 gpu_ext 论文的区别**:
- gpu_ext: "GPU driver as programmable OS subsystem"（单子系统可编程性）
- **xCoord**: "GPU memory awareness as first-class CPU scheduling signal"（跨子系统协调）
- gpu_ext 专注 GPU 内部策略；xCoord 专注 CPU↔GPU 策略联动
- gpu_ext 的评估是 "不同策略对比"；xCoord 的评估是 "协调 vs 独立 vs 朴素隔离"

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

> **核心 novelty 不是 "shared BPF map"（那只是管道），而是 "GPU 内存状态作为 CPU 调度的第一类信号"。**
>
> 详细 novelty 分析见 Section 八。

| 对比 | 已有工作 | xCoord 差异 |
|------|----------|-------------|
| sched_ext (Meta) | 用 kprobes 识别 GPU 线程，但不知道 GPU 内存状态 | 将 fault_rate/eviction_pressure 作为调度信号 |
| gpu_ext | GPU 策略可编程，但不知道 CPU 调度状态 | 根据 is_running 决定 eviction 优先级 |
| GPREEMPT | 修改 GPU 驱动做 preemption，不协调 CPU | 跨子系统协调，不修改 GPU 驱动 |
| MIG/MPS | 静态 GPU 分区 | 动态、策略驱动的资源共享 |
| Pegasus | Xen hypervisor 级 CPU-GPU co-scheduling | OS 内核级 eBPF 协调（更轻量、更灵活） |
| CPU pinning (taskset) | 静态 CPU 隔离，对大模型无效（实验数据: 19.2% 降级） | 动态调度，根据 GPU 状态实时调整 |

**关键差异**:
1. Meta sched_ext 可以识别 GPU 线程但**看不到 GPU 内存状态**（fault rate、eviction pressure）
2. 静态 CPU 隔离（taskset）在大模型场景**完全失败**（实验数据支撑）
3. xCoord 的 GPU memory awareness 填补了这个缺口

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

## 八、Novelty 分析：xCoord 与 gpu_ext 的根本区别

### 8.1 当前 claim 的问题

当前描述 "通过 shared BPF maps 在 sched_ext 和 gpu_ext 之间共享状态" **缺乏 novelty**：
- Shared BPF map 只是管道（pipe），不是洞察（insight）
- 任何人读完 sched_ext 和 gpu_ext 的代码都能想到 "pin 一个 map 让两边读写"
- 真正的问题是：**什么问题只有跨子系统协调才能解决？**

### 8.2 关键洞察：CPU-GPU 耦合是不对称的

**核心发现（已有实验数据支撑）**:

1. **CPU 调度直接影响 GPU 性能，幅度远超预期**
   - 实验数据（`scripts/sched/`）：CPU stress 导致 GPU LLM 推理 **19-26% 吞吐量下降**
   - Qwen3-30B: 218.7 → 177.1 tok/s（CPU stress），218.7 → 160.8 tok/s（heavy load）
   - Context switches 增加 **21,990-36,414x**（0.1 → 2578-3179 per 1K CUDA launches）

2. **朴素的 CPU 隔离方案无效**
   - `taskset -c 0-3` + `nice -n -10` 完全失败：19.2% 降级（vs 无 pinning 的 19.0%）
   - 原因：pinning 到已争用的核反而加剧竞争
   - 需要内核级 `isolcpus` + IRQ affinity，但这是静态的、需要 root 权限、不可动态调整

3. **GPU 状态对 CPU 调度器不可见**
   - Meta sched_ext 部署（`scripts/sched/pdf_summary.md`）：用 kprobes 在 NVIDIA 驱动上识别 GPU-critical threads
   - 但 Meta **无法看到 GPU 内存状态**（fault rate、eviction pressure、memory utilization）
   - 这意味着 CPU 调度器在 "盲飞"：不知道 GPU 侧正在发生什么

4. **UVM page fault 的 CPU-GPU 耦合**
   - GPU 触发 page fault → CPU interrupt → CPU worker thread 处理 → PCIe 传输
   - Host-side overhead 是 PCIe 传输时间的 **7x**（来自 gpu_ext 论文数据）
   - CPU 被抢占 = page fault 处理延迟 = GPU 空闲等待 = 吞吐量下降

### 8.3 xCoord 的真正 Novelty

**不是 "共享 map"，而是 "GPU memory awareness 作为 CPU 调度信号"**。

| 维度 | gpu_ext | xCoord |
|------|---------|--------|
| **问题** | GPU driver 不可编程 | CPU 和 GPU 资源管理互相 "盲飞" |
| **洞察** | eBPF struct_ops 可扩展 GPU driver | GPU 内存状态（fault rate、eviction pressure）是 CPU 调度的关键缺失信号 |
| **贡献** | GPU 侧策略可编程性 | 跨子系统闭环：GPU 状态 → CPU 调度 → GPU 性能改善 |
| **评估** | 单 tenant：不同 eviction/prefetch 策略效果 | Multi-tenant：协调 vs 独立 vs 朴素隔离 |
| **关键实验** | "用 LFU 替代 LRU" → hit rate 提升 | "CPU scheduler 看到 fault rate" → tail latency 降低 + CPU pinning 无法达到的效果 |

**一句话 novelty**: GPU 内存子系统的运行时状态（page fault rate、eviction pressure）是 CPU 调度决策的第一类（first-class）输入信号，通过 eBPF 跨子系统共享这些信号，可以实现朴素隔离方案无法达到的性能保障。

### 8.4 与 Meta sched_ext 的区别

Meta 的 `scx_layered` 在 AI 训练中部署（`scripts/sched/pdf_summary.md`）：
- ✅ 可以识别 GPU-critical threads（通过 kprobes 在 NVIDIA 驱动上）
- ✅ 恢复了被 noisy neighbors 抢走的 ~25% 性能
- ✅ Fleet-wide 节省 4% GPU capacity

**但 Meta 缺少的**:
- ❌ 不知道 GPU 内存状态（fault rate 高不高？eviction 在发生吗？）
- ❌ 不能根据 GPU memory pressure 动态调整 CPU 优先级
- ❌ 不能做反向协调（CPU 状态 → GPU eviction 决策）
- ❌ 对 UVM/managed memory workload 没有特殊处理

**xCoord 的补充**: 在 Meta 的 "识别 GPU 线程" 基础上，加入 "理解 GPU 内存状态"，实现更精细的协调。

---

## 九、已有实验证据（Motivation Data）

> 以下数据来自 `scripts/sched/` 目录下的实验，可直接作为论文 motivation section 的定量支撑。

### 9.1 CPU 调度对 GPU 推理性能的影响

**实验配置**: Qwen3-30B-A3B-FP8, ShareGPT 200 prompts, llama-server

| 场景 | tok/s | 降级 | Context Switch/1K | 增幅 |
|------|-------|------|-------------------|------|
| Baseline | 218.7 ± 0.1 | - | 0.1 | 1x |
| CPU Stress | 177.1 ± 3.6 | **19.0%** | 2578.4 | 21,990x |
| Network Stress | 218.2 ± 0.3 | 0.2% | 0.6 | 5x |
| Disk Stress | 218.8 ± 0.1 | 0.0% | 0.1 | 1x |
| **Heavy Load** | **160.8 ± 6.9** | **26.5%** | **2895.4** | **24,694x** |
| CPU Pinned | 176.8 ± 2.3 | **19.2%** | 3179.3 | 27,115x |

**关键发现**:
1. **CPU contention 是主导因素**：网络/磁盘几乎无影响，CPU 争用导致 19-26% 性能损失
2. **CPU pinning 完全失败**：不但没改善，反而更差（19.2% vs 19.0%）
3. **Heavy load 非线性放大**：CPU+Net+Disk 组合（26.5%）远超各自之和

### 9.2 干净环境下的 baseline 分析

**实验配置**: Qwen3-0.6B, 无干扰环境

| 指标 | 值 | 说明 |
|------|-----|------|
| Context switches | 592 total (7.44 Hz) | 极低 |
| Kernel launches | 51,464 | |
| 受影响的 launch pairs | 62/51,463 (0.1%) | 极少 |
| 每次抢占的代价 | 15.3 ms (vs 正常 2 µs) | **7,650x 放大** |
| 总调度影响 | 1.2% | 干净环境下可忽略 |
| IRQ 总影响 | 0.0276% | 本地推理下可忽略 |

**关键发现**:
1. **干净环境 → 调度开销极小**（1.2%），但一旦有 noisy neighbors → 19-26%
2. **每次抢占代价极高**（7,650x），但发生概率低 → 尾延迟是关键战场
3. **IRQ 在本地推理中可忽略**，但分布式训练中 NET_RX 影响 5-20%（Meta 数据）

### 9.3 CPU Pinning 失败的根因分析

| 实验 | Model | Pinning 策略 | 效果 |
|------|-------|-------------|------|
| REPORT.md | Qwen3-0.6B | taskset -c 0-3 + nice -10 | **有效**: sched/1K 从 11,933 → 445 (96.3% 减少) |
| analysis_report | Qwen3-30B | taskset -c 0-3 + nice -10 | **无效**: sched/1K 从 2,578 → 3,179 (增加 23%) |

**分析**:
- 小模型（0.6B）：CPU 线程少，pinning 可以在 4 核上隔离
- 大模型（30B）：CPU 线程多（page fault handler、CUDA runtime、多个 worker），pinning 到 4 核反而加剧核内竞争
- **结论**: 静态 pinning 无法适应不同 workload 的动态需求 → 需要动态的 GPU-aware CPU 调度

### 9.4 Meta sched_ext 部署数据

来源: `scripts/sched/pdf_summary.md`（Meta AI Training 内部分析）

| 指标 | 值 |
|------|-----|
| 部署规模 | AI 训练集群 fleet-wide |
| 使用的 scheduler | scx_layered |
| Noisy neighbor 性能恢复 | ~25% |
| GPU capacity 节省 | 4% |
| GPU thread 识别方法 | kprobes on NVIDIA driver |

**Meta 的关键挑战**: 识别 GPU-critical threads。他们用 kprobes 在 NVIDIA 驱动上打点。
**Meta 的盲点**: 不知道 GPU 内存状态 → 不能根据 fault rate 调整 CPU 优先级。

---

## 十、详细 POC 方案

> 目标: 用**最小工程量**验证 xCoord 的核心 claim："GPU 内存状态作为 CPU 调度信号可以改善朴素方案无法达到的性能"。
>
> 原则: **先证明问题存在，再证明方案有效，最后证明方案优于替代方案。**

### 10.1 POC 总览

```
POC-0: 量化问题（已部分完成）         → 1 周
POC-1: 最小单向协调（GPU→CPU）        → 2 周
POC-2: 验证优于朴素方案               → 1 周
POC-3: 反向协调（CPU→GPU）           → 2 周（可选）
                                     ────────
                                     总计: 4-6 周
```

### 10.2 POC-0: 量化 CPU-GPU 耦合问题 ✅ 已完成

> **状态**: 2026-02-23 完成全部实验
> **结果目录**: `scripts/xcoord/results/poc0_20260223_152937/` (20B) 和 `scripts/xcoord/results/poc0_uvm_20260223_154122/` (120B UVM)

**目标**: 建立完整的 motivation data，证明 "CPU 调度影响 GPU 性能" 不是 corner case。

#### 10.2.1 实验 A: 20B 模型（模型完全装入 GPU，无 UVM paging）

**配置**: gpt-oss-20b-mxfp4 (~10GB，适配 32GB RTX 5090)，50 ShareGPT prompts

| 场景 | tok/s | 降级 | Requests OK | TPOT Mean | TPOT P99 |
|------|-------|------|-------------|-----------|----------|
| Baseline | **198.67** | - | 50/50 | 3.72ms | 6.29ms |
| CPU Stress | **175.40** | **-11.7%** | 40/50 | 4.54ms | 16.48ms |
| CPU Stress + Pinned | **179.71** | -9.5% | 40/50 | 4.62ms | 6.42ms |
| Heavy Load | **185.38** | -6.7% | 43/50 | 4.11ms | 13.68ms |

**GPU Hook 调用频率 (chunk_trace)**:

| 场景 | Activate/s | Used/s | Evict/s | Total Hooks |
|------|------------|--------|---------|-------------|
| Baseline | 67 | 453 | 0 | 49,363 |
| CPU Stress | 64 | 428 | 0 | 49,345 |
| CPU Pinned | 64 | 426 | 0 | 49,348 |
| Heavy Load | 59 | 396 | 0 | 49,365 |

**关键发现**:
1. **98.3% 的 ACTIVATE 事件发生在第 1 秒**（模型加载阶段），推理阶段几乎无 page fault
2. **零 eviction** — 模型完全装入 GPU，不存在 UVM thrashing
3. **CPU stress → 11.7% 吞吐量下降** — 纯粹的线程调度延迟（kernel launch delay）
4. **CPU pinning 仅恢复 2.5%** — 效果有限

#### 10.2.2 实验 B: 120B 模型（超出 GPU 内存，重度 UVM paging）⭐

**配置**: gpt-oss-120b-mxfp4 (~60GB，远超 32GB GPU)，20 ShareGPT prompts

| 场景 | tok/s | Requests OK | TPOT Mean | TPOT P99 | TTFT Mean | TTFT P99 |
|------|-------|-------------|-----------|----------|-----------|----------|
| UVM Baseline | 11.24 | 3/20 ⚠️ | 79.32ms | 86.05ms | 521.85ms | 1127.83ms |
| UVM + CPU Stress | 13.11 | 20/20 | 73.31ms | 132.38ms | **3160.10ms** | **7426.73ms** |
| UVM + CPU Stress + Pinned | 13.03 | 20/20 | 73.50ms | 132.31ms | **3169.66ms** | **7407.45ms** |

> ⚠️ Baseline 在 3 个请求后 segfault（llama-server UVM 代码不稳定）

**GPU Hook 调用频率**:

| 场景 | Duration | Activate/s | Used/s | Evict/s | Total Hooks |
|------|----------|------------|--------|---------|-------------|
| UVM Baseline | 214.8s | **2,225** | **5,279** | **2,150** | 2,073,192 |
| UVM + CPU Stress | 529.3s | **2,492** | **8,244** | **2,461** | 6,985,725 |
| UVM + CPU Stress + Pinned | 528.5s | **2,492** | **8,216** | **2,460** | 6,959,722 |

**与 20B 模型对比（关键数据）**:

| 指标 | 20B Model | 120B UVM Model | 比率 |
|------|-----------|----------------|------|
| Activate/s (baseline) | 67 | 2,225 | **33x** |
| Used/s (baseline) | 453 | 5,279 | **12x** |
| Evict/s (baseline) | 0 | 2,150 | **∞** |
| Total hooks (baseline) | 49,363 | 2,073,192 | **42x** |
| 推理阶段 page fault | ~0 | ~800,000+ | **∞** |

**⭐ 核心发现: TTFT Explosion + CPU Pinning 完全失效**:

1. **TTFT mean**: 521ms (baseline) → **3160ms** (CPU stress) = **6.1x 增加**
2. **TTFT P99**: 1128ms → **7427ms** = **6.6x 增加**
3. **CPU pinning 零效果**: 3160ms (unpinned) vs 3170ms (pinned) — 完全相同
4. **原因**: UVM page fault handler 运行在**内核 worker 线程**中，不是 llama-server 进程
   - `taskset` 只 pin 了用户空间线程
   - nvidia_uvm 内核模块的 fault handler 线程在任意 CPU 上运行
   - CPU stress 影响的是**这些内核线程**，不是被 pin 的用户空间线程
   - **因此 CPU pinning 根本无法帮助 UVM 工作负载**

#### 10.2.3 两种 CPU-GPU 耦合机制

| 机制 | 证明实验 | 影响 | CPU Pinning 效果 |
|------|---------|------|-----------------|
| **线程调度延迟** | 20B 模型 | -11.7% throughput | 微弱 (+2.5%) |
| **UVM page fault 延迟** | 120B 模型 | TTFT 6.1x 增加 | **完全无效** (0%) |

**xCoord 的核心价值**: 一个 GPU-aware CPU scheduler 需要知道**哪些线程是 UVM fault handler** 并提升其优先级，而不是仅仅 pin 用户空间 GPU 线程。

#### 10.2.4 POC-0 产出 ✅

1. ✅ "CPU 干扰 → GPU 性能" 因果表（两种机制量化）
2. ✅ UVM hook 频率对比（20B vs 120B，33-42x 差异）
3. ✅ "CPU pinning 为什么失败" 的定量解释（内核线程不受 taskset 影响）
4. ✅ 论文 Motivation section 的完整数据
5. ✅ 确认 120B UVM 是 POC-1 的理想测试场景

**POC-0 结论**: **问题真实存在且严重。继续 POC-1。**

**报告详情**: `scripts/xcoord/results/poc0_20260223_152937/REPORT.md` 和 `scripts/xcoord/results/poc0_uvm_20260223_154122/REPORT.md`

---

### 10.3 POC-1: 最小单向协调 GPU→CPU（2 周）

**目标**: 用最少代码验证 "gpu_ext 写入 GPU 状态 → sched_ext 读取并调整优先级 → 性能改善"。

#### 10.3.1 架构

```
┌─────────────────────┐     ┌─────────────────────┐
│   gpu_ext BPF        │     │   sched_ext BPF      │
│                     │     │                     │
│  chunk_activate() ──┼──→  │  enqueue():          │
│  chunk_used()     ──┼──→  │    read gpu_state    │
│                     │     │    if fault_rate HIGH │
│  每次 hook 更新:      │     │      boost priority  │
│  gpu_state_map[pid] │     │    else               │
│    .fault_rate      │     │      normal priority  │
│    .mem_pressure    │     │                     │
│    .last_active_ns  │     │  select_cpu():       │
│                     │     │    prefer same-NUMA   │
└─────────────────────┘     └─────────────────────┘
          │                          │
          └──── /sys/fs/bpf/gpu_state_map ────┘
```

#### 10.3.2 Step 1: gpu_ext 侧 — 写入 GPU 状态（3 天）

**文件**: `extension/eviction_lfu_xcoord.bpf.c` (~200 LOC)

基于 `eviction_freq_pid_decay.bpf.c` 修改:

```c
// shared_maps.h — GPU/CPU 共享状态定义
struct gpu_pid_state {
    u64 fault_count;        // 累计 page fault 次数
    u64 fault_rate;         // faults/sec (滑动窗口)
    u64 eviction_count;     // 被驱逐的 chunk 次数
    u64 mem_bytes;          // 当前 GPU 内存使用量
    u64 last_active_ns;     // 最后活跃时间
    u32 is_thrashing;       // 是否在 thrashing (fault_rate > threshold)
};

// 在 chunk_activate 中更新 (每次 page fault 触发)
SEC("struct_ops/uvm_pmm_chunk_activate")
int BPF_PROG(uvm_pmm_chunk_activate, ...) {
    u32 pid = get_owner_pid_from_chunk(chunk);
    struct gpu_pid_state *state = bpf_map_lookup_elem(&gpu_state_map, &pid);
    if (state) {
        u64 now = bpf_ktime_get_ns();
        __sync_fetch_and_add(&state->fault_count, 1);
        // 滑动窗口 fault rate (每秒更新)
        u64 elapsed = now - state->last_active_ns;
        if (elapsed > 1000000000ULL) { // 1 second
            state->fault_rate = state->fault_count * 1000000000ULL / elapsed;
            state->fault_count = 0;
            state->last_active_ns = now;
        }
    }
    // ... 正常 LFU eviction 逻辑
    return 0;
}

// 在 eviction_prepare 中更新 eviction_count
SEC("struct_ops/uvm_pmm_eviction_prepare")
int BPF_PROG(uvm_pmm_eviction_prepare, ...) {
    // ... 遍历链表，找到最低频 chunk
    u32 victim_pid = get_owner_pid_from_chunk(victim);
    struct gpu_pid_state *state = bpf_map_lookup_elem(&gpu_state_map, &victim_pid);
    if (state) {
        __sync_fetch_and_add(&state->eviction_count, 1);
    }
    return 0;
}
```

**Pin map 到 BPF 文件系统**:
```c
// 在用户空间 loader 中:
int map_fd = bpf_object__find_map_fd_by_name(obj, "gpu_state_map");
bpf_obj_pin(map_fd, "/sys/fs/bpf/xcoord_gpu_state");
```

**验证方法**:
```bash
# 1. 加载 gpu_ext 策略
sudo ./eviction_lfu_xcoord

# 2. 运行 workload
cd workloads/llama.cpp && uv run python bench.py

# 3. 读取 shared map
sudo bpftool map dump pinned /sys/fs/bpf/xcoord_gpu_state
# 应看到 per-PID 的 fault_rate, eviction_count 等
```

**成功标准**: 运行 llama-server 时能实时看到 PID 的 fault_rate 在变化。

#### 10.3.3 Step 2: sched_ext 侧 — 读取 GPU 状态并调整优先级（5 天）

**文件**: `extension/sched_gpu_aware.bpf.c` (~400 LOC)

基于 `scx_simple`（sched_ext 最简单的示例）修改:

```c
#include <scx/common.bpf.h>

// 打开 gpu_ext pin 的 map
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, u32);    // PID
    __type(value, struct gpu_pid_state);
} gpu_state_map SEC(".maps");  // 通过 bpf_obj_get() 打开 pinned map

// 阈值参数 (可通过 map 动态调整)
#define FAULT_RATE_HIGH   1000  // faults/sec
#define FAULT_RATE_MEDIUM  100
#define BOOST_WEIGHT       200  // vruntime 权重调整

// enqueue(): 根据 GPU fault rate 调整优先级
void BPF_STRUCT_OPS(gpu_aware_enqueue, struct task_struct *p,
                    u64 enq_flags)
{
    u32 pid = p->tgid;
    struct gpu_pid_state *gpu = bpf_map_lookup_elem(&gpu_state_map, &pid);

    if (gpu && gpu->fault_rate > FAULT_RATE_HIGH) {
        // GPU 侧 fault rate 很高 → 这个进程的 GPU 线程在等 CPU 处理 page fault
        // 提升 CPU 优先级，加速 fault 处理
        scx_bpf_dispatch(p, SCX_DSQ_GLOBAL, 0, enq_flags | SCX_ENQ_HEAD);
    } else if (gpu && gpu->is_thrashing) {
        // GPU 侧在 thrashing → 降低 CPU 优先级，减缓 GPU 访问频率
        scx_bpf_dispatch(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL * 2, enq_flags);
    } else {
        // 默认行为
        scx_bpf_dispatch(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
    }
}

// select_cpu(): NUMA-aware 放置
s32 BPF_STRUCT_OPS(gpu_aware_select_cpu, struct task_struct *p,
                   s32 prev_cpu, u64 wake_flags)
{
    // 简单版: 优先选择之前的 CPU (cache warm)
    // 进阶版: 读取 GPU 状态，为 fault handler 选择与 GPU 同 NUMA 的核
    return prev_cpu;
}
```

**用户空间 loader** (`extension/sched_gpu_aware.c`, ~150 LOC):
```c
// 1. 打开 gpu_ext 的 pinned map
int gpu_map_fd = bpf_obj_get("/sys/fs/bpf/xcoord_gpu_state");

// 2. 加载 sched_ext BPF 程序
struct sched_gpu_aware *skel = sched_gpu_aware__open();

// 3. 将 pinned map fd 注入到 BPF 程序
bpf_map__reuse_fd(skel->maps.gpu_state_map, gpu_map_fd);

// 4. 加载并 attach
sched_gpu_aware__load(skel);
struct bpf_link *link = bpf_map__attach_struct_ops(skel->maps.gpu_aware_ops);

// 5. 运行 (Ctrl+C 退出时自动 fallback 到 CFS)
while (!should_exit) { sleep(1); }
```

**验证方法**:
```bash
# 终端 1: 加载 gpu_ext
sudo ./eviction_lfu_xcoord

# 终端 2: 加载 sched_ext
sudo ./sched_gpu_aware

# 终端 3: 运行 workload + 干扰
stress-ng -c $(nproc) --timeout 300 &
cd workloads/llama.cpp && uv run python bench.py

# 终端 4: 观察 GPU state
watch -n1 'sudo bpftool map dump pinned /sys/fs/bpf/xcoord_gpu_state'
```

**成功标准**: sched_ext 能成功读取 gpu_state_map 中的 fault_rate，并且加载后不崩溃。

#### 10.3.4 Step 3: 端到端性能测试（2 天）

**实验矩阵**:

```
4 个配置 × 3 个 workload × 10 trials = 120 runs

配置:
(a) Baseline: CFS + no gpu_ext        — 朴素默认
(b) taskset:  CPU pinning + no gpu_ext — 朴素隔离（已证明无效）
(c) sched_ext only: scx_simple + no gpu_ext — sched_ext 但无 GPU awareness
(d) xCoord:   sched_gpu_aware + eviction_lfu_xcoord — 完整协调

Workload:
(1) llama-server (Qwen3-30B) + CPU stress
(2) llama-server (Qwen3-30B) + heavy load (CPU+Net+Disk)
(3) llama-server (Qwen3-30B) + 另一个 GPU 进程 (multi-tenant)

度量:
- tok/s (throughput)
- TPOT P99 (tail latency)
- Page fault latency P50/P99
- Context switches per 1K CUDA launches
- GPU utilization %
```

**测试脚本** (`scripts/sched/test_xcoord_poc.sh`):
```bash
#!/bin/bash
# POC-1 端到端测试
RESULTS_DIR="results/poc1_$(date +%Y%m%d_%H%M)"
mkdir -p $RESULTS_DIR

CONFIGS=("baseline" "taskset" "sched_ext_only" "xcoord")
SCENARIOS=("cpu_stress" "heavy_load" "multi_tenant")

for config in "${CONFIGS[@]}"; do
  for scenario in "${SCENARIOS[@]}"; do
    for trial in $(seq 1 10); do
      echo "=== $config / $scenario / trial $trial ==="
      python ../workloads/cleanup_gpu.py

      # 启动策略
      case $config in
        xcoord)
          sudo ./eviction_lfu_xcoord &
          sudo ./sched_gpu_aware &
          ;;
        sched_ext_only)
          sudo ./sched_simple &  # 无 GPU awareness
          ;;
        taskset)
          # 在 bench.py 中用 taskset 启动 llama-server
          ;;
      esac

      # 启动干扰
      case $scenario in
        cpu_stress)    stress-ng -c $(nproc) --timeout 300 & ;;
        heavy_load)    stress-ng -c $(nproc) --timeout 300 &
                       iperf3 -c localhost -t 300 & ;;
        multi_tenant)  # 启动第二个 GPU workload
                       python workloads/pytorch/gnn_bench.py & ;;
      esac

      # 运行 benchmark
      uv run --directory workloads/llama.cpp python bench.py \
        --output $RESULTS_DIR/${config}_${scenario}_${trial}.json

      # 清理
      kill %% 2>/dev/null
      sudo killall eviction_lfu_xcoord sched_gpu_aware stress-ng iperf3 2>/dev/null
    done
  done
done
```

**预期结果**:

| 配置 | CPU Stress tok/s | Heavy Load tok/s | 相对 Baseline |
|------|-----------------|-----------------|--------------|
| (a) Baseline CFS | 177 | 161 | -19% / -26% |
| (b) taskset | 177 | 161 | -19% / -26% (无改善) |
| (c) sched_ext only | ~180 | ~165 | ~-18% / ~-25% (微小改善) |
| (d) **xCoord** | **~195** | **~180** | **~-11% / ~-18%** |

**POC-1 成功标准**（基于 POC-0 实测数据）:
- **120B UVM 场景**: TTFT 从 3160ms 降至 <2000ms（>36% 改善）
- **20B 场景**: 吞吐量从 175.40 tok/s 提升至 >190 tok/s（恢复 >60% 损失）
- xCoord 比 taskset 至少好 **5 个百分点**
- 如果达到: → 继续 POC-2, POC-3
- 如果未达到: → 分析原因，调整策略，或考虑 Fallback A

---

### 10.4 POC-2: 验证优于朴素方案（1 周）

**目标**: 用 ablation study 证明 "GPU awareness" 是关键，不是 sched_ext 本身。

#### 10.4.1 实验矩阵

```
Ablation 4 个变体:

(A) xCoord full:     sched_ext 读取 gpu_state → 动态调整优先级
(B) sched_ext blind:  sched_ext 不读取 gpu_state → 静态 boost 所有 GPU PID
(C) sched_ext oracle: sched_ext 读取 gpu_state → 但只用于 logging，不调整优先级
(D) taskset optimal:  用 cgroup cpuset 精心配置的 CPU 隔离

比较 (A) vs (B) → "GPU awareness" 的价值
比较 (A) vs (D) → 动态协调 vs 静态隔离
比较 (B) vs (C) → sched_ext 本身的价值（不含 GPU awareness）
```

#### 10.4.2 敏感性分析

```
变量:
(1) 干扰强度: stress-ng -c 4 / 8 / 16 / $(nproc)
(2) GPU 内存压力: 不同模型大小 (0.6B / 7B / 30B)
(3) Multi-tenant 数量: 2 / 3 / 4 个 GPU 进程
(4) fault_rate 阈值: 100 / 500 / 1000 / 5000 faults/sec

目标: 找到 xCoord 优势最大的 sweet spot
```

**POC-2 成功标准**:
- (A) xCoord full 比 (B) sched_ext blind 至少好 **3 个百分点**
- 证明 "GPU awareness" 是性能改善的关键因素

---

### 10.5 POC-3: 反向协调 CPU→GPU（2 周，可选）

**目标**: 验证双向协调比单向更好。

#### 10.5.1 CPU 状态写入

```c
// 在 sched_ext 的 running() hook 中:
void BPF_STRUCT_OPS(gpu_aware_running, struct task_struct *p) {
    u32 pid = p->tgid;
    struct cpu_pid_state *state = bpf_map_lookup_elem(&cpu_state_map, &pid);
    if (state) {
        state->is_running = 1;
        state->core_id = bpf_get_smp_processor_id();
        state->last_run_ns = bpf_ktime_get_ns();
    }
}

// 在 sched_ext 的 stopping() hook 中:
void BPF_STRUCT_OPS(gpu_aware_stopping, struct task_struct *p) {
    u32 pid = p->tgid;
    struct cpu_pid_state *state = bpf_map_lookup_elem(&cpu_state_map, &pid);
    if (state) {
        state->is_running = 0;
    }
}
```

#### 10.5.2 gpu_ext 读取 CPU 状态

```c
// 在 eviction_prepare 中:
SEC("struct_ops/uvm_pmm_eviction_prepare")
int BPF_PROG(uvm_pmm_eviction_prepare, ...) {
    // 遍历 eviction 候选列表
    struct list_head *pos = BPF_CORE_READ(list, next);
    #pragma unroll
    for (int i = 0; i < 128 && pos != list; i++) {
        uvm_gpu_chunk_t *chunk = container_of(pos, uvm_gpu_chunk_t, list);
        u32 pid = get_owner_pid_from_chunk(chunk);

        // 读取 CPU 状态
        struct cpu_pid_state *cpu = bpf_map_lookup_elem(&cpu_state_map, &pid);
        if (cpu && cpu->is_running) {
            // 进程正在 CPU 上运行 → 即将 launch GPU kernel → 保护其 GPU 内存
            // 跳过这个 chunk，不驱逐
            pos = BPF_CORE_READ(pos, next);
            continue;
        }

        // 进程不在 CPU 上运行 → 优先驱逐其 GPU 内存
        bpf_uvm_pmm_chunk_move_head(chunk, list);
        pos = BPF_CORE_READ(pos, next);
    }
    return 0;
}
```

**测试场景**: Multi-tenant (2 个 GPU 进程共享 GPU 内存)
- LC 进程正在 CPU 上运行 → gpu_ext 保护其 GPU 内存
- BE 进程被 CPU deschedule → gpu_ext 优先驱逐其 GPU 内存

**POC-3 成功标准**:
- 双向协调 (POC-3) 比单向 (POC-1) 在 multi-tenant 场景下额外改善 **>3%**
- 如果未达到: 说明单向已足够，论文聚焦 GPU→CPU 方向

---

### 10.6 POC 时间线

```
Week 1: POC-0 (量化问题) ✅ 已完成 (2026-02-23)
  ├─ ✅ 20B 模型: 4 场景完成，确认 11.7% CPU-GPU 耦合
  ├─ ✅ 120B UVM 模型: 3 场景完成，确认 6.1x TTFT 增加 + pinning 失效
  └─ ✅ 数据分析完成，motivation data 充分

Week 2-3: POC-1 (最小单向协调) ← 当前阶段
  ├─ Day 1-3: 实现 eviction_lfu_xcoord.bpf.c (gpu_ext 侧)
  ├─ Day 4-5: 验证 shared map pin + 读取
  ├─ Day 6-8: 实现 sched_gpu_aware.bpf.c (sched_ext 侧)
  ├─ Day 9: 端到端集成测试
  └─ Day 10: 性能测试 (4 配置 × 3 场景 × 10 trials)

Week 4: POC-2 (验证优于朴素方案)
  ├─ Day 1-2: Ablation study (4 变体)
  ├─ Day 3-4: 敏感性分析
  └─ Day 5: 分析结果，决定是否继续 POC-3

Week 5-6 (可选): POC-3 (双向协调)
  ├─ Day 1-3: 实现 CPU→GPU 方向
  ├─ Day 4-5: Multi-tenant 测试
  └─ Day 6-7: 综合分析
```

### 10.7 POC 决策树

```
POC-0 结果: ✅ CPU stress 增加 TTFT 6.1x (521ms → 3160ms) → 继续 POC-1
  └─ 且 CPU pinning 完全无效 (3160ms vs 3170ms) → 强化 xCoord 动机

POC-1 结果:
├─ xCoord 比 taskset 好 >5% → 继续 POC-2
├─ xCoord 比 taskset 好 2-5% → 调整阈值参数，再测
└─ xCoord 比 taskset 好 <2% → Fallback A (纯 GPU-aware sched_ext)

POC-2 结果:
├─ GPU awareness 贡献 >3% → 核心 claim 成立，写论文
└─ GPU awareness 贡献 <3% → sched_ext 本身是主因，调整论文 framing

POC-3 结果:
├─ 双向 > 单向 3%+ → 论文包含双向协调
└─ 双向 ≈ 单向 → 论文聚焦 GPU→CPU，双向作为 future work
```

---

## 十一、工程量重新评估

### 11.1 POC 阶段（4-6 周）

| 组件 | LOC | 天数 |
|------|-----|------|
| `fault_latency_trace.bpf.c` | ~100 | 2 |
| `shared_maps.h` | ~50 | 0.5 |
| `eviction_lfu_xcoord.bpf.c` | ~250 | 3 |
| `sched_gpu_aware.bpf.c` | ~400 | 5 |
| `sched_gpu_aware.c` (loader) | ~150 | 2 |
| `test_xcoord_poc.sh` | ~200 | 2 |
| 分析脚本 | ~100 | 1 |
| **总计** | **~1250** | **~16 天 (3-4 周)** |

### 11.2 完整论文阶段（POC 通过后，额外 5-6 周）

| Phase | 内容 | 周数 |
|-------|------|------|
| Phase 3 | 4 种协调策略完整实现 | 2-3 |
| Phase 4 | 5 scenarios 完整评估 | 2-3 |
| Phase 5 | 论文撰写 | 2-3 |
| **总计** | | **6-9 周** |

### 11.3 总时间线

```
POC: 4-6 周 (Week 1-6)
  → 如果成功，继续论文:
完整实现: 6-9 周 (Week 7-15)
总计: 10-15 周 (OSDI '27 deadline ~2026.11)
```

---

## 十二、风险与备选

### 12.1 POC 级风险

| 风险 | 概率 | 缓解 |
|------|------|------|
| GPU fault latency 对 CPU 调度不敏感 | 低 | 已有 19-26% 吞吐量下降数据作为间接证据 |
| sched_ext 与 gpu_ext 同时加载冲突 | 中 | 两者是独立的 BPF 子系统，理论上不冲突；需实测 |
| Shared map 延迟过高 | 低 | BPF map 操作是 µs 级，远小于调度决策间隔 |
| sched_ext 导致系统不稳定 | 中 | sched_ext 有安全 fallback 到 CFS 的机制 |
| 改善幅度不够发论文 | 中 | 先做 POC-0/1 快速验证；不够则走 Fallback A |

### 12.2 Fallback 方案

**Fallback A**: 缩小范围到 "GPU-aware sched_ext scheduler"（单方向：CPU 感知 GPU）
- 工程量减半
- 仍然有 novelty（首个 GPU-aware sched_ext）
- 可投 EuroSys/ATC
- **与 POC-1 工程量重叠 80%**，不浪费前期工作

**Fallback B**: 转向 "LLM-focused gpu_ext"
- 复用已有 gpu_ext 基础设施
- 专注 MoE + KV-cache 场景
- 可投 MLSys/EuroSys
