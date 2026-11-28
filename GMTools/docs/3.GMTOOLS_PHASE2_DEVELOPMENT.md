# GMTools 第二阶段开发文档：标准文本格式扩展与导入便利性完善

> 项目：EETB / GMTools  
> 文档性质：阶段开发任务说明（供 Codex 实施）  
> 制定日期：2026-11-24  
> GitHub：<https://github.com/FeiyueDong/eetb>  
> 评估基线：`main` / `ce14599`（`feat: update test`）  
> 关联文档：`docs/PROJECT_PRINCIPLES.md`、`docs/GMTOOLS_PHASE1_DEVELOPMENT.md`、`docs/validation/GMTOOLS_PHASE1_RESULTS.md`  
> 阶段定位：**保持核心架构稳定，优先扩展真实标准文本格式，并减少非标准文本导入时的重复手动设置。**

## 1. 背景、目标与完成定义

第一阶段已建立 `FileData + Trace`、统一 Reader/Writer、静态格式注册、`TextParseOptions`、CLI、Qt GUI 和逐 Trace 单文件导出；TXT/DAT、CSV/TSV 与 PEER AT2 基础转换已通过真实文件验收。SAC 和标准 miniSEED 已完成部分实际子类型的独立验证，但保持实验性。四份现有样本及第一阶段的测试记录应视为**回归基线**，不能在扩展格式时破坏已有行为。

本阶段的核心问题不再是“如何建立格式转换框架”，而是：

1. **已知标准文本**能否凭头段自动识别格式、数据区、采样参数、标定系数和必要元信息，并继续复用现有数值解析能力。
2. **通用非标准文本**能否通过简明的表头辅助识别与可复用解析配置，降低每次指定起始行、布局、时间列、单位的操作成本。
3. 新增能力能否继续复用既有 CLI、GUI、`read()`、`write()`、`export_file()`，而不改变单文件输入和单 Trace 输出的原则。

**本阶段核心交付目标**：
- 通用文本的解析配置能够保存、加载，并在 CLI/GUI 中复用；对有明确列标签的文本提供安全的参数建议。
- 首个标准文本增量以 **NIED K-NET / KiK-net ASCII（单文件单分量）读取** 为重点，完成头段解释、数字计数标定为物理加速度、时间信息和实际样本验证。
- 不改变现有四份样本的数值、单位与时间解析结果；第一阶段已验收的导出行为仍然成立。
- 对 PEER VT2/DT2、COSMOS/SMC 完成适配可行性调查；其中部分内容可依据真实样本和复杂度**条件性实施**，不以格式数量作为完成指标。

> **完成判定**：M1（通用文本便利性与 Profile）和 M2（经真实样本验收的 K-NET ASCII Reader）均通过，并完成针对性回归，方可宣告第二阶段核心目标完成。若暂时拿不到许可明确、可验证的 K-NET 实际数据，仍可提交其他工作，但 M2 标为 `BLOCKED`，不得将模拟样本结果写成正式兼容性验收。

## 2. 保持不变的项目约束

以下规则为本阶段的硬性边界：

- **单文件输入**：一次 `read()` 仅解释一个输入文件。即使 K-NET/KiK-net 的 NS、EW、UD 分别放在不同文件中，也不进行跨文件自动合并。
- **独立 Trace**：一个文件可产生 1～N 条 Trace，各自保存样本、`dt`、起点、物理量、单位和扩展属性。不引入 Ground Motion/Record Set 层级。
- **单通道单文件输出**：Reader 可以新增；不要求每个新格式都有对应 Writer。现有 TXT/CSV/AT2/SAC/miniSEED Writer 仍按原有能力和限制使用。
- **无隐式波形处理**：不做基线修正、去均值、滤波、积分、截取、重采样或三分量旋转。格式规定的标定与显式单位转换须单独表述。
- **不猜测物理含义**：不凭文件扩展名、列数或幅值范围推断物理量、单位或采样率；确定性不足时保留未知并要求用户确认。
- **CLI/GUI 共用 Core**：禁止分别实现两套表头识别、单位换算、数据导出或解析配置解释逻辑。
- **避免架构膨胀**：静态格式表 + 公共 Text Parser + 少量格式头段处理函数即可；不引入动态插件、工作流、任务库和庞大的自动识别框架。

本阶段**不开展**目录批量任务、跨文件匹配、更多二进制格式、REST 专有 miniSEED 的专项适配、频谱/反应谱、数据处理算法、全站点信息数据库和 GUI 大规模重设计。

