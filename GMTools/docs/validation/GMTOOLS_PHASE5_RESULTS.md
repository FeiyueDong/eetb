# 第五阶段验收记录

范围：第四阶段遗留问题修复（M0），以及时域统计、强度指标、频谱、反应谱与 GUI/CLI 分析入口（M1–M8）。实施要求见 [`7.GMTOOLS_PHASE5_DEVELOPMENT.md`](../7.GMTOOLS_PHASE5_DEVELOPMENT.md)。

状态标记：`IMPLEMENTED` 代码已实现；`COMPILED` 已通过当前配置编译；`PASS` 有运行证据；`PARTIAL` 部分完成或仅有间接证据；`NOT RUN` 未运行；`SKIPPED` 缺少样本或依赖；`BLOCKED` 环境阻塞。

## 基线与工作树

- 基线提交：`1689d5f feat: finish P4`。
- 本阶段新增与修改的文件较多，均未提交；`git status` 中包含第四阶段遗留的文档重命名（用户改动），未做批量重写。
- 依赖：Eigen `3.5`（系统 `/usr/include/eigen3`，xmake 以 `add_requires("eigen >=3.3", {system = true})` 解析）；nlohmann/json 沿用既有依赖。系统没有 libmseed，本阶段验收使用 `--enable_mseed=n` 的精简配置。

## 构建与入口

```sh
xmake f -y --enable_mseed=n --build_gui=y -o build/xmake-phase4
xmake build -j 8
```

产物：`build/xmake-phase4/linux/x86_64/release/{gmtools, gmtools_gui, translations/gmtools_zh_CN.qm}`。`xmake install -o build/install-test` 会安装 `bin/gmtools`、`bin/gmtools_gui` 与 `bin/translations/gmtools_zh_CN.qm`。

新增/修改的源码：

- Core：`include/gmtools/analysis.hpp`、`src/analysis/{statistics,intensity,spectrum,response_spectrum,result_io}.cpp`。
- CLI：`src/cli/main.cpp` 新增 `analyze` 与 M0 的筛选/预设覆盖。
- GUI：`src/gui/analysis_plot.{hpp,cpp}`、`src/gui/analysis_dialog.{hpp,cpp}`、`src/gui/main_window.{hpp,cpp}` 的分析入口。
- 修复：`src/core/{detail.hpp,files.cpp,batch.cpp}` 的输出规划与发布、`src/gui/main_window.cpp` 的预设事务、`src/gui/batch_dialog.cpp` 的进度与预检显示、`xmake.lua` 依赖与安装规则、`translations/gmtools_zh_CN.ts`。

## 验收项

| 编号 | 内容 | 状态 | 证据 |
| --- | --- | --- | --- |
| T0 | 多通道发布、通道筛选、预设状态 | PASS | 三通道 CSV 单文件与批量均写出 3 个文件；筛选无匹配时零输出；目标冲突/源文件保护按预期拒绝 |
| T1 | 构建与既有转换回归 | PARTIAL | 精简配置下 Core/CLI/GUI 编译通过；`formats/info/convert/batch` 冒烟通过；`verify_core_formats.py` 3 PASS + 3 SKIPPED；完整 miniSEED 配置 BLOCKED |
| T2 | 时域统计 | PASS | 合成向量与 Helena 实录，见下 |
| T3 | 强度指标与积分 | PARTIAL | 常值加速度解析值通过；`trapezoid-detrend` 只做了冒烟，无独立参考数值 |
| T4 | FFT、幅值谱与 FAS | PASS | 整周期正弦的峰值 bin、模值、幅值与 FAS 全部符合解析值 |
| T5 | Welch PSD | PASS | 与 NumPy 同参数结果逐点比对，最大相对差 `3.2e-14` |
| T6 | FOH 反应谱 | PASS | 与独立 RK4 参考程序对照，相对差约 `1e-16`；零输入、`T=0`、PGA 边界通过 |
| T7 | `analyze` 与导出 | PASS | 五类输出、通道选择、参数校验、覆盖保护、退出码均有运行证据 |
| T8 | GUI 数据分析窗口 | PARTIAL | 离屏构造、计算、导出路径与中英文渲染通过；真实 X11 交互、主题切换、取消与关闭流程 NOT RUN |

## 运行证据

### 统计与强度（合成数据）

- `{1,-2,3,-3}`、`dt=0.5`：min `-3`、max `3`、绝对峰值 `3`、带符号峰值 `3`（首个绝对峰值在第 3 点，取该点的符号）、峰值点 `3`（对外 1 基）、峰值时刻 `1.0 s`、均值 `-0.25`、总体标准差 `2.38485`、RMS `2.39792`、时程长度 `1.5 s`。
- 常值加速度 `A=2 m/s²`、`N=1001`、`dt=0.01`（`D=10 s`）：PGA `2`、CAV `20 m/s`、IA `6.40707 m/s`、D5–75 `7 s`、D5–95 `9 s`。梯形积分对常值精确。
- Helena AT2（5093 点、`dt=0.01`）：PGA `1.576521957325 m/s²`、CAV `1.2528143736 m/s`、IA `0.0695722075 m/s`、D5–75 `1.20648 s`、D5–95 `2.4116 s`。

