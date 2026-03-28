# xCoord：跨 CPU-GPU eBPF 资源协调

> **方向**: sched_ext (CPU) + gpu_ext (GPU) + shared BPF maps = 跨子系统协调
> 上次更新：2026-03-19

---

## 0. 当前快照

- **CPU 侧**: 6 个 sched_ext 策略实现（baseline → minimal → serving → xcoord → coord v1 → FPRS v2）
- **GPU 侧**: 3 个 gpu_ext 策略（always_max_cycle_moe / lfu_xcoord / always_max_xcoord）
- **推荐组合**: `prefetch_always_max_xcoord` (GPU) + `sched_gpu_xcoord` (CPU)
- **最佳结果**: vLLM 30B + stress: TPOT **-7.4%**, throughput **+8.5%**
- **FPRS (closed-loop)**: 20B + FAISS: mean latency **-4.8%**, throughput **+5.3%** — 但主要来自 UVM worker priority，非 feedback control
- **Dual-actuator QoS eviction**: 设计完成，E12 首次失败（gpu_block_access 不触发 bug），迁移到 chunk_activate 后**未产出正面结果**
- **核心瓶颈**: CPU throttle 对 GPU-bound BE 无效（因果链断裂）
- **不在当前论文中**: xCoord/FPRS 全部是 repo-only，距可发表差距大

### 0.1 当前资产与限制

| 类别 | 现状 | 对 xCoord 的意义 |
|------|------|------------------|
| NVIDIA UVM 模块 + 5 个 BPF hooks | 可用 | GPU 侧基础设施已具备，可直接承载 xCoord |
| eBPF 策略库（eviction/prefetch/scheduling） | 已有 ~20 个策略 | 可快速复用为 baseline / xcoord / qos 变体 |
| Workload 基础设施 | llama.cpp / vLLM / PyTorch / FAISS 脚本齐全 | 评估矩阵已具备，不必重搭实验框架 |
| BPF CO-RE 访问 chunk / VA block / 链表 | 已验证 | **零内核修改**仍可完成共享状态与 eviction 控制 |
| 主要限制 1 | CPU throttle 执行器偏弱 | GPU-bound BE 时 FPRS 上限低 |
| 主要限制 2 | sched_ext hot-path 有开销天花板 | 高线程 / barrier workload 不适合 blind boost |

### 0.2 关键事实

- UVM GPU1 BH 是 **`SCHED_OTHER` kthread**（`ppid=2`），不是不可调度的中断上下文；sched_ext **可以** boost 它。
- `uvm_worker_pids` 机制成立：gpu_ext hook 中 `bpf_get_current_pid_tgid()` 记录当前 UVM worker 的 `tgid`，sched_ext 再用 `p->tgid` 匹配并提升优先级。
- 完整链路现已验证通过：`prefetch_always_max_xcoord` + `sched_gpu_*` 组合下，`gpu_state_map` 有 `fault_rate/eviction_count`，`uvm_worker_pids` 非空，`gpu_boosted` 非零。
- E1 nsys 中 UVM BH 零 sched events 的真正原因是 **追踪范围限制**（只看目标进程子进程）叠加 **当时未加载 gpu_ext、几乎无 fault**，不是 “BH 不能被调度”。

---

## 1. 核心 Thesis + Novelty

**Thesis**: GPU workload 性能同时取决于 CPU 和 GPU 两侧的资源管理决策，但现有系统将两者独立管理。GPU 内存状态（fault_rate, eviction_pressure）是 CPU 调度的关键缺失信号。

**Novelty（诚实评估）**:
- 所有已实现的 CPU 侧策略**本质都是 priority scheduling**（boost GPU / throttle non-GPU = `nice -20` 变体）
- coord v1 的 4 项声称创新均可归约为 DSQ 队列分离 + static mode switch
- **FPRS (v2) 是唯一算法创新**：用 GPU fault_rate 作为 CPU 调度器的积分控制信号（closed-loop feedback control）
- **但 FPRS 效果弱 (~5%)**，且改善主要来自 UVM worker priority，非 BE throttle

**关键实验证据（Motivation）**:
- CPU stress → 30B 推理 **-19~26%** 吞吐，context switch 增加 **22,000x**
- CPU pinning (taskset + nice) **无效**（大模型线程多，pinning 加剧核内竞争）
- Meta sched_ext 恢复 ~25% 性能，但**看不到 GPU 内存状态**

### 1.1 论文定位

