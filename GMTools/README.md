# GMTools

GMTools 将一个文件中的地震动记录读成独立通道。可以检查样本、采样间隔和单位，再将所选通道分别写入独立文件。命令行和 Qt 界面使用同一套解析、校验、换算和导出代码。

## 构建

需要支持 C++20 的编译器、CMake 3.20 或更新版本，以及提供 CMake 配置的 nlohmann/json 3.10 或更新版本。构建 GUI 还需要 Qt 6.2 或更新版本的 Widgets 和 Concurrent 开发包。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

生成 `gmtools` 和 `gmtools_gui`。使用 `-DGMTOOLS_BUILD_GUI=OFF` 可以仅构建 CLI。命令示例采用单配置构建器的输出路径。Visual Studio 等多配置构建器通常将产物放入 `build/Release/`。

miniSEED 是可选功能。安装带 CMake 配置的 libmseed 3.5.3 或兼容的 3.x 版本，然后在配置时添加 `-DGMTOOLS_ENABLE_MSEED=ON`。如依赖安装在非系统路径，同时添加 `-DCMAKE_PREFIX_PATH=/path/to/libmseed`。分发包含 libmseed 的程序时需附带依赖的 Apache 2.0 许可及适用声明。[EarthScope libmseed](https://github.com/EarthScope/libmseed/tree/v3.5.3)

## 格式与限制

| 格式 | 读取 | 写出 | 说明 |
|---|---|---|---|
| TXT / DAT | 是 | 是 | UTF-8/ASCII 数值文本。通用表头可通过指定首数据行跳过，多个数值一行且布局不清楚时需明确布局。 |
| CSV / TSV | 是 | 是 | 默认按列分通道，可带简单列名。写出一列幅值和一个列名。 |
| PEER NGA AT2 | 是 | 是 | 识别常见加速度头段及 `NPTS`、`DT`。Helena 样本已通过 AT2↔CSV 回读。写出以 g 保存。样式参见 [PEER 报告附录](https://peer.berkeley.edu/sites/default/files/publications/2017_09_stewart_9.10.18.pdf)。 |
| NIED K-NET / KiK-net ASCII | 实验性 | — | 单文件单分量，通过头段辨认格式，按 Scale Factor 把有符号整数 counts 标定为 cm/s²。正式样本验收待完成。[NIED ASCII 格式](https://www.kyoshin.bosai.go.jp/en/knetascii/) |
| SAC Binary | 实验性 | 实验性 | 读取两种字节序的 v6/v7 等间隔 ITIME 记录。输出为小端 v7，时间头段有双精度 footer，波形为 float32。已核验小端 v6→v7。[SAC Binary 规范](https://ds.iris.edu/files/sac-manual/manual/file_format.html) |
| miniSEED 2 / 3 | 实验性，可选 | 实验性，可选 | v2/v3 共用读取入口，当前允许 INT16、INT32、FLOAT32、FLOAT64、Steim-1、Steim-2 波形编码。输出使用 FLOAT64，`mseed` 写 v3，`mseed2` 写受标识和时间表示约束的 v2。已核验花莲的 v2 FLOAT32→v3 FLOAT64。[EarthScope libmseed](https://github.com/EarthScope/libmseed/tree/v3.5.3) |

时间列以秒为单位，需要指定列号。解析会核查单调性和整条时间轴的等间隔性。只有一个样本时，时间列只能给出相对起点。普通 TXT、DAT、CSV 和 TSV 输出只保存幅值，采样信息、单位及专有元数据若无法保存，会给出提示。文本读取当前不支持复杂引号 CSV、GBK、Fortran `D` 指数或固定宽度布局。

PEER 头段缺少 NPTS 或与实际数量冲突时给出警告并保留实际样本。缺少或非法的 DT 会阻止 AT2 解析，可切换到通用文本并指定数据起始行来解释该文件。SAC 的 `IDEP` 可读写加速度、速度和位移，工程单位需要另行确认。能同时表示时，SAC 导出保留相对起点 B 和绝对首样本时间。`SCALE` 作为元数据保存，样本读入时保持原值。

miniSEED 以文件中的记录顺序处理。同一源标识在时间间隔、重叠、乱序或采样参数变化时拆成独立 Trace。仅在编码时间精度和采样间隔都允许时合并相邻片段，接受非零时间残差时会报告。样本值维持输入的数值，单位缺失时保留未知。输出需要已知绝对起始时刻、采样间隔和符合对应版本要求的 FDSN 源标识。对于带台网、台站和三字符分量的 SAC，也可以从这些字段组成标识。miniSEED 2 写出还会核查编码后的采样间隔和时间戳。花莲样本的两字符通道标签超出当前 v2 Writer 的源标识约束，可导出 v3。REST 的专有 miniSEED 变体尚未接入。

K-NET ASCII 读取从 `Memo.` 后按行展开 count，以头段 `Sampling Freq(Hz)` 确定 dt。首样本为 JST 触发时刻 `Record Time` 减 15 秒并转换为 UTC。通道编号、原始方向与仪器系数记入元数据。CSV 默认保持标定后的 cm/s²，AT2 再换算为 g。显式 `--numeric-factor` 在仪器标定后单独应用。冲突的输入单位或采样设置在标准模式下报错。

## CLI

```sh
# 查看当前构建提供的格式及读写能力
./build/gmtools formats

# 查看 AT2 的采样信息、单位、通道和诊断
./build/gmtools info record.at2

# 从时间 + 两个通道的 CSV 分别导出两个文件
./build/gmtools convert record.csv --time-column 1 --data-columns 2,3 \
  --to csv -o out/

# 将第 5 行起的多列连续样本解释为一条加速度记录
./build/gmtools convert record.txt --format text --first-line 5 --layout sequential \
  --dt 0.01 --input-unit cm/s2 --to at2 -o out/

# Trinidad 样本的头段注明时间列使用秒、加速度列使用 g
./build/gmtools convert example/Trinidad.dat --first-line 6 --layout columnar \
  --time-column 1 --input-unit g --to csv -o out/

# 复用 Trinidad 的文本解释参数
./build/gmtools info example/Trinidad.dat --profile example/trinidad.profile.json

# 将单路 NIED ASCII 波形按头段标定后写为 AT2
./build/gmtools convert /path/to/station.NS --format knet-ascii --to at2 -o out/

# 显式指定输出单位，选择第二条通道
./build/gmtools convert channels.csv --channel 2 --input-unit g --output-unit m/s2 \
  --to txt -o selected.txt
```

行号、列号和通道编号都从 1 开始。首末行均包含在数据范围内。`--dt` 和 `--fs` 互斥。`--input-unit` 解释或覆盖输入单位，保留现有样本值。`--output-unit` 在导出时换算加速度，使用 `1 g = 9.80665 m/s² = 980.665 cm/s²`。已知单位、采样间隔或物理量被覆盖时可以在诊断及来源字段中查看相应记录。`--numeric-factor` 明确指定样本缩放，与解释输入单位及导出单位换算分别处理。

默认导出全部通道，文件名使用输入文件名和安全的通道名称或序号。`-o` 指定目录，单通道时也可指定与目标格式匹配的完整文件名。使用 `--overwrite` 才会允许替换已有输出。程序先核对目标路径，再暂存所有编码结果。若发布阶段发生文件系统错误，命令返回非零并列出已写出的文件，可据此处理部分发布。

## 文本解析配置

`--profile FILE` 可供 `info` 和 `convert` 使用。GUI 的普通文本导入窗口也可以载入或保存同一份 JSON。[Trinidad Profile 示例](example/trinidad.profile.json)

| JSON 字段 | 允许值 |
|---|---|
| `schema_version` | 必填，整数 `1`。 |
| `format` | `auto`、`text`、`dat`、`csv` 或 `tsv`。省略时自动检测。 |
| `text` | 可包含 `first_line`、`last_line`、`layout`、`delimiter`、`data_columns`、`time_column`、`numeric_factor`。布局为 `sequential` 或 `columnar`。分隔符为 `auto`、`whitespace`、`comma` 或 `tab`。列号从 1 开始，数据列用整数数组保存。 |
| `overrides` | 可包含 `dt`、`fs`、`quantity`、`unit`。`dt` 和 `fs` 互斥，量和单位沿用 CLI 的选项。 |

CLI 显式填写的值优先于 Profile。例如显式 `--fs` 替换 Profile 的 `dt`，与 Profile 不同的值会产生提示。GUI 的 **Load profile** 完整替换当前表单，包括清空配置中未设置的字段；加载后可以继续手动调整。读取标准文件时仍执行该格式的头段和标定校验。未知字段、重复键、非法值和未知版本会报错。Profile 只保存输入解释参数。

诸如 `Time[s]`、`Time (s)` 和 `Accel[g]` 的明确标签可用来提出导入建议。程序先核对完整数据区和时间网格。CLI 将建议写到标准错误，GUI 可通过 **Apply text suggestions** 应用。建议需要显式采用，多个候选时间列或不同列的加速度单位冲突时由用户选择。

## GUI

1. 点击 **Open file**、按 `Ctrl+O` 或拖入单个文件。解析窗口支持自动识别或手动选格式。
2. 对照编号原文检查数据起始行、布局、列号、时间列和采样间隔。通用文本可以选择 **Apply text suggestions**、**Load profile** 或 **Save profile**。用 **Preview parse** 查看通道数、样本数、单位和诊断，再点击 **Import channels**。修改表单后，旧预览会失效；参数未变化时直接复用成功的预览结果。
3. 左侧列出全部通道，点击通道可定位并高亮对应波形，勾选决定导出范围。右侧 **Summary / Metadata / Diagnostics** 分别展示参数、来源字段及导入诊断。波形横轴在已知 dt 时显示首样本起的经过时间，其他情况显示样本序号。大记录使用保留极值的概览包络绘图，导出仍使用完整样本。
4. 使用 **Edit channel parameters** 编辑名称、采样信息及输入单位。输入单位用于解释现有数值，导出区的输出单位才执行数值换算。K-NET 的额外 **Numeric factor** 在仪器标定后应用；标准格式的单位和采样约束仍由核心检查。
5. 在 **Export** 中选择目标格式、输出单位和目录，点击导出按钮或按 `Ctrl+Shift+E`。按钮显示勾选数量，未选通道时禁用。格式说明提前展示采样和元数据限制，结果窗口列出实际文件与诊断，并可打开输出目录。

读取、解析、波形概览计算与导出在后台执行；执行期间显示忙碌指示，禁止替换记录、编辑及关闭正在工作的窗口。打开新文件或重新解释文件时会重置覆盖开关，有未全部导出的通道编辑时会提示。窗口尺寸、面板比例及最近使用目录保存在本机 Qt 设置中。界面按启动时的系统配色选择浅色或深色主题。

模块职责、此次整理的问题及人工检查项目见[架构与界面整理记录](docs/ARCHITECTURE_AND_UI_REVIEW.md)。

## 验收状态

第一阶段在 Linux 完成了四份真实样本验收、文本与 AT2 回读、输出冲突和单位验证，并在隔离的 X11 虚拟显示上核验了当时的 GUI 工作流。SAC 和 miniSEED 的验收范围按版本、字节序及编码单独记录，两者仍为实验性。Windows 验收待完成。[样本清单与 T0–T7 验收记录](docs/validation/GMTOOLS_PHASE1_RESULTS.md)

第二阶段加入文本建议、Profile 与实验性 NIED ASCII 读取。正式波形样本、运行回归和界面操作待验收。PEER VT2/DT2、COSMOS 和 USGS SMC 的调查范围见[第二阶段样本与开发记录](docs/validation/GMTOOLS_PHASE2_RESULTS.md)。

本次架构与界面整理只做静态检查和 Linux 构建，未运行测试、启动 GUI 或执行视觉验收。历史验收记录不代表新界面的运行验证。
