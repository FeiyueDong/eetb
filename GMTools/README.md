# GMTools

GMTools 将一个文件中的地震动记录读成独立通道。可以检查样本、采样间隔和单位，再将所选通道分别写入独立文件。命令行和 Qt 界面使用同一套解析、校验、换算和导出代码。

## 构建

需要支持 C++20 的编译器和 CMake 3.20 或更新版本。构建 GUI 还需要 Qt 6.2 或更新版本的 Widgets 开发包。

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
| SAC Binary | 实验性 | 实验性 | 读取两种字节序的 v6/v7 等间隔 ITIME 记录。输出为小端 v7，时间头段有双精度 footer，波形为 float32。已核验小端 v6→v7。[SAC Binary 规范](https://ds.iris.edu/files/sac-manual/manual/file_format.html) |
| miniSEED 2 / 3 | 实验性，可选 | 实验性，可选 | v2/v3 共用读取入口，当前允许 INT16、INT32、FLOAT32、FLOAT64、Steim-1、Steim-2 波形编码。输出使用 FLOAT64，`mseed` 写 v3，`mseed2` 写受标识和时间表示约束的 v2。已核验花莲的 v2 FLOAT32→v3 FLOAT64。[EarthScope libmseed](https://github.com/EarthScope/libmseed/tree/v3.5.3) |

时间列以秒为单位，需要指定列号。解析会核查单调性和整条时间轴的等间隔性。只有一个样本时，时间列只能给出相对起点。普通 TXT、DAT、CSV 和 TSV 输出只保存幅值，采样信息、单位及专有元数据若无法保存，会给出提示。文本读取当前不支持复杂引号 CSV、GBK、Fortran `D` 指数或固定宽度布局。

PEER 头段缺少 NPTS 或与实际数量冲突时给出警告并保留实际样本。缺少或非法的 DT 会阻止 AT2 解析，可切换到通用文本并指定数据起始行来解释该文件。SAC 的 `IDEP` 可读写加速度、速度和位移，工程单位需要另行确认。能同时表示时，SAC 导出保留相对起点 B 和绝对首样本时间。`SCALE` 作为元数据保存，样本读入时保持原值。

miniSEED 以文件中的记录顺序处理。同一源标识在时间间隔、重叠、乱序或采样参数变化时拆成独立 Trace。仅在编码时间精度和采样间隔都允许时合并相邻片段，接受非零时间残差时会报告。样本值维持输入的数值，单位缺失时保留未知。输出需要已知绝对起始时刻、采样间隔和符合对应版本要求的 FDSN 源标识。对于带台网、台站和三字符分量的 SAC，也可以从这些字段组成标识。miniSEED 2 写出还会核查编码后的采样间隔和时间戳。花莲样本的两字符通道标签超出当前 v2 Writer 的源标识约束，可导出 v3。REST 的专有 miniSEED 变体尚未接入。

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

# 显式指定输出单位，选择第二条通道
./build/gmtools convert channels.csv --channel 2 --input-unit g --output-unit m/s2 \
  --to txt -o selected.txt
```

行号、列号和通道编号都从 1 开始。首末行均包含在数据范围内。`--dt` 和 `--fs` 互斥。`--input-unit` 解释或覆盖输入单位，保留现有样本值。`--output-unit` 在导出时换算加速度，使用 `1 g = 9.80665 m/s² = 980.665 cm/s²`。已知单位、采样间隔或物理量被覆盖时可以在诊断及来源字段中查看相应记录。`--numeric-factor` 明确指定样本缩放，与解释输入单位及导出单位换算分别处理。

默认导出全部通道，文件名使用输入文件名和安全的通道名称或序号。`-o` 指定目录，单通道时也可指定与目标格式匹配的完整文件名。使用 `--overwrite` 才会允许替换已有输出。程序先核对目标路径，再暂存所有编码结果。若发布阶段发生文件系统错误，命令返回非零并列出已写出的文件，可据此处理部分发布。

## GUI

1. 打开一个文件。解析窗口支持自动识别或手动选格式。
2. 对照编号原文检查数据起始行、布局、列号、时间列和采样间隔。用 **Preview parse** 查看通道数、样本数、单位和诊断。
3. 确认后在主窗口查看全部通道。时间戳和相对起点在通道信息区显示。波形横轴在已知 dt 时显示首样本起的经过时间，其他情况显示样本序号。
4. 可编辑当前通道的名称、采样信息及单位。勾选要写出的通道，选择目标格式和输出目录。导出结果会列出文件和诊断。

## 验收状态

Linux 已完成四份真实样本验收、文本与 AT2 回读、输出冲突和单位验证，并在隔离的 X11 虚拟显示上核验 GUI 工作流。SAC 和 miniSEED 的验收范围按版本、字节序及编码单独记录，两者仍为实验性。Windows 验收待完成。[样本清单与 T0–T7 验收记录](docs/validation/GMTOOLS_PHASE1_RESULTS.md)