- **Title**: `xCoord: Coordinated CPU-GPU Resource Management via Cross-Subsystem eBPF`
- **一句话 claim**: GPU 内存子系统的运行时状态（`fault_rate`、`eviction_pressure`）是 CPU 调度器缺失的第一类信号；共享这些信号后，调度器能做出 blind boost 和静态 CPU 隔离做不到的决策。
- **与 repo 当前实现的距离**: thesis 和 design 已成型，但 repo 中的 FPRS / dual-actuator 仍是 POC 级结果，效果和方法学都不足以直接投稿。

### 1.2 与 gpu_ext 论文的关系

| 维度 | gpu_ext（在审） | xCoord（新论文） |
|------|----------------|-----------------|
| 焦点 | GPU driver 可编程性 | CPU-GPU 跨子系统协调 |
| 技术栈 | gpu_ext BPF hooks + device-side eBPF | `sched_ext` + `gpu_ext` + shared maps |
| 评估重点 | 单 tenant GPU 策略 | Multi-tenant 协调、CPU-GPU 联动 |
| 核心 insight | GPU driver 可被 eBPF 扩展 | GPU memory state 应成为 CPU scheduling signal |
| 关系 | 独立论文 | 以 gpu_ext 作为 GPU 侧基础设施与证据来源 |

### 1.3 Novelty 细化

- Shared BPF map 只是管道，不是 insight；真正的 novelty 是 **GPU memory awareness 进入 CPU scheduling loop**。
- CPU-GPU 耦合是**不对称**的：GPU page fault、kernel launch、DMA/TLB 控制面都由 CPU 驱动，但传统 CPU 调度器看不到 GPU memory pressure。

| 对比对象 | 已有能力 | 缺口 | xCoord 补足 |
|----------|----------|------|-------------|
| Meta `sched_ext` | 识别 GPU-critical threads，恢复 ~25% noisy-neighbor 损失 | 看不到 `fault_rate/eviction pressure` | GPU memory-aware CPU scheduling |
| MSched | GPU 内部 proactive memory scheduling | 不协调 CPU scheduler | 补上 CPU 控制面与调度盲区 |
| `taskset` / cpuset | 静态 CPU 隔离 | 对 30B/120B 多线程 / UVM 场景失效 | 动态、按 GPU 状态实时调度 |

### 1.4 目标会议

| 会议 | 预计 2026 Deadline | 匹配度 | 备注 |
|------|--------------------|--------|------|
| OSDI '27 | ~2026.11 | ★★★★★ | cross-subsystem design 最匹配 |
| EuroSys '27 | ~2026.10 | ★★★★ | system building 友好 |
| ASPLOS '27 | ~2026.10 | ★★★★ | 若强调 CPU-GPU coupling 可投 |
| ATC '27 | ~2027.01 | ★★★ | practical backup |

---

## 2. 设计架构

```
sched_ext BPF (CPU)  ←→  shared BPF maps  ←→  gpu_ext BPF (GPU)
  select_cpu()              gpu_state_map        chunk_activate
  enqueue()                 uvm_worker_pids      gpu_page_prefetch
  dispatch()                gpu_process_pids     gpu_evict_prepare
```

**四种协调策略**:
1. **GPU-Aware CPU Scheduling**: fault_rate 高 → boost UVM worker + LC thread
2. **CPU-Aware GPU Eviction**: is_running → 保护 running process 的 GPU pages
3. **Coordinated Anti-Thrashing (FPRS)**: LC fault_rate → 积分控制器 → BE CPU throttle
4. **Dual-Actuator QoS Eviction**: LC fault_rate → eviction bias → 直接 VRAM LRU 控制

### 2.1 FPRS: Fault-Pressure Regulated Scheduling

**诚实评估**: coord v1 的本质仍是 DSQ 队列分离 + static priority；真正的 closed-loop 设计只在 coord v2 / FPRS 中出现。

**CPU→GPU 因果链**:

```text
CPU 调度 BE 线程
  -> BE 提交更多 GPU kernel
  -> GPU 触碰更多新页面
  -> UVM fault / eviction 增加
  -> LC 下次 kernel stall
```

| 维度 | Priority Scheduling | FPRS |
|------|---------------------|------|
| 决策信号 | “是不是 GPU 进程”二值分类 | `lc_fault_rate - target` 连续误差信号 |
| 控制方式 | boost / throttle / DSQ 切换 | integral + decay 的 closed loop |
| BE 影响 | 二值优先级 | 0-100% throttle 连续调节 |
| 目标函数 | 无显式目标 | 维持 LC `fault_rate` 接近 target |