## 3. 开发内容与优先级

| 模块 | 目标 | 本阶段级别 | 依赖/注意事项 |
|---|---|---|---|
| M0 数据与规范调查 | 取得 K-NET ASCII 实际样本、核对官方格式、记录参考值 | 必做、优先启动 | 先确认样本来源与使用/再分发条件 |
| M1a 通用文本辅助识别 | 可靠文本头段/列名建议，不改变歧义时的安全默认行为 | 必做 | 先复用当前 `TextParseOptions` |
| M1b 解析 Profile | 简洁、版本化的文本解析配置；CLI/GUI 使用同一套 | 必做 | 只保存输入解释参数，不变成工作流 |
| M2 K-NET/KiK-net ASCII Reader | 头段、采样、方向、标定、时刻、数据读取 | 必做（实测验收） | 读取单文件单分量；默认不写回该格式 |
| M3 PEER VT2/DT2 | 评估后按真实样本扩充 PEER 家族 | 条件性 | 速度/位移单位需单独合法建模 |
| M4 COSMOS / SMC 调查 | 辨别具体格式、块结构、V0/V1/V2 等目标子集 | 调查必做，编码非必做 | 不假定 `.smc` 都是同一标准 |
| M5 集成验收与文档 | 旧样本回归、新标准样本独立对照、CLI/GUI 核查 | 必做 | 精简真实数据测试 |

实现顺序允许 M0 与 M1 并行，M2 应在正式样本和参考信息准备后开发，不以假想字段驱动适配器。

## 4. M0：标准格式规范与样本准备

### 4.1 首要样本：K-NET / KiK-net ASCII

依据 NIED 官方文档，该格式典型头段包含 `Origin Time`、`Station Code`、`Record Time`、`Sampling Freq(Hz)`、`Duration Time(s)`、`Dir.`、`Scale Factor` 等字段；数值区从头段后开始，**一行可排多个连续采样值，数值是仪器数字计数**。KiK-net 可以出现 `NS1/EW1/UD1`（井下）及 `NS2/EW2/UD2`（地表）等分量标记，但各自依然是独立的波形文件。

数据准备要求：

1. 从 NIED 官方渠道取得至少 **1 份 K-NET ASCII 实际单分量文件**，如条件允许再取得 **1 份 KiK-net ASCII**，最好覆盖带数字后缀的方向名称。
2. 记录文件来源 URL、下载时间、SHA-256、原始头段、采样率、Scale Factor、方向和数据规模。不能仅以扩展名判断格式。
3. 使用 ObsPy 的 NIED/K-NET 读取器或其他独立解释路径取得对照结果；明确量、单位、样本顺序、首样本 UTC、样本数。
4. 不自动下载并提交大批测试数据到公开仓库；先确认版权、引用和再分发要求。若不可随代码发布，保留可公开的样本说明和独立核验方法。

官方参考：
- NIED K-NET ASCII Format：<https://www.kyoshin.bosai.go.jp/en/knetascii/>
- NIED FAQ（多列整数、方向、标定说明）：<https://www.kyoshin.bosai.go.jp/en/faq/>
- ObsPy NIED Reader 实现（供独立对照）：<https://github.com/obspy/obspy/blob/master/obspy/io/nied/knet.py>

### 4.2 PEER 与 COSMOS 样本准备

- PEER 家族应优先取得实际 `.VT2`、`.DT2` 文件；先检查头段声明的物理量与单位，不能仅按扩展名硬编码。若暂无有效样本，先记录待扩展目标，不提交推测式读写实现。
- COSMOS 应明确“产品版本 / 文件类型 / 数据段 / 量纲”；USGS/CSMIP 的传统 `.smc` 与 COSMOS 标准文件不能因都属于强震 ASCII 就视作同一格式。先形成目标子集调查表；本阶段不要求完整读取。
- 参考：PEER NGA-West2 <https://peer.berkeley.edu/peer-nga-west2-online-ground-motion-database-updated-and-expanded>；COSMOS 格式与强震数据格式列表 <https://www.strongmotioncenter.org/vdc/scripts/formats.plx>、<https://www.strongmotioncenter.org/vdc/cosmos_format_1_20.pdf>。

## 5. M1：通用文本输入便利性与 Profile

### 5.1 文本表头的有限辅助识别