### 频谱

- `N=1024`、`A=2`、`m=50`、`dt=0.01` 的整周期正弦：峰值频率 `4.8828125 Hz`、FFT 模值 `1024`、单边幅值 `2`、FAS `10.24`。
- Welch PSD（`N=20000`、`M=4096`、overlap 0.5、对称 Hann、逐段去均值）：CLI 头部显示 `segments 8`、`window hann`、`fft_length 4096`；与 NumPy/SciPy 同参数结果的最大相对差 `3.2e-14`。
- Helena 单边幅值谱：`5093` 点、`1` 段、矩形窗、频率分辨率 `0.0196348 Hz`、峰值频率 `2.47398 Hz`。

### 反应谱

- 与独立 RK4 程序（每个采样区间 100 子步、forcing 线性插值、峰值只在采样边界取）对照：`T=0.37 s`、`ζ=0.05`、`3.1 Hz` 正弦、1000 点，`Sd=0.014795405280293409 m`、`Sv=0.274500555610036 m/s`，差小于 `1e-15`。
- 零输入：五种谱全为 0；`T=0`：`Sa=PSA=PGA=3`（`A=3` 的常值输入）。
- Helena：默认 251 个周期、5% 阻尼，`Sa` 峰值约 `4.9 m/s²`（周期约 `0.2 s`），与 `PGA=1.58 m/s²` 的放大关系合理。

### CLI

```sh
gmtools analyze three.csv --channel 2 --first-line 2 --layout columnar --time-column 1 --type stats
gmtools analyze helena.at2 --type intensity
gmtools analyze helena.at2 --type psd -o out/psd.csv
gmtools analyze helena.at2 --type response -o out/response.csv
```

- 频谱/反应谱缺少 `-o` 时退出码 `2`；统计/强度带 `-o` 同样报参数错误。
- 已存在目标且无 `--overwrite` 时退出码 `1`，文件不变；成功时只输出最终路径。
- 多通道未指定 `--channel` 时报 `the file contains N channels; select one with --channel N`。
- CSV 首行 `# GMTools analysis 1`，次行为 JSON 头；反应谱列 `period_s,sd_m,sv_mps,sa_mps2,psv_mps,psa_mps2`；PSD 单位 `g^2/Hz`。GUI 导出工程参数时把统计与强度合并为一个 JSON 对象（`statistics`、`intensity` 两个键）。

### GUI（离屏渲染）

`QT_QPA_PLATFORM=offscreen` 下构造 `AnalysisDialog`、点击计算并渲染四个标签页，输出 `build/phase5-tests/{time,spectrum,response,parameters}.png` 与对应的 `_zh` 版本。检查结果：

- 时程页显示波形、峰值与位置；频谱页曲线、网格、刻度与摘要一致；反应谱页 251 点曲线；工程参数页 20 行指标，列宽自适应。
- 中文界面完整：时程/频谱/反应谱/工程参数、类型/窗函数/分段长度/重叠比例/FFT 长度、导出数据…/保存图片… 等均为中文。
- 对数频率轴（0.02–20 Hz 的十进位刻度）与线性轴渲染均正常；轮子缩放/拖动的事件处理已实现但离屏平台无法模拟，`NOT RUN`。
- 修复了两处视觉问题：空状态文字曾覆盖已有曲线；参数表首列被截断（改为按内容调整列宽）。

翻译源文件共 419 条，`lrelease` 报告 `419 finished and 0 unfinished`。

## 已知限制与未验证项

- 完整 miniSEED 配置需要 libmseed 3.5.3+，当前机器没有该库，相关回归为 `BLOCKED`；精简配置下 miniSEED 读写按既有行为报“适配器不可用”。
- Windows/MSVC 未构建，`NOT RUN`。
- 真实 Modified miniSEED 与 K-NET 数据的分析链路未运行（无本地样本），`SKIPPED`；counts 输入只验证了“强度不可用”的诊断，未验证真实标定。
- 反应谱与 Welch 的取消路径、GUI 关闭与主题切换、真实 X11 下的滚轮/拖动交互未运行，`NOT RUN`；离屏平台不支持窗口系统事件。
- `trapezoid-detrend` 仅有冒烟运行，未与 REST 的数值结果逐点比对。
- 批量分析不在本阶段范围；分析结果不进入转换预设 schema。

## 复现方式

```sh
# 构建（精简配置）
xmake f -y --enable_mseed=n --build_gui=y -o build/xmake-phase4 && xmake build -j 8

# 核心数学自检（源码在 build/phase5-tests/，未提交）
g++ -std=c++20 -Iinclude -Isrc -I/usr/include/eigen3 build/phase5-tests/core_math_test.cpp \
  -Lbuild/xmake-phase4/linux/x86_64/release -lgmtools_core -o /tmp/core_math_test && /tmp/core_math_test

# 离屏截图（需要 Qt SDK 的 moc 与 libQt6*，链接同一 gmtools_core）
QT_QPA_PLATFORM=offscreen ./shot helena.at2

# 格式回归
python3 scripts/verify_core_formats.py --gmtools build/xmake-phase4/linux/x86_64/release/gmtools \
  --examples example --output build/phase5-tests/verify
```