**控制参数**:

| 参数 | 原始设计 | E11 调优后 |
|------|----------|------------|
| `target_lc_fault_rate` | 100 faults/s | 100 |
| `regulate_interval_ns` | 100ms | 100ms |
| `ki_gain` | 1 | 10 |
| `decay_shift` | 2 | 1-2 之间试验，当前仍有振荡 |
| `max_integral` | 100000 | 10000 |
| `min_be_slice_ns` | 1ms | 1ms |

**核心 `regulate()` pseudocode**:

```text
regulate():
  now = ktime_get_ns()
  if now - last_regulate_ns < 100ms:
      return

  lc_fr = max fault_rate across lc_pid_array
  if lc_fr > target_lc_fault_rate:
      error = lc_fr - target_lc_fault_rate
      pressure_integral = min(pressure_integral + error * ki_gain,
                              max_integral)
  else:
      pressure_integral >>= decay_shift

  be_throttle_pct = min(pressure_integral * 1000 / max_integral, 1000)
```

**enqueue 决策逻辑**:

| 任务类型 | 决策 |
|----------|------|
| LC（`-p PID`） | `GPU_BOOST_DSQ` + max slice |
| UVM BH worker | `GPU_BOOST_DSQ` + proportional slice，优先解决已发生 fault |
| BE GPU（auto-detected） | `be_slice = max_slice * (1000 - throttle) / 1000`；`throttle > 50%` 时降到 `SHARED_DSQ` |
| Non-GPU | 早期 backpressure 已移除，避免网络/系统服务被误伤 |

### 2.2 Dual-Actuator QoS Eviction

**动机**: E11 证明 FPRS 只有 ~5% 改善，因为 FAISS 是 GPU-bound，CPU throttle 经过 “CPU→GPU→VRAM” 的间接因果链，执行器太弱。

| 角色 | 设计 |
|------|------|
| Sensor | `gpu_state_map` 中 LC `fault_rate`（在 `chunk_activate` 更新） |
| Controller | 与 FPRS 同类的 integral + decay |
| Actuator | `eviction_bias` 控制 LRU：LC `move_tail` 保护，BE `move_head` 提前驱逐 |

**核心逻辑**:

```text
if eviction_bias > 0 and owner is LC:
    move_tail(chunk)   # protect
elif eviction_bias > 0 and owner is BE:
    move_head(chunk)   # demote
else:
    keep default policy
```

**E12 首次失败摘要**:

| 问题 | 关键信号 | 结论 / 修复 |
|------|----------|-------------|
| `gpu_block_access` 不触发 | `activate=1.8M`, `used=0`, `lc_prot=0` | 保护逻辑必须移到 `chunk_activate` |
| LC 信号为 0 | `bias=0%`, `integral=0`, `lc_fr=0` | 20B server 在 FAISS 期间被 OOM / 无有效 fault 信号 |
| 首版设计未产出正面结果 | 首次 E12 失败；迁移到 `chunk_activate` 后仍未得到正面结果 | Dual-actuator 目前仍停留在 repo 级设计探索 |

### 2.3 技术可行性

| 方面 | 现有接口 / 资产 | 可以做什么 | 主要风险 |
|------|------------------|------------|----------|
| `sched_ext` API | `select_cpu()/enqueue()/dispatch()/running()/stopping()`、task storage、pinned maps | GPU-aware placement、priority、写 `cpu_state_map` | hot-path overhead，尤其是高线程 / barrier workload |
| `gpu_ext` API | `chunk_activate/chunk_used/evict_prepare/prefetch` + BPF CO-RE | 写 `gpu_state_map`、追踪 worker、控制 eviction bias | 某些 hook 触发点比预期浅，需要先验证 counter |
| Shared state | `gpu_state_map`、`uvm_worker_pids`、`gpu_process_pids`、`lc_pid_array` | µs-ms 级跨子系统协调 | staleness、PID 匹配、map cleanup |
| 环境 | Linux 6.15 + custom `nvidia_uvm` + 零内核修改 | 当前 repo 已能端到端运行 | 模型 OOM、loader/toolchain 复杂度、workload 稳定性 |

---

## 3. 决策记录