保持现有 `Sequential`（按行顺序展平为单 Trace）和 `Columnar`（每个数据列分别形成 Trace）的两种基本布局，**不扩展为独立的四套解析器**。

本阶段只实现证据明确的辅助能力：

- 对常见 `Time[s]`、`Time (s)`、`t(s)` 等**明确时间列标签**以及 `Accel[g]`、`Acceleration (cm/s2)` 等**明确单位标签**，在通用文本导入时提供建议。须检查完整列数及数据区可解析性；标签只用于解释，不可代替全列时间等间隔验证。
- 对数据前有少量说明文字、随后有规则数值行的情况，可给出**候选起始行**；不能从任意位置找到几个数字就认定数据区已被可靠识别。多数据区或前后夹杂其他表格时应请求用户明确指定。
- 如果时间列标签和布局存在歧义，或内容与候选时间轴不一致，应显示候选及问题，而不是静默把第一列当作时间。
- 不改变用户显式选择的 `first_line`、`layout`、`time_column`、`data_columns`、`dt/fs`、`quantity/unit`；自动建议不能覆盖用户已确认的配置。
- 自动建议结果及其依据可出现在解析预览中，例如“时间列建议：1（表头 Time[s]）”“单位建议：g（表头 Accel[g]）”。保存 Profile 时由用户明确决定是否保存这些值。

实现方式应以现有 `text.cpp` 和 `ReadOptions` 为基础。可以新增一个**轻量的文本探测结果**（候选选项、来源说明、诊断），再将最终确认的选项交给现有 `parse_text()`；不要让建议器直接修改 Trace 样本。

**Trinidad.dat 是这一功能的重点回归样本**：当前可以用 `--first-line 6 --layout columnar --time-column 1 --input-unit g` 读取。新版本应在 GUI 中减少明显的重复设置；如无法在不误判的前提下可靠自动填写全部字段，允许只提供正确候选和一键应用，不要求强制“零参数”自动成功。

### 5.2 保存与读取通用文本解析配置

新增一个简单的 **Profile**，供用户保存并复用对同类文件的输入解释规则：

- 使用 JSON 文本保存，首版仅需 `schema_version: 1`；通过明确的字段名进行读写和校验。无须开发复杂的配置管理库。
- Profile 仅包含**输入格式/文本布局与用户解释参数**，例如起止行、分隔符、布局、时间列、数据列、`dt/fs`、`quantity/unit`、必要时 `numeric_factor`；**不保存输出路径、原始数据、导出结果、用户任务历史**。
- 配置值与当前已有 `TextParseOptions`、`Overrides` 对应，避免为 CLI/GUI 再定义另一种互不兼容的解释参数。
- 采用显式路径载入；GUI 可以“载入解析配置 / 保存当前解析配置”，CLI 增加 `--profile PATH`；不要求本阶段实现配置同步、云端保存、全局 Profile 数据库或复杂列表管理。
- Profile 加载失败、字段非法、版本未知应报告具体错误；不得只因缺一个可选项就丢弃其余有效参数，且不能忽略未知的重要语义字段。
- **优先级约定**：命令行或 GUI 本次显式指定 > Profile 中显式值 > 可靠格式头段/文本标签推断 > 未知。对于 PEER/K-NET 的强制结构头段和物理标定，若出现冲突，先核验格式合法性并给出冲突诊断；用户想按另一种含义解释时应选择通用文本模式，不允许在标准模式中静默绕过格式校验。
- 注意 `--input-unit` 是对原始数值的解释/覆盖而非数值转换；`--output-unit` 仍只用于写出阶段。Profile 不得隐式触发一次用户调幅。

建议 Profile 样例（**结构示意，字段名最终以实现和文档约定为准**）：

```json
{
  "schema_version": 1,
  "format": "dat",
  "text": {
    "first_line": 6,
    "layout": "columnar",
    "delimiter": "whitespace",
    "time_column": 1,
    "data_columns": [2]
  },
  "overrides": {
    "quantity": "acceleration",
    "unit": "g"
  }
}
```

CLI 使用示意：

```bash
# 仍然保留手动参数，不依赖 Profile
xmake run gmtools info example/Trinidad.dat \
  --first-line 6 --layout columnar --time-column 1 --input-unit g

# 新增 Profile 复用（具体配置文件由用户保存）
xmake run gmtools info example/Trinidad.dat --profile ./profiles/trinidad.json
xmake run gmtools convert example/Trinidad.dat --profile ./profiles/trinidad.json \
  --to csv -o ./build/output/

# 显式参数优先于 Profile，对冲突给出必要诊断
xmake run gmtools info example/Trinidad.dat --profile ./profiles/trinidad.json \
  --input-unit cm/s2
```

