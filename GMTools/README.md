# GMTools

GMTools 将一个文件中的地震动记录读成独立通道。可以检查样本、采样间隔和单位，再将所选通道分别写入独立文件。命令行和 Qt 界面使用同一套解析、校验、换算和导出代码。

## 构建

需要支持 C++20 的编译器和 xmake 3.0 或更新版本。nlohmann/json 3.10 或更新的 3.x 版本由 xmake 查找或安装。构建 GUI 还需要已安装的 Qt 6.2 或更新的 6.x SDK，包含 Widgets 和 Concurrent 模块。

完整构建包含 CLI、Qt GUI 和标准 miniSEED。安装匹配的 Qt SDK、libmseed 并确保可找到 `mseed.pc` 后，在 `GMTools/` 目录执行：

```sh
xmake f -m release --build_gui=y --enable_mseed=y
xmake build -j 4
```

默认构建 `gmtools` 和 `gmtools_gui`，二者共用内部静态库 `gmtools_core`。产物位于 `build/<平台>/<架构>/<模式>/`，例如 Linux Release 的 `build/linux/x86_64/release/`。使用 `xmake run gmtools ...` 或 `xmake run gmtools_gui` 可以免去手动拼接路径；这两个目标的运行目录为 `GMTools/`，以下样例中的相对输入与输出路径均以此为基准。

常用配置：

```sh
# 只构建 CLI，不查找 Qt
xmake f -m release --build_gui=n --enable_mseed=n
xmake build -j 4

# 只使用 Qt SDK 的 GUI 配置
xmake f -m release --build_gui=y --enable_mseed=n --qt=/path/to/Qt/6.x/gcc_64
xmake build -j 4

# 启用 miniSEED，使用与当前编译器、架构及运行库匹配的 libmseed 安装目录
xmake f -m release --build_gui=y --enable_mseed=y --mseed_sdk=/path/to/libmseed
xmake build -j 4

# 安装应用到指定目录；gmtools_core 不作为公共 SDK 安装
xmake install -o /path/to/install
```

`--build_gui` 默认 `y`，`--enable_mseed` 默认 `n`；xmake 会记住上次配置，切换时应显式设置开关。Debug 使用 `-m debug`。Windows/MSVC 的 Qt 路径应指向对应的 MSVC SDK；MinGW 使用 `xmake f -p mingw --mingw=/path/to/mingw --qt=/path/to/Qt/mingw_64`，不要混用两套编译器的依赖。MSVC 各目标统一使用 MD/MDd 运行库，CLI 保留 Unicode 入口。