| 决策 | 原因 | 日期 |
|------|------|------|
| 不同策略放不同文件 | 独立可对比，不修改 baseline | 2026-02-28 |
| SHARED_DSQ 用于非 GPU 任务 | local DSQ 优先于 custom DSQ dispatch | 2026-02-28 |
| coord v1 不算 novelty | 核心只是 DSQ 队列分离 = priority scheduling | 2026-03-01 |
| FPRS 替代 coord v1 | 需要 closed-loop feedback control | 2026-03-01 |
| Non-GPU backpressure 移除 | throttle 100% 时网络/系统服务也被 throttle → LC 45s 首请求 | 2026-03-01 |
| Dual-actuator 设计 | CPU throttle 对 GPU-bound BE 无效，需直接 VRAM 控制 | 2026-03-01 |
| 保护逻辑移到 chunk_activate | gpu_block_access 从不触发（chunk TEMP_PINNED） | 2026-03-01 |
| sched_ext overhead with gpu_ext = 零 | FAISS GPU-bound 时 sched_ext 开销被掩盖 | 2026-03-01 |
| 不再重复 “跑 baseline→解释失败” 循环 | E1→E4 多次单侧 baseline 分析没有推进完整链路验证 | 2026-02-28 |
| nsys 零 sched events 不等于 BH 不可调度 | 真实原因是 tracing scope 限制 + 当时几乎无 fault | 2026-02-28 |
| 结论必须绑定 workload 与 phase | batch/serving、loading/steady-state 差异极大，不能跨场景外推 | 2026-02-28 |
| 原始 plan 保留 raw logs，v2 只保留结论 | 避免 2000+ 行历史记录淹没设计与决策 | 2026-03-19 |

---

## 4. 任务追踪表

| # | 任务 | 状态 | 关键结果 |
|---|------|:---:|------|
| 1 | 完整链路测试 (gpu_ext + sched_ext) | ✅ | gpu_state_map 有数据，uvm_worker_pids 追踪正常 |
| 2 | prefetch_always_max_xcoord | ✅ | pp=222, tg=85（与无 xCoord 持平） |
| 3 | E5: 120B batch + stress | ✅ | **负面**: stress -1.9% pp, sched_ext 零效果。GPU/PCIe 瓶颈 |
| 4 | sched_gpu_xcoord (auto-detect) | ✅ | state_boost=15K, 非 UVM 场景与 baseline 相同 |
| 5 | E6: vLLM 30B + stress | ✅ | **正面**: TPOT -7.4%, tput +8.5% |
| 6 | E7: 多 workload 共存 (20B LC + 120B BE) | ✅ | 120B loading 时 TPOT 3x, sched_ext TTFT -35% |
| 7 | E8: coord vs xcoord on vLLM | ✅ | **中性**: 30B 无 thrashing, coord 未激活 |
| 8 | E9: FAISS + stress + coord | ✅ | **中性**: FAISS GPU-bound, CPU throttle 无效 |
| 9 | E10: Multi-tenant coord v1 vs xcoord | ✅ | **方法学问题严重**: thrashing 未激活, 顺序偏差, 只 3 runs |
| 10 | FPRS (coord v2) 实现 | ✅ | 积分控制器 + 8 stats, 构建/加载通过 |
| 11 | sched_gpu_xcoord_noad | ✅ | 去除 auto-detect, 隔离效果 |
| 12 | E10 v2: 4 conditions × 5 runs | ✅ | FPRS 未激活；median TPOT `16.82 / 12.06 / 12.06 / 12.22ms`（no_sched / xcoord / noad / fprs），全条件双峰 |
| 13 | E11: FPRS 激活场景 (20B + FAISS) | ✅ | `lc_fr 52→3828`；mean -4.8%, P99 -4.9%, tput +5.3%。控制器振荡，改善主要来自 UVM worker priority |
| 14 | Dual-actuator QoS eviction 设计 | ✅ | prefetch_always_max_qos.bpf.c |
| 15 | E12: QoS eviction 实验 | ✅ 失败 | `activate=1.8M, used=0, lc_prot=0, bias=0%, lc_fr=0` |
| 16 | FPRS 调优 + 大规模实验 | ❌ | 控制器振荡, 需隔离 UVM worker vs BE throttle |
| 17 | E12 v2: chunk_activate-based QoS | ❌ | 设计完成, 未产出正面结果 |
| 18 | E11-A: 120B+120B feasibility probe | ✅ 失败 | 两个 120B UVM 进程约 120GB RAM，接近 125GB 上限，系统 OOM 两次后放弃 |
| 19 | POC-0: CPU-GPU 耦合量化 | ✅ | 20B stress -11.7%；120B 在 CPU stress 下 TTFT 6.1x；pinning 对 UVM 内核线程无效 |
| 20 | POC-1: 最小 GPU→CPU 协调 | ✅ | 6 文件 / 1281 LOC；c=1 TPOT `4.63→3.70ms`，c=4 P99 TTFT -27% |
| 21 | POC-2: 优于朴素方案的 ablation | ⏳ 设计完成 | xCoord full / blind / oracle / cpuset 矩阵已定义，待系统执行 |
| 22 | POC-3: CPU→GPU 反向协调 | ⏳ 设计完成 | `running()/stopping()` → `cpu_state_map` → `eviction_prepare` 保护 running process pages |
| 23 | W1: GNN UVM screening | ✅ | stress 仅 +4.8%；taskset 更差；无 stress + boost 反而 +3% overhead |
| 24 | W2: FAISS screening | ✅ | with gpu_ext 时 sched_ext 基本中性；旧 blind-boost 变体曾出现 +67~759% 额外开销 |
| 25 | W3: llama.cpp 20B scheduler A/B | ✅ | `GPU_BOOST_DSQ + SHARED_DSQ` 才能恢复 c=1 TPOT；serving/local-DSQ 方案失败 |
| 26 | E1: 120B UVM batch profiling | ✅ | CPU ON-CPU 99.7-99.9%，sched overhead <0.4%，瓶颈是 PCIe / GPU |
| 27 | E2: 120B batch + stress profiling | ✅ | `gpu_ext + stress` 仅 -0.5% / -1.4%，batch 模式不适合 sched_ext |
| 28 | E3: vLLM 30B profiling + stress | ✅ | stress 使 TPOT P99 `615→2062ms` (+235%)；blind boost 反而吞吐 -62% |
| 29 | E4: 20B no-UVM profiling | ✅ | per-token 86% 是 CPU 工作，但无竞争时 ON-CPU 100%，blind boost 无主动收益 |