注意：对于单个文件的临时转换，一条普通 `gmtools convert` 命令仍应足够。不得强制用户先创建 Profile。

## 6. M2：K-NET / KiK-net ASCII 的最小可靠适配

### 6.1 接入位置和输入输出能力

首版增加 `KnetAscii`（具体枚举/命令名称可结合现有代码简洁确定），作为**只读标准文本格式**注册到当前静态格式表。

推荐逻辑：

```text
单个文件的原始字节
    ↓ UTF-8/ASCII 解码
K-NET ASCII 签名与头段提取（固定字段或按字段标记定位）
    ↓ 采样频率、方向、记录时间、标定系数、数据起始位置
TextParseOptions(layout=Sequential, first_line=...)
    ↓ 共用 Text Parser 读取整数值序列
明确应用仪器 Scale Factor（counts → cm/s²）
    ↓
FileData { Trace(单通道、dt、时间、quantity/unit、metadata) }
    ↓
现有单通道 Writer（TXT/CSV/AT2/SAC/... 按各自约束）
```

不要因为官方提到 K-NET 三分量或 KiK-net 六分量，就自动搜索、读取或合并其他文件；**一个 K-NET ASCII 输入文件只产生它实际存储的一条通道**。

### 6.2 头段与数据规则

- 检测至少基于规范性的 `Origin Time`、`Station Code`、`Sampling Freq(Hz)`、`Dir.`、`Scale Factor` 等组合特征，不能单凭 `.NS`、`.EW`、`.UD`、`.NS1` 等文件后缀进行判断。
- 解析 `Sampling Freq(Hz)`，要求正且有限，得到 `dt = 1/fs`。保存规范中明确的台站、方向、记录背景等扩展元数据；不需要把所有头段字段变成 Trace 的固定成员。
- 数据区以空白分隔，样本按行优先连续展开；八列数值只表示排版，不是八个通道。允许最后一行不足八值，严禁按列组装八条 Trace。
- 头段中的 `Duration Time(s)` 和 `Max Acc. (gal)` 可用于核验/诊断，不得据此静默截断波形，也不得把头段最大加速度视为原始样本的 `max(abs(samples))`：官方说明该值计算前去除了整段的均值偏置，而本项目禁止隐式去均值。
- 不合理或缺失的采样率、无效的 Scale Factor、非法的数值行应准确报错（必要时含行号），不以一个默认数字继续处理。

### 6.3 标定与物理单位（本阶段最重要的正确性要求）

NIED 官方给出的原始样本是 `counts`，头段 Scale Factor 可表示为类似 `3920(gal)/6182761` 的比例：

\[
 a_i[\mathrm{cm/s^2}] = C_i\,\frac{3920}{6182761}
\]

这属于**数据文件规定的仪器数值标定**，不是用户人为调幅。建议：

1. 在 Reader 中验证并解析 Scale Factor 的数值和物理单位，对数据样本**明确执行一次**标定，结果存入 `Trace::samples`。
2. 标定后的 `Trace::quantity = Acceleration`，`Trace::unit = Cmps2`；在 `attributes` 中记录原始数字单位、Scale Factor 原文、实际系数和来源。原文件本身不得改变。
3. 该标定与 `--numeric-factor`（用户额外数值处理）及 Writer 的 `--output-unit`（物理单位换算）必须分离。不能在 Reader 和 Writer 两处重复乘 Scale Factor。
4. 若 Scale Factor 的分子、分母、单位无法可靠解释，则**不能**将原始计数直接标记为 g/cm/s²；应返回清晰错误或明确的 counts 读取能力及转换限制。第一版不要求为不合法的标准文件制造成功结果。
5. 不额外保存庞大的原始计数数组副本；保留来源和换算元信息即可。转换属于数值物理解释，并不承诺字节级无损往返。

### 6.4 时间与方向

- NIED 格式中的 `Record Time` 是触发记录时间，官方说明公开波形包含触发前 15 秒；首样本起点按该规则计算，不可直接把 `Record Time` 当首样本时间。
- K-NET/KiK-net 文件的当地记录时间按 JST（UTC+09:00）解释，转换 UTC 并明确记录时区/预触发时间的来源；必要时以 ObsPy 等独立工具核对。若时间字段无效，保留未知并给出诊断，不能猜造绝对日期。
- `Dir.` 可能为 `N-S`、`E-W`、`U-D` 或带 `1/2` 的地下/地表标识。保存原始方向标记；允许生成简明通道名称，但不要做未经许可的坐标旋转或方向修正。

