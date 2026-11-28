# GMTools 第四阶段验收记录

2026-11-28，Linux x86_64。以下结果对应基线 `17d91bd` 上的第四阶段工作树。开发范围与完成定义见[第四阶段开发文档](../6.GMTOOLS_PHASE4_DEVELOPMENT.md)。

本阶段新增界面国际化、程序设置、转换预设与顺序批量转换。Core 与 CLI 已在关闭标准 miniSEED 的精简配置中构建并运行检查；GUI 完成编译、离屏启动、翻译加载与预设存取的自动检查，但没有显示服务器，目视布局、鼠标交互和主题配色未验收。

## 构建与入口

M1–M6 主体代码为 **IMPLEMENTED**；精简配置的 Core、CLI 与 GUI 为 **COMPILED**。完整配置（`--enable_mseed=y`）为 **BLOCKED**：当前机器没有 libmseed 头文件与库，第三阶段使用的 `/tmp/gmtools-libmseed-sdk` 已不存在。

本次使用 xmake 3.1.2、GCC 16.2.1、Qt 6.12.0（`/home/yue/Qt/6.12.0/gcc_64`），Qt Linguist 工具位于 `/usr/lib/qt6/bin`。实际命令：

```sh
xmake f -y --enable_mseed=n --build_gui=y -o build/xmake-phase4
env XMAKE_GLOBALDIR=/tmp/gmtools-xmake-global XMAKE_CONFIGDIR="$PWD/build/xmake-phase4-config" \
  XMAKE_SKIP_HISTORY=y xmake build -j 8
```

产物位于 `build/xmake-phase4/linux/x86_64/release/`，包括 `gmtools`、`gmtools_gui` 和 `translations/gmtools_zh_CN.qm`。默认 xmake 全局目录不可写时会出现 `cannot create filelock for package(nlohmann_json)`，因此显式指定 `/tmp` 下的全局目录。

翻译源文件更新与静态检查：

```sh
/usr/lib/qt6/bin/lupdate -no-obsolete src -ts translations/gmtools_zh_CN.ts
/usr/lib/qt6/bin/lrelease translations/gmtools_zh_CN.ts -qm /tmp/gmtools_zh_CN.qm
```

`lupdate` 提取 300 条（`MainWindow` 107、`BatchDialog` 88、`ParseDialog` 49、`PresetDialog` 27、`SettingsDialog` 11、`OverrideFields` 8、`TracePlot` 6、`TraceInfo` 3、`ExportOptions` 1）。逐条翻译后 `lrelease` 报告 `Generated 300 translation(s) (300 finished and 0 unfinished)`。`ParseDialog`、`OverrideFields`、`TracePlot`、`TraceInfo` 与 `ExportOptions` 使用与提取上下文一致的显式上下文查询，运行时不会退回英文。构建的 `after_build` 步骤会把同一 `.ts` 编译到产物目录的 `translations/` 下；缺少 `lrelease` 时 GUI 构建失败，CLI 构建不受影响。

## 分项结果

| 用例 | 状态 | 本轮实际核查 |
|---|---|---|
| T0 构建与回归 | PASS（精简配置） | Core、CLI、GUI 编译通过；`scripts/verify_core_formats.py` 对 `example/` 的四份真实样本执行，Helena/Trinidad、台湾 SAC、legacy v1 与 provenance 三项 PASS；花莲 miniSEED、武汉标定、Modified 分段三项因缺 libmseed 或未提供武汉记录而 SKIPPED。 |
| T1 双语与设置 | PASS（离屏） | 翻译条目 300/300 完成。离屏测试程序加载产物中的 `gmtools_zh_CN.qm`，`QCoreApplication::translate` 返回“打开文件…”“导入通道”“批量转换”“采样”，并成功构造 `MainWindow`、`BatchDialog`、`PresetDialog`、`SettingsDialog`，事件循环正常退出。`SettingsDialog` 读写 `ui/language`、`ui/theme`、`preferences/rememberDirectories`、`preferences/rememberConversionSettings`；语言与主题按设计在重启后生效。实际窗口的双语布局与主题配色未目视核验。 |
| T2 预设加载与拒绝 | PASS | 未知字段报 `preset.read.metadata: unknown field`；`manual` 缺系数与单位报 `preset.read: manual calibration requires calibration_factor and calibration_unit`；两者退出码均为 1 且不产生输出。`--output-unit keep` 可清除预设的输出单位。 |
| T3 Profile 兼容 | PASS | `gmtools info example/Trinidad.dat --profile example/trinidad.profile.json` 仍读出 1 条、2141 点、dt 0.01 s、量 acceleration、单位 g；Profile v1 未改动。 |
| T4 单文件预设与显式参数 | PASS | Helena AT2 以预设 `04-peer-at2.json` 与以 `--to at2 --output-unit g --quantity acceleration` 导出的 `helena_CH01.at2` 逐字节相同。 |
| T5 批量与单文件一致 | PASS | 同一 Helena 文件经 `batch` 与 `convert` 以相同参数导出的 `helena_CH01.at2` 逐字节相同。 |
| T6 失败隔离与通道筛选 | PASS | 目录批量中 AT2 成功、缺少 dt 的 CSV 失败（`PEER export requires dt`），其余继续，退出码 1，成功文件保留。`--select-quantity acceleration` 对纯数值 counts 输入报 `no channel with quantity acceleration was found among 1 channel(s)`；改 `--select-quantity counts --input-unit counts` 成功导出 1 个文件。 |
| T7 输出安全 | PASS | 已有目标无 `--overwrite` 报 `target already exists; choose a new path or enable overwrite`；加 `--overwrite` 成功。目标等于输入文件时即使加 `--overwrite` 也报 `target refers to the input file`，退出码 1。批次内两个同主干文件生成同一目标时先到者成功，后者报 `target is also produced by another file in this batch`。重复输入（`helena.at2` 与 `helena.at2` 的别名路径）被提示 `skipping duplicate input` 并只处理一次。 |
| T8 进度与安全停止 | NOT RUN | 未在运行中的批次上发送 `SIGINT`；退出码 `130` 与“完成当前文件后停止”只有代码路径，没有运行证据。 |
| T9 GUI 闭环 | PARTIAL（离屏） | `QT_QPA_PLATFORM=offscreen` 下主窗口与三个对话框构造、显示与退出无崩溃；中文字符串经产物 `.qm` 正确解析；预设目录在可写配置根下完成写入、列出、载入、拒绝覆盖与删除。无 X 服务器，截图、菜单点击、批量运行与主题配色为 NOT RUN。 |