### 4.1 POC 摘要

| POC | 设计 | 结果 | Bug / Lesson |
|-----|------|------|--------------|
| POC-0 | 量化 20B 与 120B 的 CPU-GPU 耦合 | 20B stress -11.7%；120B TTFT 6.1x；两种耦合机制被区分 | `taskset` 不影响 UVM 内核线程 |
| POC-1 | gpu_ext 写共享 map，sched_ext 做最小单向 boost | c=1 TPOT 恢复到 baseline；c=4 P99 TTFT -27% | blind boost 有效，但 novelty 不足 |
| POC-2 | xCoord full / blind / oracle / cpuset ablation | 设计完成，尚未系统跑完 | 必须证明改善来自 GPU awareness，而不是 sched_ext 本身 |
| POC-3 | `running()/stopping()` 写 CPU 状态，gpu_ext 反向保护页 | 设计完成，尚未完成端到端验证 | reverse coordination 可作为 backup thesis |

### 4.2 POC-1 实现浓缩

**文件清单 + LOC**:

| 文件 | LOC | 作用 |
|------|-----|------|
| `shared_maps.h` | 48 | 定义 GPU/CPU 共享状态与 worker map |
| `eviction_lfu_xcoord.bpf.c` | 313 | LFU 驱逐 + `gpu_state_map` 写入 + worker PID 追踪 |
| `eviction_lfu_xcoord.c` | 252 | GPU loader，pin 两个共享 map |
| `sched_gpu_baseline.bpf.c` | 180 | sched_ext BPF：读取 `uvm_worker_pids` / `gpu_state_map` |
| `sched_gpu_baseline.c` | 214 | sched_ext loader，`bpf_obj_get + reuse_fd` 复用共享 map |
| `poc1_xcoord_scheduling.sh` | 274 | 自动化实验脚本 |
| **总计** | **1281** | POC-1 最小单向协调实现 |

**关键设计决策**:

| 决策 | 原因 |
|------|------|
| 双 `vmlinux.h` | gpu_ext 与 sched_ext 依赖的类型集合不同 |
| 直接 libbpf API | clang 18 与 UEI 宏不兼容，避开 32-bit atomics 问题 |
| `SCX_ENUM_INIT` 必须在 `open()` 后调用 | 否则 `SCX_DSQ_LOCAL / SCX_SLICE_DFL / SCX_ENQ_HEAD` 全为 0 |
| 独立 `GPU_BOOST_DSQ` | 避免 FIFO/PRIQ 冲突，并让 GPU 任务与普通任务分流 |