### 6.5 第一版明确不做

- 不写出 K-NET ASCII/Binary/CSV 专有格式；只要求能够读成通用 Trace，再使用当前已经存在的 Writer 导出。
- 不把 K-NET 和 KiK-net 的多个独立文件组合为三分量或六分量。
- 不自动减均值来“匹配”头段最大加速度；不执行仪器响应校正。
- 不开发 K-NET Binary（Kyoshin-WIN32）、K-NET CSV、WIN32 等其他格式适配；若实际样本驱动且成本很低，可另作独立增量任务。

CLI 使用示意（最终格式名由实现确认）：

```bash
xmake run gmtools info /path/to/station.NS --format knet-ascii
xmake run gmtools convert /path/to/station.NS --format knet-ascii \
  --to at2 -o ./build/output/
# 由于已按官方 Scale Factor 转换为 cm/s²，AT2 Writer 再转换为 g。
```

## 7. M3 / M4：其他标准文本格式的条件性工作

### 7.1 PEER VT2 / DT2

当前 PEER AT2 Reader 已具备提取头段、采样参数并复用 `parse_text()` 的机制。VT2/DT2 在组织方式上值得复用，但**不能只是给扩展名换一个名字**：

- 需要读取文件实际的 `VELOCITY`、`DISPLACEMENT` 类头段，识别量和**声明单位**；不得把速度/位移数值标为加速度 g。
- 现有 `Unit` 主要面向加速度。若实际样本要求支持速度/位移单位，按最小必要增量增加对应单位及合法量纲组合（例如 `cm/s`、`m/s`、`cm`、`m` 中真实文件使用的部分），不要不加判断地复用加速度换算系数。
- Reader 可先完成，Writer 只有在格式规范及独立回读确认后再增加；完整格式支持清单须逐项注明读/写能力。
- 如本轮无法取得可复现的真实样本，则仅记录调查结果与清晰的后续任务，不算本阶段遗漏。

### 7.2 COSMOS / SMC

COSMOS 的产品类型和数据段可能包含不同处理级别及加速度、速度、位移等不同波形或计算结果；其他机构的 `.smc` 文件不一定就是 COSMOS 文件。

本阶段只需提交一份简短调查记录，回答：具体源文件属于哪一种规范/版本？头段和数据区是否可由现有 Text Parser 表达？是否需要多段/定宽或多个物理量的支持？需要哪些实际文件才能验收？

**未经样本证明，不新增一个声称支持所有 COSMOS/SMC 的 `FormatId`。** 如发现一个简单且常见的稳定子类型，可留给下一轮单独开发。

## 8. CLI 和 GUI 设计变更

### 8.1 CLI

保持 `formats`、`info`、`convert` 三个入口，不增加工作流命令。

- 增加 `--profile FILE` 用于读取文本解释配置；解析配置同样适用于 `info` 和 `convert`。
- 在 `formats` 中显示 `knet-ascii`（实际命名可统一调整）的输入/输出能力及备注（只读、原始 counts 需标准 Scale Factor）。
- `info` 能展示标准格式的采样率、单位、通道、起点，以及关键标定来源；必要时将细节放在扩展属性，而不新增每个格式专有的 CLI 参数。
- `--format` 保持明确覆盖自动检测的能力；但不要允许错误的标准格式头段在不报警的情况下被当作合法记录。
- 参数帮助、错误退出码、路径/覆盖行为与第一阶段保持一致。

### 8.2 GUI

保持当前文件—通道—时程—属性—导出单窗口布局，不重做主界面。

- 导入对话框增加标准格式选项，以及“载入配置 / 保存配置”入口。仅在通用文本场景展示必要的 Profile 控件；标准格式自动填充其可识别的元信息。
- 对有明确文本表头的文件展示建议起始行、时间列和单位，让用户能够检查、应用或覆盖；原始文本和行号仍可查看。
- K-NET ASCII 文件应像其他单通道文件一样正常预览，标定后的物理量/单位与采样参数清楚可见；原 Scale Factor 来源可在信息区查看。
- 保持当前 GUI 的 `Preview parse` 与确定性错误反馈；不必引入实时持续重解析、完整格式向导、多个弹窗的配置管理系统。
- 本轮 GUI 只做必要的一次人工操作检查和已发现问题的修复；不要编写大量截图比较或自动 UI 回归。