## 离屏 GUI 检查

本机没有 X 服务器，GUI 只做了可控的离屏检查。测试程序用 `pkg-config` 的 Qt 6 链接 `libgmtools_core.a` 与相关 GUI 源文件，设置 `QT_QPA_PLATFORM=offscreen` 运行。

- 构造 `SettingsDialog`、`PresetDialog`、`BatchDialog` 并进入事件循环 300 ms，正常退出。
- 加载产物中的 `translations/gmtools_zh_CN.qm` 后，`gmtools::gui::MainWindow/ParseDialog/BatchDialog/OverrideFields` 上下文的英文源串分别解析为“打开文件…”“导入通道”“批量转换”“采样”。
- 以可写的 `XDG_CONFIG_HOME` 运行预设存取：写入 `presets/smoke-test.json`、列出 1 个文件、回读名称/目标格式/表头/精度、再次写入被拒绝、删除成功。

文件沙箱不允许写入 `$HOME/.config`，因此预设测试改用临时 `XDG_CONFIG_HOME`。这验证了目录创建、原子写入与读取，但没有改变程序的默认配置位置。

## 运行证据

以下命令均在 `GMTools/` 内执行，输出目录为当次新建。

```sh
B=build/xmake-phase4/linux/x86_64/release/gmtools

# 预设拒绝
$B convert src/helena.at2 -o out --preset bad.json         # exit 1, unknown field
$B convert src/helena.at2 -o out --preset bad-manual.json  # exit 1, manual calibration incomplete

# 预设与显式参数一致
$B convert src/helena.at2 -o t4a --preset example/presets/04-peer-at2.json
$B convert src/helena.at2 -o t4b --to at2 --output-unit g --quantity acceleration
cmp t4a/helena_CH01.at2 t4b/helena_CH01.at2

# 批量失败隔离
$B batch --input-dir src -o out --preset example/presets/04-peer-at2.json
# helena.at2: succeeded, 1 file(s) written
# ramp.csv: failed (PEER export requires dt)
# Batch: 1 succeeded, 0 partial, 1 failed, 0 cancelled; 1 file(s) written

# 通道筛选
$B batch counts.txt -o out --to csv --select-quantity acceleration     # exit 1, no match
$B batch counts.txt -o out --to csv --select-quantity counts --input-unit counts  # exit 0

# 输出保护
$B convert src/helena.at2 -o src/helena.at2 --to at2 --overwrite        # exit 1, refers to input
$B convert src/helena.at2 -o t6b --to at2 --output-unit g               # exit 0
$B convert src/helena.at2 -o t6b --to at2 --output-unit g               # exit 1, already exists
$B convert src/helena.at2 -o t6b --to at2 --output-unit g --overwrite   # exit 0
```

Helena 输入取自 `build/acceptance/verified-1795459681983431539/t1-csv-to-peer/RSN1_HELENA_A_A-HMC180_CH01_amplitude.at2`（5093 点，dt 0.01 s）。仓库原有 `samples/` 未被跟踪且当前不存在；`example/` 的四份真实样本仍可用于回归。

## 已知限制与未完成项

- **GUI 目视验收**：无显示环境，语言、主题、预设对话框与批量窗口的布局和鼠标交互仍未目视核验。离屏测试已覆盖构造、翻译加载与预设存取；需在具备 X11/Wayland 的机器上补做截图与完整点击流程。
- **Windows**：未构建，未验收。
- **完整 miniSEED 配置**：缺 libmseed，BLOCKED；第四阶段没有改动 miniSEED 代码路径，但完整配置的回归仍待执行。
- **批量停止**：`SIGINT` 与 GUI 的“正在停止”路径未运行；部分发布状态也未构造。
- **真实 Modified / K-NET 批量**：无对应样本，未执行文件标定与辅助通道保留的批量回归。
- `example/presets/` 的示例预设没有安装到 GUI 的预设目录，需要在预设管理中用 **Import…** 引入后才能在内置列表中看到。
- 批量输入目录需显式写 `--input-dir`；把目录作为位置参数会按普通文件处理并报 `expected a readable regular file`。
- 通用文本在批量与预设路径下仍会向标准错误打印解析建议（既有行为），不影响结果。
