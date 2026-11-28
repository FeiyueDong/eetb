# 分析算法来源与许可

本文件记录第五阶段（时域统计、强度指标、频谱与反应谱）的实现来源。结论先行：所有数值算法均按公开公式与参考实现的行为**重新编写**，没有复制 REST 或 MSL 的源码；参考只用于确认定义、归一化方式、边界条件和数值容差。

## 参考版本

| 项目 | 仓库 | 固定提交 | 参考文件 |
| --- | --- | --- | --- |
| REST | <https://github.com/FeiyueDong/REST> | `ff483351ceabd68ef60a92866c73b44a8f6ced8b` | `src/rest_algorithm/intensity_measures/intensity_measures/intensity_measures.hpp`、`src/rest_algorithm/preprocess/internal/signal_processing.cpp/.hpp`、`src/rest_dynamics/src/response/response_spectrum.cpp`、`src/rest_dynamics/src/response/spectrum_foh.cpp`、`src/internal/response/spectrum_foh.hpp`、`include/rest_dynamics/response/response_spectrum.hpp` |
| MSL | <https://github.com/FeiyueDong/MSL> | `03513d44e9c185597589534569aac6ce27ad290a` | `include/msl/signal/fft.hpp`、`include/msl/signal/power_spectral_density.hpp`、`include/msl/signal/window.hpp`、`include/msl/integral/trapz.hpp`、`include/msl/signal/detrend.hpp` |
| REST GUI | 同上 | 同上 | `src/rest_gui/visualization/` 的坐标刻度、抽稀与交互取舍（仅界面经验，未使用其代码） |

## 参考到的定义与约定

- Arias 强度 `IA = π/(2g)·∫a²dt`。REST 按其记录单位分别取 9.81/981/9810；本项目在临时 SI 数据上统一取 `g₀ = 9.80665 m/s²`。
- 显著持时：沿累积平方加速度能量曲线找 5%、75%、95% 的位置，在相邻采样点之间线性插值。总能量为零时不给出持时，不返回 0。
- CAV：`∫|a|dt`，累积曲线从零开始。REST 的 SED 使用矩形和；本项目统一使用梯形积分。
- 积分：REST 的顺序为“梯形积分 → 一阶去趋势 → 再积分 → 去趋势”，对应本项目的 `trapezoid-detrend`；`trapezoid` 为不做去趋势的零初值积分。
- 频率轴：`L/2+1` 个单边频点，`df = fs/L`。REST 的 “amplitude” 是未归一化的 `|X[k]|`；本项目按文档区分 FFT 模值、单边幅值谱（`fold·|X|/Σw`）与 FAS（`dt·|X|`）。
- Welch：`K = 1 + floor((N-M)/(M-O))`，`P[k] = fold·|FFT(w·x)|² / (K·fs·Σw²)`。REST 无逐段去均值；本项目按文档默认逐段去均值，并保留开关。
- 窗：对称定义（分母 `n-1`）；Hann、Hamming、Blackman、矩形，长度 1 时取 1.0。
- FOH 反应谱：4×4 增广矩阵指数 `A(0,1)=scale·dt`、`A(1,0)=-ω(ω/scale)·dt`、`A(1,1)=-2ζω·dt`、`A(1,2)=-dt`、`A(2,3)=1`，`scale=max(1,ω)`；状态为 `[scale·u, v]`，`Sa` 是真实绝对加速度 `-ω²u-2ζωv`，不是 `ω²Sd`；峰值只在采样边界取，`T=0` 时 `Sa=PSA=PGA`。

## 本项目的实现差异

- 不使用 REST 的异常层级、多通道求解器、结构模型或结果容器；Core 只处理单条 `Trace`。
- 不使用 MSL 的矩阵封装；FFT 直接调用 Eigen 的 `unsupported/Eigen/FFT`（内置 kissfft 后端，正向不缩放、逆向乘 `1/n`）。
- 所有输入校验、单位换算、诊断文本、结果结构与序列化均为本项目代码。
- FOH 系数经过独立的 RK4 参考程序逐项核对（相对误差约 `1e-16`）；Welch 单边 PSD 与 NumPy/SciPy 的同参数结果逐点比对（最大相对差 `3.2e-14`）。

## 许可结论

- **REST**：仓库 `LICENSE` 明确说明它本身不是授权文件（计划中的 MIT 未落地），前端为 GPL-3.0-only，所参考的文件没有逐文件许可头。因此仅参考算法与行为，不复制任何代码、注释或测试。
- **MSL**：README 声称 MIT，但仓库中没有 MIT `LICENSE` 文件，头文件标注 “Copyright 2025-2026 ... All Rights Reserved”。同样仅作行为参考。
- **Eigen**：MPL-2.0；其 FFT 实现封装 kissfft（BSD-3-Clause）。按 Eigen 的方式使用头文件与模板实例。
- **nlohmann/json**：MIT，已有依赖，用于结果与预设的 JSON 输出。

上游若后续补充明确许可，可在保持本文件记录的前提下重新评估是否直接复用实现。