## 9. 开发里程碑与建议提交顺序

### M0 — 样本与参考数据

- 获取并检查至少一份真正的 K-NET ASCII 文件及其许可条件；整理关键头段、格式特征、官方时区/标定说明。
- 以独立读取方法核对基础数值和时间；记录样本 SHA-256、来源及预期结果。
- 若条件允许，再准备 KiK-net ASCII、PEER VT2/DT2 和一种具体 COSMOS/SMC 样本，后两者只用于判断是否有必要立刻实施。

**交付**：`docs/validation/` 下新增第二阶段样本清单，不以猜测填充参考数值。

### M1 — 通用文本辅助识别与 Profile

- 小范围增加有依据的文本参数建议，不改动 `parse_text()` 两种布局的基本语义。
- 实现 JSON Profile 的保存、读取、版本检查和参数校验，统一应用于 CLI/GUI。
- 用 Trinidad、普通两列表格、时间+多通道 CSV 及一份故意歧义文本进行核验。

**交付**：可用的 `--profile`、GUI 载入/保存配置，以及关键规则说明。

### M2 — K-NET ASCII 适配

- 增加静态格式项、检测与专用头段解析。
- 复用数值解析器；实现一次仪器标定、JST/预触发时间处理、方向/台站属性保存。
- 仅接入 Reader，目标输出继续走现有单 Trace Writer。

**交付**：一份实际 K-NET ASCII 可 `info` 并能转换为 TXT/CSV/AT2，且独立核验数值和时间；有实际 KiK-net 样本时再覆盖后缀方向字段。

### M3 — 条件性 PEER 家族扩展 / COSMOS 调查

- 优先调查 PEER VT2/DT2 的样本、单位和格式规则；有充分参考再实施相应 Reader/Writer。
- 调查 COSMOS 与 SMC 的具体格式子集与样本要求，不因名称相近合并为一种格式。

**交付**：新增可验证格式（如实施）或明确的格式调查与后续待办；未实现不能宣称支持。

### M4 — 回归、人工核查与文档收尾

- 复核四份第一阶段真实样本及关键输出行为。
- 核对标准新格式的数值、时间、标定与导出结果，完成一次 GUI 人工核查。
- 更新 `README.md`、`formats` 能力列表及 `docs/validation/` 验收记录；保留实验性标签与未支持项。

**交付**：第二阶段验收结论及格式支持边界。

提交信息保持简洁，例如：`feat: add text profiles`、`feat: read knet ascii`、`fix: clarify text detection`、`docs: update format support`。无须为每个微小工作拆分大量提交。

## 10. 精简验收用例及判定标准

| ID | 样本 / 场景 | 必须检查的结果 | 状态要求 |
|---|---|---|---|
| P2-T1 | Trinidad.dat + Profile | 与第一阶段参考一致：1 Trace、2141 样本、时间列解释一致；显式 CLI 参数能按约定覆盖 Profile | PASS |
| P2-T2 | 通用两列与带表头三列 CSV | 有依据的时间/单位建议正确；若歧义无法确定，仍可手动选择；无静默误读 | PASS |
| P2-T3 | Profile 非法/未知版本 | 有清楚错误；不丢配置内容后继续产生貌似成功的导出 | PASS |
| P2-T4 | K-NET ASCII 真实样本 | 样本数、顺序、dt、Scale Factor 及加速度数值与独立读取路径一致 | PASS（真实样本） |
| P2-T5 | K-NET 时间与属性 | 验证 Record Time、JST/UTC、15 s 预触发、通道和关键来源字段 | PASS（真实样本） |
| P2-T6 | K-NET → AT2/CSV | AT2 输出按 g 换算，CSV 保持选定/原单位数值；单输入只产生自身的 Trace，不跨文件合并 | PASS |
| P2-T7 | 不合法 K-NET 头段副本 | 采样频率/标定系数无效时明确失败，不将 counts 冒充加速度；不覆盖原件 | PASS |
| P2-T8 | 旧格式回归 | Helena AT2、Trinidad、IES SAC、Hualian miniSEED 的 N/dt/样本与第一阶段参考一致；原有 Writer 不退化 | PASS（已编入对应依赖时） |
| P2-T9 | GUI 一次人工操作 | 加载/应用/保存 Profile；标准格式自动元信息；查看波形、导出单通道无明显错误 | PASS |
| P2-T10 | PEER VT2/DT2 或 COSMOS | 有样本则独立核验；仅调查的项目填写 `NOT IMPLEMENTED` 或 `BLOCKED`，不得写 `PASS` | 条件性 |