---

## 5. 关键实验结果

### 5.0 Motivation 数据

| 主题 | 关键数字 | 结论 |
|------|----------|------|
| CPU stress 对 GPU serving 的影响 | Qwen3-30B `218.7→177.1 tok/s` (-19.0%)；heavy load `160.8 tok/s` (-26.5%)；context switch / 1K launches `0.1→2578/2895` | CPU contention 是主导因素 |
| 干净环境 baseline | 总调度影响仅 1.2%，但单次抢占代价 `15.3ms vs 2us`（约 7650x） | 问题主要体现在 tail latency，不是平均值 |
| CPU pinning 失败 | 0.6B 上有效；30B 上 `2578→3179 sched/1K` 更差 | 静态 pinning 不适合大模型多线程 / UVM 场景 |
| Meta `sched_ext` 数据 | noisy-neighbor 恢复 ~25%，GPU capacity +4% | 证明 sched_ext 有价值，但仍看不到 GPU memory state |

### 5.1 单 workload + stress

| 实验 | Workload | sched_ext 效果 | 原因 |
|------|----------|---------------|------|
| E5 | 120B batch + stress | **零** | GPU/PCIe-bound |
| E6 | vLLM 30B + stress | **TPOT -7.4%, tput +8.5%** | CPU-bound serving |
| E7 (FAISS) | FAISS + stress + xcoord | **零** (with gpu_ext) | GPU-bound |

### 5.2 Multi-tenant

| 实验 | 配置 | 结果 |
|------|------|------|
| E10 v2 | 20B(LC) + 120B(BE), 4×5 runs | FPRS 未激活 (LC fits in VRAM) |
| E11 | 20B(LC) + FAISS(BE), 60s traffic | FPRS: mean -4.8%, tput +5.3%。改善来自 UVM worker priority |

#### E10 v2 补充

- 4 conditions × 5 runs 呈明显双峰：`no_sched 18.34/11.56/11.14/16.82/17.32`，`xcoord 22.03/12.06/11.90/11.55/22.78`，`xcoord_noad 11.36/12.06/28.02/11.72/19.92`，`fprs 18.65/12.12/12.22/11.70/19.41`。
- FPRS 统计在 5 次运行里均为 `throttle=0%`, `be_reg=0`, `integral=0`；LC `fault_rate=1-8/s << target=100`。
- 结论不是 “FPRS 算法错误”，而是 **20B fit in VRAM**；该 workload mix 的干扰路径主要是 GPU compute / PCIe contention，而不是 LC 页面被驱逐。

#### E11 细节

- 失败前置尝试：`120B + 120B` 双 UVM 进程约占 120GB system RAM，系统在 125GB RAM 机器上 OOM 两次，因此改为 `20B serving + FAISS SIFT100M`。

| 项目 | 关键信息 |
|------|----------|
| 干扰量化 | 20B 单次 64 tok：baseline `0.23s / fault_rate=52`；FAISS 首次请求 `2.5s / 3828`；后续请求 `0.44s / ~1446` |
| 3 个 FPRS bug | non-GPU backpressure 让 LC 首请求到 **45s**；`fault_rate` 不衰减导致永久 throttle；`ki_gain/max_integral` 让响应时间慢到 ~50s |
| 修复后控制器 | `ki 1→10`，`max_integral 100K→10K`，staleness `>2s` 视为 0；但 throttle 仍在 0-100% 间振荡 |
| 实际改善来源 | `uvm_worker boosts=57,068`，`be_reg=12,176`，`be_demoted=0`；改善主要来自 UVM worker priority，而非 BE throttle |

### 5.3 FPRS 控制器行为

- throttle 峰值 100%, lc_fr 峰值 1713
- **振荡**: 请求期间 throttle↑, 请求间隙 throttle↓, 无法维持稳态
- be_demoted=0: FAISS 从未被降级到 SHARED_DSQ
- 调优后响应更快，但仍缺少稳定 steady-state；当前仍需隔离 “UVM worker boost” 与 “BE throttle” 的独立贡献

---

## 6. 评估设计

### 6.1 Research Questions