miniSEED 需要 libmseed 3.5.3 或更新的 3.x 版本。`--mseed_sdk` 下应有 `include/libmseed.h` 和 `lib/`（或 `lib64/`）中的 mseed 库；显式路径无效时直接报错。不指定该选项时，通过 pkg-config 的 `mseed.pc` 查找，可用 `PKG_CONFIG_PATH` 指向安装目录。关闭 miniSEED 时不查找该依赖。分发包含 libmseed 的程序时需附带依赖的 Apache 2.0 许可及适用声明。[EarthScope libmseed](https://github.com/EarthScope/libmseed/tree/v3.5.3)

GUI 的翻译源文件为 [`translations/gmtools_zh_CN.ts`](translations/gmtools_zh_CN.ts)。构建 GUI 时需要 Qt Linguist 的 `lrelease`；xmake 会把它生成到产物的 `translations/` 目录，找不到工具时 GUI 构建报错，CLI 构建不受影响。修改界面文字后用 `lupdate src -ts translations/gmtools_zh_CN.ts` 更新源文件，再由构建完成编译。

当前构建入口为 [`xmake.lua`](xmake.lua)，不再维护 CMake 配置；`docs/validation/` 和架构整理记录中的旧命令属于对应版本的历史证据。旧 `build/` 下的 CMake 产物不会自动删除。

## 格式与限制

| 格式 | 读取 | 写出 | 说明 |
|---|---|---|---|
| TXT / DAT | 是 | 是 | UTF-8/ASCII 数值文本。通用表头可通过指定首数据行跳过，多个数值一行且布局不清楚时需明确布局。 |
| CSV / TSV | 是 | 是 | 默认按列分通道，可带简单列名。默认写一列幅值和一个列名。可选经过时间列、元信息表头和有效数字。 |
| PEER NGA AT2 | 是 | 是 | 识别常见加速度头段及 `NPTS`、`DT`。Helena 样本已通过 AT2↔CSV 回读。写出以 g 保存。样式参见 [PEER 报告附录](https://peer.berkeley.edu/sites/default/files/publications/2017_09_stewart_9.10.18.pdf)。 |
| PEER VT2 / DT2 | 是 | 是 | 按头段识别速度、位移，分别导出为 cm/s、cm。已用 Tabas 速度实录和 Northridge 位移实录逐点核对。 |
| Modified miniSEED | 实验性 | — | 专用 wc/wt/ws 包头和 Steim-2 读取，可选择原始 counts 或文件灵敏度标定。武汉 wc 实录已核验，wt/ws 设备实录待验收。两种构建配置均包含此 Reader。 |
| NIED K-NET / KiK-net ASCII | 实验性 | — | 单文件单分量，通过头段辨认格式，按 Scale Factor 把有符号整数 counts 标定为 cm/s²。正式样本验收待完成。[NIED ASCII 格式](https://www.kyoshin.bosai.go.jp/en/knetascii/) |
| SAC Binary | 实验性 | 实验性 | 读取两种字节序的 v6/v7 等间隔 ITIME 记录。输出为小端 v6/v7，默认 v7 含双精度 footer，两者的波形均为 float32。[SAC Binary 规范](https://ds.iris.edu/files/sac-manual/manual/file_format.html) |
| miniSEED 2 / 3 | 实验性，可选 | 实验性，可选 | v2/v3 共用读取入口，当前允许 INT16、INT32、FLOAT32、FLOAT64、Steim-1、Steim-2 波形编码。可写 FLOAT64（默认）、FLOAT32、INT32 或 Steim-2，`mseed` 写 v3，`mseed2` 写受标识和时间表示约束的 v2。[EarthScope libmseed](https://github.com/EarthScope/libmseed/tree/v3.5.3) |

第三阶段核验了台湾台站小端 SAC v6 原件、小端 v6/v7 写出和独立生成的大端 v6 输入。标准 miniSEED 核验了花莲 v2 FLOAT32 实录、libmseed 的六种 v2 编码参考文件，以及 v2/v3 各四种输出编码。[分项验收记录](docs/validation/GMTOOLS_PHASE3_RESULTS.md)

时间列以秒为单位，需要指定列号。解析会核查单调性和整条时间轴的等间隔性。只有一个样本时，时间列只能给出相对起点。通用文本支持 Fortran `D/d` 指数。复杂引号 CSV、GBK 与固定宽度字段超出当前解析范围。

PEER 头段缺少 NPTS 或与实际数量冲突时给出警告并保留实际样本。缺少或非法的 DT 会阻止 AT2 解析，可切换到通用文本并指定数据起始行来解释该文件。SAC 读取默认只从 IDEP 取得物理量。确认数据遵循 SAC 纳米制时，可用 `--sac-input-units standard` 解释单位。能同时表示时，SAC 导出保留相对起点 B 和绝对首样本时间。`SCALE` 作为元数据保存，样本读入时保持原值。

miniSEED 以文件中的记录顺序处理。同一源标识在时间间隔、重叠、乱序或采样参数变化时拆成独立 Trace。仅在编码时间精度和采样间隔都允许时合并相邻片段，接受非零时间残差时会报告。样本值维持输入的数值，单位缺失时保留未知。输出需要已知绝对起始时刻、采样间隔和符合对应版本要求的 FDSN 源标识。对于带台网、台站和三字符分量的 SAC，也可以从这些字段组成标识。miniSEED 2 写出还会核查编码后的采样间隔和时间戳。花莲样本的两字符通道标签超出当前 v2 Writer 的源标识约束，可导出 v3。

K-NET ASCII 读取从 `Memo.` 后按行展开 count，以头段 `Sampling Freq(Hz)` 确定 dt。首样本为 JST 触发时刻 `Record Time` 减 15 秒并转换为 UTC。通道编号、原始方向与仪器系数记入元数据。CSV 默认保持标定后的 cm/s²，AT2 再换算为 g。显式 `--numeric-factor` 在仪器标定后单独应用。冲突的输入单位或采样设置在标准模式下报错。

## CLI

```sh
# 查看当前构建提供的格式及读写能力
xmake run gmtools formats

# 查看 AT2 的采样信息、单位、通道和诊断
xmake run gmtools info record.at2

# 从时间 + 两个通道的 CSV 分别导出两个文件
xmake run gmtools convert record.csv --time-column 1 --data-columns 2,3 \
  --to csv -o out/

# 将第 5 行起的多列连续样本解释为一条加速度记录
xmake run gmtools convert record.txt --format text --first-line 5 --layout sequential \
  --dt 0.01 --input-unit cm/s2 --to at2 -o out/

# Trinidad 样本的头段注明时间列使用秒、加速度列使用 g
xmake run gmtools convert example/Trinidad.dat --first-line 6 --layout columnar \
  --time-column 1 --input-unit g --to csv -o out/

# 复用 Trinidad 的文本解释参数
xmake run gmtools info example/Trinidad.dat --profile example/trinidad.profile.json

# 将单路 NIED ASCII 波形按头段标定后写为 AT2
xmake run gmtools convert /path/to/station.NS --format knet-ascii --to at2 -o out/

# 显式指定输出单位，选择第二条通道
xmake run gmtools convert channels.csv --channel 2 --input-unit g --output-unit m/s2 \
  --to txt -o selected.txt
```

行号、列号和通道编号都从 1 开始。首末行均包含在数据范围内。`--dt` 和 `--fs` 互斥。`--input-unit` 解释或覆盖输入单位，保留现有样本值。`--output-unit` 在同一物理量内换算，重力加速度采用 `1 g = 9.80665 m/s² = 980.665 cm/s²`。已知单位、采样间隔或物理量被覆盖时可以在诊断及来源字段中查看相应记录。`--numeric-factor` 明确指定样本缩放，与解释输入单位及导出单位换算分别处理。

默认导出全部通道，文件名使用输入文件名和安全的通道名称或序号。`-o` 指定目录，单通道时也可指定与目标格式匹配的完整文件名。使用 `--overwrite` 才会允许替换已有输出。程序先核对目标路径，再暂存所有编码结果。若发布阶段发生文件系统错误，命令返回非零并列出已写出的文件，可据此处理部分发布。

## 单位、标定与通道标识

| 物理量 | 可用物理单位 |
|---|---|
| 加速度 | g、m/s²、cm/s²（Gal）、nm/s² |
| 速度 | m/s、cm/s、mm/s、nm/s |
| 位移 | m、cm、mm、nm |

命令可用 `cm/s2`、`nm/s/s` 等 ASCII 拼法。单位换算要求已知且同量纲。输入 `counts` 表示数字计数，`unknown` 表示解释待补。标准 miniSEED 的整数编码本身不确定物理单位。对已知计数，指定物理单位或调幅系数都不能代替仪器标定。

Modified miniSEED 默认输出原始计数。`--calibration source` 使用扩展低 30 位的灵敏度，按 `counts × 100 / sensitivity` 得到对应 SI 单位；无量纲辅助通道保留 counts 并提示。零灵敏度阻止文件标定。已知 counts 可用 `--calibration manual --calibration-factor 0.025 --calibration-unit cm/s2` 手动标定。普通数值文本需要先用 `--input-unit counts` 确认输入。对所有格式，`--numeric-factor` 都在本次解析的标定之后执行一次，再按需换算输出单位。K-NET 固定应用文件 Scale Factor。

Modified Reader 以片段首样本和累计点数预测下一包起点。头段条件一致、起点偏差在 `min(100 µs, dt/16)` 内时合并，所有样本保持原顺序。每个片段的绝对偏差按最近整数纳秒记录，半纳秒向上取整。首次达到 1 ns 时提示通道及包序号，随后只更新 `modified.max_start_residual_ns`。完全连续或舍入为零时省略该字段。超容差或时间未知时另起片段。

`--start-time` 接受带 `Z` 的 UTC 时间，可精确到纳秒。`--relative-start` 为独立的秒数。完整 `--source-id` 和分项 `--network --station --location --channel-code` 二选一；已知空位置可写为 `--location ''`，恢复未知位置可用 `--clear-location`。多通道文件修改标识时要指定 `--channel N`。指定通道后，仅对该通道应用输入覆盖、标定和缩放。显示名称用于列表与默认文件名。跨格式写出使用通道标识，显式编辑优先于原始 SAC、miniSEED 字段。

## 按目标格式导出

```sh
# 将第一路标定加速度转换成 cm/s²，输出带时间列和元信息的 CSV
xmake run gmtools convert station.mseed --format modified-mseed --channel 1 \
  --calibration source --output-unit cm/s2 --to csv --text-layout time-values \
  --text-header metadata -o station.csv

# 补齐时刻和标识，将数值时程写入标准 miniSEED 3
xmake run gmtools convert channel.txt --dt 0.005 --start-time 2025-02-07T16:44:58Z \
  --source-id FDSN:AB_TEST__H_N_Z --to mseed --mseed-encoding float32 -o channel.mseed

# 以 SAC 纳米制写入 v6，工程数值会相应换算
xmake run gmtools convert example/RSN1_HELENA.A_A-HMC180.AT2 \
  --to sac --sac-version 6 --sac-unit-mode standard -o helena.sac
```

- **文本**默认保持 TXT/DAT 无头单列、CSV/TSV 单列带列名。`--text-layout values|time-values` 选择幅值或经过时间与幅值。时间列从零开始，以 `i × dt` 计算。`--text-header none|columns|metadata` 选择表头。`--precision 1..17` 仅调整幅值有效数字。CSV/TSV 使用固定分隔符；TXT/DAT 可选 `--text-delimiter whitespace|comma|tab`。
- **自描述文本**第一行为 `# GMTools waveform 1`，第二行为以 `# ` 开头的一行 JSON，第三行为列名，第四行起为数据。头段保存实际输出单位、量、名称、点数、可用的 dt、UTC、相对起点和标识。回读校验点数与时间网格；经过时间的零点和源相对起点独立。普通文本继续使用原有 Profile。
- **SAC** 默认 v7，可用 `--sac-version 6` 写出 v6。v6 时间舍入超过 1/16 个采样间隔会报错。默认 `--sac-unit-mode keep` 保留工程尺度，仅对已知纳米制写匹配的 IDEP；旧版会对其他工程尺度也写入物理量 IDEP。依赖传统 SAC IDEP 解释的软件应使用 `standard`，由 GMTools 换算并设置 IDEP。台站、事件坐标、方向和可表达的标识随记录传递。
- **标准 miniSEED** 使用 `--mseed-encoding float64|float32|int32|steim2`。浮点缩窄会提示舍入，整数编码要求每点精确可表示为 int32，Steim-2 还要求差分可编码。缺 UTC、缺标识或目标版本无法保持时间网格时拒绝导出。该输出不携带工程单位。

### 自描述文本的数值来源

v1 的可选 `provenance` 对象用 `original_quantity` 和 `original_unit` 保存已知的处理前标签，`steps` 按执行顺序保存标定、调幅和导出换算。例如，武汉 EIN 从 counts 经文件标定、两倍调幅并以 cm/s² 写出后，头段中的主字段为 `quantity="acceleration"`、`unit="cm/s²"`，对应的来源对象如下。

```json
{
  "original_quantity": "counts",
  "original_unit": "counts",
  "steps": [
    {"type": "calibration", "source": "Modified miniSEED", "factor": 0.00002, "unit": "m/s²"},
    {"type": "scale", "factor": 2},
    {"type": "unit_conversion", "from": "m/s²", "to": "cm/s²"}
  ]
}
```

标定步的 `factor` 以所列物理单位每 count 表示，调幅步的 `factor` 为实际乘数。回读时从文件直接取样本和当前单位，`gmtools info` 和 GUI 的 **Metadata** 可查看 `history.*`。再次显式调幅或换算会追加步骤。缺少来源对象的旧 v1 文件照常读入。读取时跳过未知扩展字段和步骤类型，已知字段的单位、乘数或结构无效时会报错。格式专有头段和未应用的候选灵敏度不随该对象导出。

## 文本解析配置

`--profile FILE` 可供 `info` 和 `convert` 使用。GUI 的普通文本导入窗口也可以载入或保存同一份 JSON。[Trinidad Profile 示例](example/trinidad.profile.json)

| JSON 字段 | 允许值 |
|---|---|
| `schema_version` | 必填，整数 `1`。 |
| `format` | `auto`、`text`、`dat`、`csv` 或 `tsv`。省略时自动检测。 |
| `text` | 可包含 `first_line`、`last_line`、`layout`、`delimiter`、`data_columns`、`time_column`、`numeric_factor`。布局为 `sequential` 或 `columnar`。分隔符为 `auto`、`whitespace`、`comma` 或 `tab`。列号从 1 开始，数据列用整数数组保存。 |
| `overrides` | 可包含 `dt`、`fs`、`quantity`、`unit`。`dt` 和 `fs` 互斥，量和单位沿用 CLI 的选项。 |

CLI 显式填写的值优先于 Profile。例如显式 `--fs` 替换 Profile 的 `dt`，与 Profile 不同的值会产生提示。GUI 的 **Load profile** 完整替换当前表单，包括清空配置中未设置的字段；加载后可以继续手动调整。读取标准文件时仍执行该格式的头段和标定校验。未知字段、重复键、非法值和未知版本会报错。Profile 只保存输入解释参数。

诸如 `Time[s]`、`Time (s)` 和 `Accel[g]` 的明确标签可用来提出导入建议。程序先核对完整数据区和时间网格。CLI 将建议写到标准错误，GUI 可通过 **Apply text suggestions** 应用。建议需要显式采用，多个候选时间列或不同数据列的量或单位冲突时由用户选择。

## 语言、主题与设置

GUI 的语言与主题由 **Settings**（`Ctrl+,`）设置，也可在未打开文件时调整。默认跟随系统；可选简体中文/English 以及跟随系统/浅色/深色。两者在重新启动后生效，程序会提示。语言设置只影响界面文字，不改变格式名、单位符号、JSON 字段、CLI 输出、Core 诊断原文和已有的元信息键值。

设置项还包括“记住最近使用的目录”和“记住上次转换设置”。关闭记忆后程序不再读写对应值，并清除已有记录。最近转换设置只恢复目标格式与输出单位这类通用参数，不恢复 UTC、台站标识、通道选择、覆盖开关或临时路径。覆盖开关每批默认关闭。

## 转换预设

转换预设把输入解释、标定和写出参数保存为一份独立 JSON，供 CLI 与 GUI 共用。预设不包含路径、覆盖许可、波形数据和通道标识；这些仍由每次运行的显式参数决定。

```json
{
  "kind": "gmtools-conversion-preset",
  "schema_version": 1,
  "name": "Time series CSV with metadata header",
  "read": {"format": "auto", "text": {"layout": "columnar", "time_column": 1}},
  "write": {"format": "csv", "text_layout": "elapsed-time", "text_header": "metadata", "precision": 6},
  "selection": {"mode": "all"}
}
```

| 字段 | 允许值 |
|---|---|
| `kind` | 必填，固定为 `gmtools-conversion-preset`。 |
| `schema_version` | 必填，整数 `1`。 |
| `name` | 可选，最长 120 字符。 |
| `read.format` | `auto` 或任一格式名，如 `text`、`csv`、`at2`、`sac`、`modified-mseed`。 |
| `read.text` | 与 Profile 相同的文本字段：`first_line`、`last_line`、`layout`、`delimiter`、`data_columns`、`time_column`、`numeric_factor`。只适用于文本类输入。 |
| `read.overrides` | `dt`、`fs`、`quantity`、`unit`；`dt` 与 `fs` 互斥。 |
| `read.calibration` | `default`、`raw`、`source`、`manual`；`manual` 必须同时给出 `calibration_factor` 与 `calibration_unit`。 |
| `read.sac_standard_units` | 可选布尔值，仅适用于 SAC。 |
| `write.format` | 必填，不能为 `auto`。 |
| `write.output_unit` | 省略表示保持输入单位；CLI 的 `--output-unit keep` 可清除预设单位。 |
| `write.text_layout`、`text_header`、`text_delimiter`、`precision` | 文本写出选项。 |
| `write.sac_version`、`sac_unit_mode`、`mseed_encoding` | 对应格式的写出选项，只在该格式适用。 |
| `selection.mode` | `all` 或 `quantity`；后者必须给出 `quantity`。 |

优先级为默认值 < 预设 < 本次显式参数。CLI 以参数是否出现判断覆盖，因此 `--format auto`、`--calibration default` 也能显式清除预设值。`--to` 覆盖预设目标格式时只清除不再适用的写出参数并提示；显式传入但不适用于目标格式的专有参数会报错。`--profile` 与 `--preset` 不能同时使用。预设与 Profile 是两份独立格式：Profile v1 仍只保存文本解释参数，不会被迁移或改写。

内置示例位于 [`example/presets/`](example/presets)：纯幅值 TXT、带时间列与元信息表头的 CSV、cm/s² 文本加速度、PEER AT2、Modified miniSEED 工程单位 CSV。示例不猜测单位、采样率或标定系数；缺少时由命令行或界面补充。GUI 管理的预设保存在本机配置目录的 `presets/` 下，CLI 只读取显式给出的文件。载入预设会完整替换表单（包括清空未设置的字段）并显示预设名称，之后仍可手动修改。

## 批量转换

`batch` 把同一份有效配置依次应用到多个输入文件。每个文件仍按单文件规则读取、标定、筛选和写出；单个文件失败不会中断其余文件。

```sh
# 目录中的全部文件，预设提供读取与写出参数；时间列需要采样间隔
xmake run gmtools batch --input-dir records -o out/batch \
  --preset example/presets/02-time-series-csv.json --dt 0.01

# 只处理 *.at2，显式指定目标格式与输出单位
xmake run gmtools batch --input-dir records --filter '*.at2' -o out/batch --to at2 --output-unit g

# 显式文件列表，保持给定顺序
xmake run gmtools batch one.at2 two.dat three.csv -o out/batch --to csv
```

显式列表与 `--input-dir` 二选一，`-o` 必须是目录。目录默认不递归，`--filter` 只按文件名的 `*`、`?` 通配符匹配；目录结果按文件名稳定排序。重复输入（同一文件、别名或大小写等价）会被提示并跳过。空列表或无匹配文件返回非零。

命名规则与单文件导出一致：输出目录中平铺 `<输入文件名主干>_<安全通道名>.<扩展名>`。读取失败、没有选中通道或编码失败的文件不产生输出。已有目标只有在本次给出 `--overwrite` 时才替换；目标指向批次内任一源文件时永远拒绝，覆盖也不解除该限制。同一批次内两个文件生成同一目标时，先处理者保留，后者失败并注明来源。输出目录不存在时按目录语义创建。

`--select-all` 导出全部通道（默认）；`--select-quantity acceleration|velocity|displacement|counts|unknown` 在完整读取与标定后按物理量筛选，未匹配时该文件失败，不会把空列表传给导出层。批量不接受 `--channel`、UTC、相对起点或 NSLC/SID 等单文件覆盖，这些仍用 `convert`。

`Ctrl+C`（`SIGINT`）在完成当前文件并发布结果后停止，尚未开始的文件标记为已取消，退出码为 `130`。全部成功为 `0`；任一失败、部分成功或取消为非零；参数或公共配置错误为 `2`。进度按文件输出终态，每个文件保留失败阶段、选中通道数、实际写出路径与诊断原文。

## GUI

1. 点击 **Open file**、按 `Ctrl+O` 或拖入单个文件。解析窗口支持自动识别或手动选格式。
2. 对照编号原文检查数据起始行、布局、列号、时间列和采样间隔。通用文本可以选择 **Apply text suggestions**、**Load profile** 或 **Save profile**。用 **Preview parse** 查看通道数、样本数、单位和诊断，再点击 **Import channels**。修改表单后，旧预览会失效；参数未变化时直接复用成功的预览结果。
3. 左侧列出全部通道，点击通道可定位并高亮对应波形，勾选决定导出范围。右侧 **Summary / Metadata / Diagnostics** 分别展示参数、来源字段及导入诊断。波形横轴在已知 dt 时显示首样本起的经过时间，其他情况显示样本序号。大记录使用保留极值的概览包络绘图，导出仍使用完整样本。
4. 使用 **Edit channel parameters** 编辑显示名称、采样信息、量与单位、UTC、相对起点及 SID/NSLC。输入单位用于解释现有数值，导出区的输出单位才执行数值换算。K-NET 的额外 **Numeric factor** 在仪器标定后应用；标准格式的单位和采样约束仍由核心检查。
5. 在 **Export** 中选择目标格式、勾选通道共同适用的输出单位和目录。**Load preset / Save preset / Manage** 载入或保存转换预设；载入后从原始字节重新解析，不会在已标定的样本上再次标定。**Format options** 可调整文本布局及表头、SAC 版本和单位策略、miniSEED 编码。点击导出按钮或按 `Ctrl+Shift+E`。按钮显示勾选数量，未选通道时禁用。格式说明提前展示采样和元数据限制，结果窗口列出实际文件与诊断，并可打开输出目录。

6. **Settings** 调整语言、主题和记忆选项，**Batch conversion**（`Ctrl+B`）打开批量窗口。批量窗口显示文件表、预设、输入参数、通道筛选、输出目录与覆盖开关；**Add files / Add directory** 去重后加入列表，目录按文件名模式筛选且默认不递归；也可以把多个文件或目录拖入批量窗口。开始前会确认文件数、输出目录与命名规则。运行期间只能停止或关闭详情，关闭请求走安全停止；结束后列出每个文件的状态、写出数量与诊断，并可打开输出目录。

读取、解析、波形概览计算、批量与导出在后台执行；执行期间显示忙碌指示，禁止替换记录、编辑及关闭正在工作的窗口。打开新文件或重新解释文件时会重置覆盖开关，有未全部导出的通道编辑时会提示。窗口尺寸、面板比例及最近使用目录保存在本机 Qt 设置中。界面按启动时的系统配色选择浅色或深色主题。

模块职责、此次整理的问题及人工检查项目见[架构与界面整理记录](docs/log/ARCHITECTURE_AND_UI_REVIEW.md)。

## 验收状态

第一阶段在 Linux 完成了四份真实样本验收、文本与 AT2 回读、输出冲突和单位验证，并在隔离的 X11 虚拟显示上核验了当时的 GUI 工作流。SAC 和 miniSEED 的验收范围按版本、字节序及编码单独记录，两者仍为实验性。Windows 验收待完成。[样本清单与 T0–T7 验收记录](docs/validation/GMTOOLS_PHASE1_RESULTS.md)

第二阶段加入文本建议、Profile 与实验性 NIED ASCII 读取。正式波形样本、运行回归和界面操作待验收。PEER VT2/DT2、COSMOS 和 USGS SMC 的调查范围见[第二阶段样本与开发记录](docs/validation/GMTOOLS_PHASE2_RESULTS.md)。

第三阶段的运行证据、样本来源与待验收范围见[第三阶段验收记录](docs/validation/GMTOOLS_PHASE3_RESULTS.md)。实施范围见[第三阶段开发文档](docs/5.GMTOOLS_PHASE3_DEVELOPMENT.md)。

第四阶段加入界面国际化、程序设置、转换预设与顺序批量转换。实施范围见[第四阶段开发文档](docs/6.GMTOOLS_PHASE4_DEVELOPMENT.md)，当前构建、运行与未完成项见[第四阶段验收记录](docs/validation/GMTOOLS_PHASE4_RESULTS.md)。

### 本地复核

使用 Python 3.9+ 和已构建的 CLI。输出目录须为新目录，每次运行请更换名称。

```sh
python3 scripts/verify_core_formats.py --gmtools build/linux/x86_64/release/gmtools \
  --examples example --output build/phase3-closeout/check-01
```

脚本用标准库独立读取 Helena、Trinidad 和 SAC，并核对 v1 兼容性与数值历史。安装可选的 ObsPy 后可比对花莲记录。追加 `--modified "/path/to/WH.XY001-2024-01-01-15-15-00.mseed"` 可核查武汉样本的哈希与分段时间，安装 ObsPy 后还可独立解压比对标定值。每项分别报告 `PASS`、`FAIL` 或 `SKIPPED`。断言失败返回非零，CLI 输出保存在新目录的 `commands.log`。本次收尾的执行状态单独记录。[核验脚本](scripts/verify_core_formats.py) · [验收状态](docs/validation/GMTOOLS_PHASE3_RESULTS.md#收尾修复)

### 示例数据来源

`example/` 中的四份波形由用户提供，首次入库于 `ce14599`。Helena 头段署名 PEER NGA，Trinidad 头段署名 PEER Strong Motion Database。台湾 SAC 的头段标识为 TW.5AFCD.00.HNZ，花莲 miniSEED 为 CB.TAPBA 的 28 个通道，这两份文件的提供机构待确认。仓库中未找到四份原件的下载日期、原始下载链接或再分发授权。对外分发前应由数据提供者确认授权范围。武汉与 PEER VT2/DT2 外部样本按本地提供的文件复核。Modified 解码器移植自 REST data，其 MIT 许可适用于相应代码。[示例摘要与哈希](docs/validation/GMTOOLS_PHASE1_RESULTS.md#样本清单) · [外部样本和代码版本](docs/validation/GMTOOLS_PHASE3_RESULTS.md#可复核的样本与数值) · [REST data 许可](licenses/REST-data-MIT.txt)