**校验方式**：每新增一种真正的标准文本格式，以 1～2 份真实文件为主；使用官方头段和独立读取程序或独立脚本核对 N、dt、方向、单位、首样本时间、样本前后值、极值及必要转换误差。K-NET 校验优先直接比较数字计数乘头段 Scale Factor 的结果，不将“程序导出后又由本程序回读一致”作为唯一依据。

**数值与时间容差**：说明物理含义和精度来源；float64 文本转换与头段比例标定可按浮点误差设限，SAC 与 miniSEED 沿用第一阶段已记录的编码精度要求。不得用大容差掩盖采样顺序、单位倍数或固定时区错误。

**复现记录**：记录当前 Git SHA、编译选项、测试命令、样本 SHA-256 和结果摘要。只保留少量关键脚本/实际需要的可复现配置；完整日志与大量导出文件留在本地构建目录。修复后仅重复相关用例和一次必要回归，不扩展为大型测试体系。

## 11. 风险与处理规则

| 风险 | 不当做法 | 本阶段处理方式 |
|---|---|---|
| NIED 标定不清楚 | 将 counts 直接设为 g 或 cm/s² | 验证 Scale Factor 结构和物理单位，应用一次并记录来源 |
| K-NET 时刻理解错误 | 把 Record Time 当首样本 UTC | 根据官方触发前 15 s 与 JST 规则处理，独立核对 |
| 通用文本过度自动化 | 根据两列数据猜测时间列，直接输出 | 仅提出可靠建议，不确定时保持手动选择 |
| Profile 与头段冲突 | 静默覆盖标准文件的必要定义 | 明确优先级与冲突诊断，允许切换通用文本解释 |
| PEER 速度/位移 | 误用加速度 g、cm/s² 的单位换算 | 按真实样本扩展量纲和合法单位；可先只读 |
| COSMOS/SMC 范围过大 | 一个适配器声称兼容所有版本、处理级别 | 先定义具体子类型，取样本后逐项开发 |
| 格式扩展破坏旧数据 | 修改 Trace/Writer 基础规则以适应一个新格式 | 先采用格式专用头段、扩展元数据和公共数值解析 |
| 示例数据公开 | 未核对许可即把实际记录和大量输出提交 GitHub | 记录来源、引用和再分发条件；保留非公开的本地样本路径方案 |

## 12. 开发完成后的交付清单

开发完成时应能交付：

1. 更新后的 C++ 源码：保持现有核心接口与静态格式表，新增最小必要模块。
2. `--profile` 与 GUI 解析配置复用功能，以及一份简洁的 Profile 格式/优先级说明。
3. 经过真实数据核验的 K-NET/KiK-net ASCII 单文件 Reader（其中 KiK-net 特有方向若未获得真实样本，应如实缩窄支持声明）。
4. 按实际工作量选择的 PEER 家族增量或其待实现调查记录，以及 COSMOS/SMC 的格式范围调查结果。
5. `docs/validation/` 第二阶段结果：样本来源和 SHA-256、官方与独立对照、验收清单、已知限制。
6. 更新后的 `README.md` 与 `gmtools formats`：读/写能力、实验性范围和实际命令相互一致。

### 阶段结束标准

- M1 与 M2 的必需验收用例通过；没有已知的静默数据、单位或时间错误。
- 不破坏第一阶段四份参考样本及 TXT/CSV/PEER/SAC/miniSEED 的既有已验收行为。
- 标准文本扩展实际复用了基础数值读取，而不是重新建设一套互不兼容的解析器。
- CLI/GUI 能让用户查看自动提取及修正的参数，保持简单直观。
- 尚未完成的格式和编码子集清楚标注，不以未验证的功能扩大“支持”范围。

**最终开发原则**：GMTools 的价值在于“让用户以更少操作得到可核验、物理含义正确的地震动文件”，不是在短时间内堆叠最多的格式名称。因此，第二阶段应首先做好 K-NET ASCII 的标定与时间问题，以及通用文本解析规则的复用，再决定下一种标准格式。