| RQ | 问题 | 核心对比 |
|----|------|----------|
| RQ1 | 跨子系统协调能否改善 multi-tenant GPU 性能？ | `no policy / gpu_ext only / sched_ext only / xCoord` |
| RQ2 | 哪类协调策略在哪些场景有效？ | fault-aware CPU、CPU-aware eviction、FPRS、dual-actuator |
| RQ3 | shared map 与 sched_ext 的额外开销是多少？ | latency / throughput / scheduler stats |
| RQ4 | 框架能否支持多种策略组合？ | 单向协调 vs 双向协调 vs SLO loop |

### 6.2 五个评估场景

| Scenario | 配置 | 主要对照 | 指标 |
|----------|------|----------|------|
| 1. Multi-tenant LC+BE | llama.cpp / vLLM serving + training / FAISS | no policy / 单侧 / xCoord | LC P99、BE throughput、PCIe 利用率 |
| 2. UVM fault handler prioritization | 单租户重 UVM workload | CFS / blind sched_ext / GPU-aware sched_ext | fault latency、总吞吐 |
| 3. Pipeline workload | CPU preprocess → GPU infer → CPU postprocess | default vs pipeline-aware | 端到端延迟、GPU 利用率 |
| 4. Anti-thrashing | 3 tenants oversubscribe GPU memory | no policy / eviction-only / FPRS / dual-actuator | total fault rate、公平性、tail latency |
| 5. SLO-driven | LC with P99 target + BE background | no SLO / priority-only / feedback loop | SLO violation rate、BE degradation |

### 6.3 Profiling-First 工作流

| 阶段 | 做什么 | 目的 |
|------|--------|------|
| Phase A: Profile | `nsys`, `perf sched`, `chunk_trace`, `gpu_sched_trace` | 先量化 CPU 调度在 GPU 关键路径上的真实开销 |
| Phase B: Design | 根据 profiling 选用 targeted boost / fault-reactive / pipeline-aware | 避免无数据支撑的 blind heuristic |
| Phase C: Evaluate | 用同样工具验证瓶颈是否消失，再做端到端 benchmark | 证明改进来自机制，而不是偶然 workload 变化 |

### 6.4 可复用基础设施

- `workloads/llama.cpp/configs/bench.py`, `server_bench.py`
- `workloads/pytorch/configs/gnn.py`
- `scripts/run_trials.py`, `collect_results.py`
- `workloads/cleanup_gpu.py`

---

## 7. 执行计划与里程碑

| Phase | 周期 | 目标 | Deliverable |
|------|------|------|-------------|
| Phase 1 | 1-2 周 | GPU-side shared map integration | `gpu_state_map` / `uvm_worker_pids` 端到端可读 |
| Phase 2 | 2-3 周 | `sched_ext` GPU-aware scheduler 原型 | 双向共享框架打通 |
| Phase 3 | 2-3 周 | 4 类协调策略实现 | fault-aware CPU、CPU-aware eviction、anti-thrashing、SLO loop |
| Phase 4 | 2-3 周 | 全面评估 | 5 scenarios、ablation、overhead、sensitivity |
| Phase 5 | 2-3 周 | 论文撰写 | 可投稿 draft |

| Week | Milestone | Deliverable |
|------|-----------|-------------|
| 1-2 | GPU shared map | LFU/shared-map 示例 + pinned map |
| 3-5 | sched_ext 原型 | GPU-aware scheduler + 双向通信验证 |
| 6-8 | 协调策略完成 | 4 种策略可独立开关 |
| 9-10 | 评估完成 | scenario 数据 + overhead 图表 |
| 11 | draft 完成 | 可投稿论文初稿 |

---

## 8. 风险、Lessons Learned 与 Fallback

### 8.1 风险

| 风险 | 当前认识 | 缓解 |
|------|----------|------|
| sched_ext 热路径开销过高 | 对 FAISS / barrier workload 可能灾难性 | 非 GPU 线程保留 local fast path，只做 targeted boost |
| 改善幅度不足以支撑论文 | FPRS 目前仅 ~5%，且贡献归因不纯 | 分离 UVM worker boost 与 BE throttle，转向更强执行器 |
| 共享状态陈旧 | `fault_rate` 若不衰减会误导控制器 | staleness timeout + decay |
| workload 稳定性 / OOM | 120B/双 UVM 场景容易崩溃 | 固定启动顺序、清 GPU、避免 RAM 上限组合 |
| 结论被错误 instrumentation 污染 | nsys 曾把 tracing artifact 误读成机制限制 | 每个 hook / 线程模型先看 stat counter 再下结论 |

### 8.2 Lessons Learned

| 主题 | 结论 |
|------|------|
| sched_ext 开发 | `SCX_ENUM_INIT` 必须在 `open()` 后调用；`foo.bpf.o` 对应 skeleton 结构体是 `foo_bpf` |
| toolchain | clang 18 不适合 UEI 宏；直接 libbpf API 更稳 |
| map 复用 | `bpf_map__reuse_fd()` 必须在 `open()` 后、`load()` 前调用 |
| UVM worker 模型 | UVM BH 是 `SCHED_OTHER` kthread，可被 sched_ext boost；`taskset` 只影响用户线程 |
| profiling 解释 | nsys 零 sched events 常常是 tracing scope 问题，不是线程不存在 |
| 实验环境 | 不要用 `sudo bash script.sh` 跑整套流程；120B 前必须 `cleanup_gpu.py`，否则极易 OOM |

### 8.3 Fallback

| 方案 | 定位 |
|------|------|
| 单向 GPU-aware sched_ext | 保留 “GPU memory-aware CPU scheduling” 主线，工程量最低 |
| LLM-focused gpu_ext | 若跨子系统故事不够强，回退到更窄但更可控的 GPU 论文方向 |

---

## 9. 工程量

| 组件 | 实际 LOC | 实际时间 | 说明 |
|------|---------|---------|------|
| `shared_maps.h` | 48 | 0.2 天 | 共享状态定义 |
| `eviction_lfu_xcoord.bpf.c` | 313 | 0.5 天 | GPU map 写入 + worker 跟踪 |
| `eviction_lfu_xcoord.c` | 252 | 0.5 天 | loader + pin map |
| `sched_gpu_baseline.bpf.c` | 180 | 0.5 天 | sched_ext BPF |
| `sched_gpu_baseline.c` | 214 | 0.5 天 | loader + map reuse |
| `poc1_xcoord_scheduling.sh` | 274 | 0.3 天 | 自动化实验脚本 |
| 调试迭代 | — | 1.0 天 | 编译 / SCX / worker PID / OOM 修复 |
| **总计** | **1281** | **~3.5 天** | POC-1 代码完成 |

| 阶段 | 原始估计 | 当前状态 |
|------|---------|---------|
| Phase 1-2（POC） | 4-6 周 | ✅ 约 1 周完成 |
| Phase 3（策略实现） | 2-3 周 | ⏳ 需基于 profiling / FPRS / QoS 重新收敛 |
| Phase 4（完整评估） | 2-3 周 | ⏳ |
| Phase 5（论文撰写） | 2-3 周 | ⏳ |

---

## 10. MSched 启发的改进

| 改进 | 核心做法 | 优先级 | 预期价值 |
|------|----------|--------|----------|
| 前瞻性信号 | 在 `gpu_state_map` 增加 `eviction_pressure/working_set_chunks/migration_pending`，替代纯 `fault_rate` | 高 | 在 fault storm 前提前调度 |
| 线程分类 | 区分 fault handler / app thread / migration thread | 中 | 更精准 boost，减少误 boost |
| 双向协调 | `cpu_running_pids` 驱动 eviction protection | 中 | 保护正在 CPU 上运行进程的 GPU 页 |
| Pipeline-aware boosting | `migration_pending` 时 boost 整个 PID 的相关线程 | 中 | 减少 DMA/TLB 控制面 bubble |
| 在线访存模式学习 | BPF 在线记录 chunk pattern 预测 working set | 远期 | 从 reactive 走向 proactive |

- MSched 证明了 GPU 内部 memory scheduling 的价值；xCoord 的位置不是替代它，而是补上 **CPU scheduler 不知道 GPU memory pressure** 这一缺口。

---

## Appendix: 原始数据位置

- 原始 plan 文档: `docs/xcoord_plan.md` (2475 行完整历史)
- 旧版 plan: `docs/xcoord/xcoord_plan_old_20260228.md`
- CPU 侧策略: `extension/sched_gpu_{baseline,minimal,serving,xcoord,xcoord_noad,coord}.bpf.c`
- GPU 侧策略: `extension/prefetch_always_max_{xcoord,qos}.bpf.c`
- Shared maps 定义: `extension/shared_maps.h`
- Motivation 数据: `scripts/sched/`, `scripts/xcoord/results/`
- BPF CO-RE findings: `docs/xcoord/bpf_core_access_findings.md`
- MSched 分析: `docs/reference/msched_analysis.md`
- nsys profiling: `/tmp/xcoord_profiling/`
