# GMTools 第三阶段开发文档：常用格式兼容性与转换能力完善

> 项目：EETB / GMTools  
> 文档性质：完整阶段开发任务说明（供 Codex 实施）  
> 制定日期：2026-11-25  
> 仓库：<https://github.com/FeiyueDong/eetb>  
> 开发基线：`main` / `cce2069`（`update to xmake`），包含 `186efea` 的 GUI 整理  
> 关联文档：`docs/PROJECT_PRINCIPLES.md`、`docs/GMTOOLS_PHASE2_DEVELOPMENT.md`、`docs/ARCHITECTURE_AND_UI_REVIEW.md`、`docs/validation/GMTOOLS_PHASE1_RESULTS.md`、`docs/validation/GMTOOLS_PHASE2_RESULTS.md`  
> 阶段定位：**以实际科研使用为导向，完善 miniSEED、SAC、PEER 和通用文本的读取、解释与导出能力，不再以新增格式数量为目标。**

## 1. 开发背景与目标

前两个阶段已建立独立 Trace 数据模型、统一格式读写接口、通用文本解析器、Profile、CLI 与 Qt GUI，并完成 TXT/CSV、PEER AT2、部分 SAC 和 miniSEED 的实际样本验收。第二阶段增加 K-NET ASCII 适配与文本辅助识别，但部分运行验收尚未完成。近期 GUI 已按职责拆分，引入 Qt Concurrent 后台任务、波形概览包络与预览复用；构建系统已迁移至 xmake。核心接口和格式实现未因这两次调整而改变。

第三阶段不再盲目扩大格式覆盖，而应使当前最常用的记录能够可靠读入、补充必要信息、转换物理单位，并输出适合后续分析的独立文件。主要面向以下场景：

- 从标准 miniSEED 或日常结构监测使用的 Modified miniSEED 中读取多通道记录，检查其时间、标定与通道信息，按通道输出物理加速度或原始数值。
- 读取 SAC 时程，保留有效的时间与台站信息，转换为文本、miniSEED 或 PEER；按需生成 SAC v6/v7。
- 读取和写出 PEER AT2，并逐步完善同一家族的 VT2/DT2 速度和位移记录，支持同量纲单位换算与显式调幅。
- 读取非标准文本时继续使用通用解析规则和 Profile；输出时可选择纯数值、时间与幅值或带基础元信息的文本，方便 MATLAB、Python、OpenSees 等程序使用。
- 将缺失的采样信息、时间、标识和单位通过明确的用户操作补齐，而不是在格式转换时隐式猜测。

本文件将此前规划的第三阶段 A、B 合并为一个完整阶段，仍按依赖关系分步实施、分项确认。优先完成公共能力、Modified miniSEED 读取和文本输出，再完善核心标准格式写出。各模块独立验收；不要求一次提交全部变更。

## 2. 基线、开发范围与约束

### 2.1 当前支持基线

| 格式 | 当前读取 | 当前写出 | 第三阶段目标 |
|---|---|---|---|
| miniSEED 2/3 | libmseed 适配，允许 INT16/INT32/FLOAT32/FLOAT64/Steim-1/2 | v2/v3 FLOAT64，存在时间和源标识约束 | 完善标准读写、编码选项、时间和标识校验 |
| Modified miniSEED | 尚未接入 | 无 | 复用 REST 底层逻辑，实现只读适配与明确标定 |
| SAC Binary | v6/v7、大小端、等间隔 ITIME | 小端 v7、float32 | 增加 v6 输出，完善常用头段、时间及单位语义 |
| PEER AT2 | 已支持 | 已支持，输出 g | 保持兼容，统一 PEER 家族逻辑 |
| PEER VT2/DT2 | 未支持 | 未支持 | 按规范和真实样本完成同类读写；无样本则如实记录未验收 |
| TXT/DAT/CSV/TSV | 通用文本解析、时间列、Profile、辅助建议 | 单列幅值，CSV/TSV 有简单列名 | 增强导出布局、表头、精度及回读；少量通用解析完善 |
| K-NET/KiK-net ASCII | 实验性，只读，正式样本待验收 | 无 | 保留现有能力，不扩大本阶段范围 |

“已实现”不等于全部版本和编码均已通过实际验证。第一阶段的四份样本验收只代表其对应实现和子类型；第二阶段记录中仍有待运行事项，GUI 整理后的实际操作亦未完成核验。

### 2.2 不变的项目原则

- 单个输入文件是一次解析的边界；一个文件可得到多条独立 Trace，不跨文件配对、同步或自动合并。
- 一个 Writer 每次写一条 Trace，每条通道独立生成文件。继续复用 `export_file()` 的路径检查、冲突保护和暂存发布机制。
- 核心继续保持 Qt-free；CLI、GUI 共用 `read()`、`write()`、`export_file()`、公共单位/标定/元信息函数。不要复制算法逻辑到 GUI。
- 保留 `FileData / Trace`、静态格式注册表、共享 Text Parser 和已有 Profile；仅增加确有需求的字段和配置。无需新增 Application、工作流、动态插件或大型元数据体系。
- 不进行隐式滤波、积分、基线修正、对齐、填零、重采样或分量旋转；仪器标定和显式单位换算是转换语义，不是信号分析。
- 识别不了的格式、单位、时间、标识和编码应给出明确诊断；不得为使导出成功而伪造信息或 silently fallback 到其他格式。
- 已有 CLI 命令、Profile schema v1 和默认导出行为尽量保持可用；确需改变旧行为时在 README 中说明原因及迁移方案。
- 保留近期 GUI 拆分、后台任务、预览失效及复用、极值包络、通道勾选、输出冲突提示等机制。完整波形是 Writer 的唯一数据来源。
- 仅使用 xmake 构建，不恢复 CMake。无用户明确授权，不自行启动程序测试、GUI、截图或视觉检查；不建设大量自动化测试，不自行提交 Git。开发完成后报告实际执行的静态检查、编译和未执行的验收。

### 2.3 本阶段不做的事项

不增加 COSMOS/SMC、SAC ASCII、其他 NIED 格式或不常用二进制格式；不实现 Modified miniSEED Writer、SIT 容器、StationXML 全量仪器响应去卷积、多文件批量任务、非等间隔时程、频谱/反应谱或复杂绘图交互。保留后续需求入口，但不要为这些功能预建框架。

## 3. 实施总览

| 模块 | 任务 | 优先级与依赖 |
|---|---|---|
| M0 | 核查最新基线、格式规范、样本与依赖 | 先行 |
| M1 | 公共物理量、单位换算与数值处理 | 必做；所有格式共用 |
| M2 | 公共元信息与参数编辑、目标写出配置 | 必做；为跨格式转换提供基础 |
| M3 | 标准 miniSEED 读取核查、Modified miniSEED Reader | 必做；依赖 M1/M2，正式验证取决于样本 |
| M4 | 通用文本 Writer 增强与自描述文本回读 | 必做；依赖 M1/M2 |
| M5 | SAC v6/v7 与常用头段、单位及标识映射 | 必做；依赖 M1/M2 |
| M6 | PEER AT2/VT2/DT2 家族完善 | 必做设计；VT2/DT2 正式验收需真实样本 |
| M7 | 标准 miniSEED 写出编码、标识与时间 | 必做；依赖 M1/M2/M3 |
| M8 | CLI、GUI、xmake、文档与分项验收 | 贯穿开发；按授权执行验收 |

推荐顺序：M0 → M1/M2 → M3/M4 → M5/M6/M7 → M8。M1/M2 与其他模块可按实际代码关系小步集成。缺少某个格式的正式样本时，不阻塞其他模块，但不能将该格式标为已正式验收。

## 4. M0：基线与资料准备

开发前检查 `git status`、当前提交、公开接口、xmake 配置及现有四份示例文件；保留用户未提交的改动。若实际基线与本文不同，先核对差异，不覆盖已有实现。

### 4.1 已有基准样本

| 样本 | 第一阶段参考结果 | 本阶段用途 |
|---|---|---|
| `example/20240902_Hualian_TAPBA0.mseed` | v2 FLOAT32，28 通道，每通道 72000 点，dt=0.005 s，首时刻 2024-09-02T08:26:25Z | 标准 miniSEED 多通道、分段、独立导出、v3 与文本转换 |
| `example/IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac` | 小端 v6，60000 点，DELTA≈0.01 s，B=-120.5 s，首时刻 2025-02-07T16:44:58Z，量与单位未知 | SAC v6/v7、时间、标识、幅值保持 |
| `example/RSN1_HELENA.A_A-HMC180.AT2` | 5093 点，dt=0.01 s，g | AT2 兼容、单位换算、文本回读 |
| `example/Trinidad.dat` | 第6行数据，2141 点，dt≈0.01 s，时间列与 g 加速度列 | Profile、文本建议、输出布局和元信息回读 |

上述为历史参考值，不表示本轮已运行。原件不修改；输出、派生测试文件放入忽略的 `build/` 子目录。完整参考值、SHA-256 及来源限制见第一阶段验收记录。

### 4.2 需要补充的样本及参考

- Modified miniSEED：从 REST 已有合法可用样本中选至少一份典型 `wc/wt/ws` 记录（不要求三个标识各取一份，覆盖范围按实际样本记录），包含原始整数、标定、时间与通道信息；先检查原始协议定义、头段和独立参考。不得将 REST 的现有输出无条件当作时间基准。
- 标准 miniSEED：优先选择一个 Steim-2 记录，另用 FDSN 官方参考文件或独立工具生成小型记录，覆盖 INT32/FLOAT32/FLOAT64 及必要时间边界。避免为了遍历编码排列组合而大量建样本。
- SAC：现有 v6 样本可作为主要参考，必要时增加一个大端或 v7 独立参考；不要求所有字节序、版本、单位组合的笛卡尔积。
- PEER：取得有明确头段的 VT2、DT2 文件；若无法取得，可以根据已确认的规范编写小型确定性夹具进行开发验证，标记为夹具而非真实样本；无可靠单位/结构依据则暂缓对应支持。
- K-NET：第二阶段已有 ObsPy `test.knet` 夹具的独立参考；有授权时可补充一次基本回归。无需专门获取 NIED 登录凭证或为其扩大阶段工作。

下载及再分发权需明确；未知许可的实际数据不得擅自提交到公开仓库。允许仅保存样本来源、哈希、获取步骤和参考结果。参见第 14 节资料。

## 5. M1：公共物理量、单位与数值处理

### 5.1 单位体系

保留现有 `Quantity` 与 `Unit` 枚举思路，将 `Unit` 扩展为最小必要的同量纲单位集合：

| Quantity | 单位 | 转到对应 SI 单位的系数 |
|---|---|---|
| Acceleration | g、m/s²、cm/s²（Gal）、nm/s² | 9.80665、1、1e-2、1e-9 |
| Velocity | m/s、cm/s、mm/s、nm/s | 1、1e-2、1e-3、1e-9 |
| Displacement | m、cm、mm、nm | 1、1e-2、1e-3、1e-9 |
| Counts | counts | 无物理单位换算系数 |
| Unknown | unknown | 无 |

加速度 mm/s² 等其他单位按实际需求再加入；不为了未来可能的格式扩展而穷举。若 Modified miniSEED 的协议给出无量纲 Dimension，应保留其原始含义；只有明确需要且语义可靠时才增设 Dimensionless，不能将未知数值自动解释为加速度。

集中定义单位名称、常见别名、量纲和 SI 系数，例如 `m/s2`/`m/s²`、`cm/s2`/`gal`、`nm/s/s`。使用单一单位描述表或等价简单机制，统一 `name()`、`unit_from_name()`、单位所属量、兼容性检查和换算函数；替换只针对加速度的 `acceleration_factor()` 相关调用。CLI、GUI 的单位选项和 Profile 字段读写应引用同一套规则，避免多处手工维护不同名单。

同物理量转换为：

```text
x_out = x_in * (k_in / k_out)
```

已知输入输出单位必须属于同一物理量；未知单位不能进行物理换算。对显式系数和结果检查有限性、溢出/不可接受的下溢。Counts 与物理量之间只能通过标定转换。禁止通过单位选项进行积分、微分等量纲改变。

### 5.2 明确四种不同操作

| 操作 | 数值是否改变 | 约束 |
|---|---|---|
| 输入量/单位解释（override） | 否 | 解释或修正现有数据的标签；不等价于标定 |
| 仪器标定（calibration） | 是 | 依据有效灵敏度/比例将已知 counts 转成对应物理量；记录来源和应用状态 |
| 用户数值缩放（numeric factor） | 是 | 用户明确要求的乘数，独立于标定；可用于调幅 |
| 输出单位换算（unit conversion） | 是 | 同量纲换算，只修改用于写出的临时数据 |

建议执行次序：**解析原始数值及元信息 → 选择原始或标定解释 → 显式用户缩放 → 输出单位换算 → 目标编码**。单位指定作用于该次解释的数值，若与已知标准标定冲突，应拒绝或明确区分重新解释与物理换算，不允许静默重标。

- 标准 miniSEED 的编码仅说明存储方式，整数编码不自动证明是未经标定的 counts；没有可靠量和单位信息时保持 Unknown，允许用户明确解释。
- Modified miniSEED 可默认保留已知原始 counts，并提示文件内的量/灵敏度；提供明确的“应用文件标定”选项，标定后量和单位与协议一致。对零值、非法值、不可确认的量纲不执行标定。
- K-NET 已有标准要求的标定语义保持不变：Reader 按文件 Scale Factor 标定，额外用户缩放在其后执行；不得重复应用系数。
- 用户手动标定采用明确的系数语义（例如 SI-unit/count 或指定单位/count），必须同时确认目标量与单位。不要允许一个无量纲的调幅系数隐式解锁 counts → 物理量。现有 `apply_overrides()` 依据 `text.numeric_factor` 判断是否已标定的逻辑应重新审查并改为明确的标定状态。
- 记录原始数值类型、原始量/单位、标定来源、实际因子、是否应用、用户缩放与单位覆盖来源；不要复制保存一份庞大原始数组。需要恢复原值时，从保留的原始文件重新解析。

### 5.3 公共数值缩放及兼容性

目前 `numeric_factor` 位于 `TextParseOptions`，SAC/标准 miniSEED 因二进制格式检查不能复用。建议将实际执行位置统一到 Core 的通用读入处理，文本专有配置只描述文本布局。保留 CLI `--numeric-factor` 和 Profile v1 的 `text.numeric_factor` 兼容输入，将其映射为同一个公共选项，不重复应用。若同时引入新的序列化位置，应明确冲突规则（相同值只执行一次，不同值报错），并记录迁移方式；优先维持现有 JSON schema，不为移动字段单独升级版本。

缩放可适用于加速度、速度、位移及原始数值，但任意缩放后的值不能继续声称是未经修改的原始整数 counts。保持已有有限、非零乘数约束；1.0 不导致实际数值变化，负数表示显式反号。是否扩展为导出端单独调幅暂不要求；本阶段保证读入/重新解释可设置，输出单位换算仍不修改当前 Trace。

## 6. M2：公共元信息、属性编辑及 Writer 参数

### 6.1 元信息与标识

继续保留 `Trace::name`、`dt_seconds`、`relative_start_seconds`、`start_time`、`quantity`、`unit`、`attributes`。不新增独立台站对象或跨文件分组。`name` 定义为显示名称，不能作为唯一写出标识来源。

公共元信息建议使用现有 Metadata 加少量 typed helper，或者最小的标识结构（由开发者选择一种一致实现）。建议语义：

| 公共字段（名称示意） | 含义 | 来源/映射 |
|---|---|---|
| `station.network` | 台网代码 | miniSEED NSLC/SID、SAC KNETWK |
| `station.code` | 台站代码 | miniSEED station、SAC KSTNM |
| `station.location` | 位置代码，可为空 | miniSEED location、SAC KHOLE |
| `channel.code` | 通道代码（不等于显示名） | miniSEED channel、SAC KCMPNM |
| `channel.source_id` | 完整 FDSN SID，保留无法无损拆分的 v3 标识 | miniSEED 2/3 |
| `channel.azimuth_deg` / `channel.inclination_deg` | 明确方向参数；有依据时填写 | SAC CMPAZ/CMPINC 等 |
| `calibration.*` | 原始计数、量、比例、单位、来源与应用状态 | Modified miniSEED、K-NET、用户设置 |

保留已有 `sac.*`、`mseed.*`、`peer.*`、`knet.*` 格式专有字段和原始信息。公共字段与专有字段发生冲突时必须有一致的优先级：明确编辑后的有效公共信息优先用于写出，原始专有信息用于追溯；不得悄悄从另一处旧值恢复。编辑 NSLC 时同步重新计算可表达的 SID，编辑 SID 时同步解释 NSLC，不能互相矛盾；不能无损映射时保留原标识并报告目标格式限制。

- miniSEED → SAC：映射台网、台站、位置与可表示通道代码，不将完整 `FDSN:...` 字符串强塞给 KCMPNM。
- SAC → miniSEED：使用完整且合规的公共标识；缺字段时提示补充，不自动虚构台网、台站、通道。位置空白与未知需要按格式规则明确区分。
- 对实际使用的两字符通道等非标准标识保留原文，不自动补字符或更名来绕过 v2 校验。允许用户显式映射并记录来源。v3 不保证所有原始标签都符合 FDSN SID 规范，必须依据目标校验。

### 6.2 时间与属性编辑

- `start_time` 始终表示首样本的绝对时刻（UTC）；`relative_start_seconds` 表示首样本相对于参考点的时刻，两者独立且可未知。时间输入采用明确时区的 ISO 8601（例如 `2025-02-07T16:44:58Z`），不要用本机时区无提示解释。
- 已有 dt/fs 互斥逻辑保持。修改采样率或起点不是重采样或移动样本数组；说明只改变时序解释。
- 支持在 Core 补充/修改绝对起点、相对起点及公共标识，统一 CLI 和 GUI 校验。CLI 本次输入级设置默认作用于相应解析记录；对于多通道不同标识不得隐式覆盖为同一个 SID，涉及单通道专属标识时要求明确选择通道或拒绝歧义。
- GUI 现有 Edit channel parameters 扩展少量字段，允许逐通道确认；提供“保持原值/明确设置/清除未知”的清楚语义。未知时间不自动变成 Unix epoch 0。
- K-NET 等标准格式已有强制采样、物理标定约束不应因为公共编辑而绕过。对已知源时间或标识的显式覆盖要保存原值或诊断。

### 6.3 Writer 选项组织

当前 `WriteOptions` 仅有 `format`、`output_unit`，需增加少量格式选项。建议按格式族组织结构，或使用简单 `std::variant`，不必为每种格式设计动态配置描述。以下为概念示意，最终命名随项目代码确定：

```cpp
struct TextWriteOptions {
    // amplitude only / time + amplitude
    // header style, delimiter, significant digits
};
struct SacWriteOptions {
    // version 6/7, unit convention
};
struct MseedWriteOptions {
    // encoding: float64 / float32 / int32 / steim2
};
// WriteOptions 保留 format, output_unit，附上述最小必要参数。
```

miniSEED 已有 `mseed`（v3）、`mseed2`（v2）格式 ID，继续兼容；不必再以第二个版本字段产生互相冲突的设置。SAC v6 可使用版本参数，或清晰的 `sac6` 目标别名，但不应维护第二套 Writer。选定一种权威语义并使 CLI/GUI 一致；对与目标无关的专有选项明确拒绝或不传入，不静默接受无效配置。

Writer 仍应收到完整 Trace 和已确认的写出选项；公共校验、单位换算、目标约束及序列化职责分开。多通道选择统一的输出单位和配置时，应先验证每条 Trace 是否满足要求；任一通道不满足时不提前发布其他文件。

## 7. M3：miniSEED 读取与 Modified miniSEED 适配

### 7.1 标准 miniSEED

继续使用 libmseed 3.x，不重新实现标准 v2/v3 解码库。保留 INT16、INT32、FLOAT32、FLOAT64、Steim-1、Steim-2 读取路径，依据规范和真实样本修复问题。

- 核查格式检测、记录长度、编码、样本数、源标识、采样间隔和时间戳；缺少 libmseed 时明确提示，不回退为文本。
- 保留原始 SID、编码、记录版本、时间精度及有用的扩展信息，映射至 M2 公共字段。
- 延续按文件记录顺序、同源且时间连续才合并的策略；间断、重叠、乱序及采样参数变化均保留独立片段，不排序、补零或强行拼接。对时间修正及 Blockette 100/1001 进行核查；合并容差必须受时间编码精度与采样间隔约束。
- 显示源通道及片段信息，导出仍逐 Trace 独立执行。整数编码本身不能证明数据一定是原始 counts，缺乏可靠单位时保持 Unknown。

### 7.2 Modified miniSEED Reader

REST 已有可复用实现，见 <https://github.com/FeiyueDong/rest/tree/main/src/rest_data/src/rest_data_tools/formats/mseed>：`modified_mseed.hpp/.cpp`、`steim2.hpp/.cpp`；`modified_mseed_export.*` 仅作为历史组织与标定语义的参考。

核查实际 REST 提交、协议与许可，复用底层头段解释和整数解码，不依赖 REST 工程、不引入其数据集、SIT 容器和上层按源顺序拼接的组织方式。适配现有字节流入口，增加独立只读格式 ID（建议 `modified-mseed`），通过 `wc/wt/ws` 特征结合固定头、Blockette 1000、记录长度、数据偏移与专有扩展布局识别。不得只凭两个字符认定合法格式，或将任意标准 miniSEED 解码失败的文件自动当成 Modified 格式。

```text
标准 miniSEED 2/3 ── libmseed ──┐
                                ├─ 记录级信息与样本 ── 公共片段组织与校验 ── FileData
Modified miniSEED ── 专用解码 ──┘
```

只在确有复用必要时提取小型记录描述或组织函数，不建立新的通用二进制框架。核对记录标识、数据包长度/序号、Blockette 1000 后八字节扩展、量纲、灵敏度、通道排列及 Steim-2 数据。

**时间处理必须单独核查。** REST 当前 `fixed_header()` 将 `start_time_us` 初始化为零，再叠加时间修正，后续 `parse_header()` 将其转回 BTime；不可直接作为真实绝对时刻。独立解释原始 BTime、年积日、0.0001 s 分数、时间修正标志和适用的微秒扩展，避免重复修正。无法解释的时间保持未知并诊断，无法证明连续性的记录分别保存；不以 Unix epoch 或记录序号虚构时间轴。不顺手修改 REST 仓库。

REST 的辅助标定公式为 `physical_value = count * 100 / sensitivity_raw`，其前提是扩展低 30 位保存以 100 倍编码的 counts/SI-unit 灵敏度。确认实际协议和样本一致后才采用；高位维度字段决定量纲，零或非法灵敏度不应用。保存原始字段和换算来源。

- 默认建议原始数值模式：保留整数 counts；标定模式由用户明确选择，输出对应 SI 物理量，再由公共单位换算处理。
- 若记录间量纲、灵敏度、通道身份或时间连续性变化，应分段或明确拒绝，不能只采用首记录的标定解释整个文件。
- 保留原始文件用于重新解释，不保存两份全量样本数组。Modified 仅要求读取，导出使用通用文本、SAC、PEER 或标准 miniSEED 的 Writer。
- 若专用解码不依赖 libmseed，可独立编译，不必受标准格式开关限制；否则明确注明依赖。

## 8. M4：通用文本导出与回读

### 8.1 输出预设和参数

继续共用 Text Writer，不增加格式 ID：

| 预设 | 布局 | 表头 | 用途 |
|---|---|---|---|
| 数值 | 每行一个幅值 | 无/简单列名 | 直接用于数值计算，保持默认 |
| 时间与幅值 | 每行一组时间、幅值 | 可选列名 | 时程交换 |
| 带基础元信息 | 幅值或时间与幅值 | 简明可识别头段 | 保存物理与时间信息，支持回读 |

可选项：布局、表头样式（无/列名/基础元信息）、分隔符、有效数字。保留 TXT/DAT 默认无头单列、CSV/TSV 默认单列和简单列名；CSV/TSV 分隔符与目标格式一致，TXT/DAT 可选空格、Tab、逗号。不引入复杂模板系统。

时间列采用秒，默认 `i*dt`（首样本起经过时间）；若支持保留原相对起点，应明确 `t0+i*dt` 的含义。绝对 UTC 放在头段，无需逐行写日期。缺少有效 dt 不得输出时间列。使用直接计算而非累加，默认十进制精度应足以回读 double；自定义幅值精度不应损害时间网格。列名和头段记录实际输出单位。

### 8.2 自描述头段与重新读取

建议定义简洁、有版本标记的文本头段（字段为示意，最终统一文档）：

```text
# GMTools waveform 1
# Channel: NS
# Quantity: acceleration
# Unit: cm/s2
# DT: 0.01
# NPTS: 4
# StartUTC: 2025-02-07T16:44:58Z
# RelativeStart: -120.5
# TimeBasis: elapsed
Time[s]  Accel[cm/s2]
0.00    0.012
0.01    0.034
0.02   -0.021
0.03    0.015
```

支持版本、名称、量、单位、dt、NPTS、可选绝对/相对时间和时间轴约定，按需保留公共标识；不要求序列化所有专有信息。未知字段省略或标为 unknown，不虚构值。明确数据段边界，依据特有标记提取规则后复用 Text Parser；不将任意注释当作本格式。核验 DT、时间列和 NPTS，冲突应诊断且不静默截断；经过时间列与原相对起点须区分。单样本时可保留头段的有效 dt。

完成要求：GMTools 生成的自描述文本能够由 GMTools 回读并保留承诺的元信息及精度。普通文本仍使用原 Profile。

### 8.3 输入侧小范围优化

支持实际常用的 Fortran `D/d` 指数（严格验证词元），完善明确速度/位移标签、单位别名。继续验证完整时间网格，建议需显式应用；多列不同单位不能用推测出的全局单位覆盖。固定宽度、GBK、复杂 CSV 引号按真实需求留待后续。

## 9. M5：SAC Binary v6/v7 与元信息

### 9.1 版本及时间精度

保留大小端 v6/v7 等间隔 ITIME 读取；增加 v6 写出，默认小端 v7 不变。共享 Writer 的头段和样本编码，不另建完整 Writer。v6 为 632 B 头段和 `4*N` 字节 float32 波形；v7 另有 176 B 双精度 footer。写入一致的 NVHDR、NPTS、DELTA、B、E、LEVEN、IFTYPE 及必要统计字段，按规范处理 footer。

尽量保持绝对首样本时刻与相对起点。v6 的时间字段为 float32，应核查实际编码后的 dt、B、参考时间及首末样本网格；明确报告精度损失，超出可接受范围时拒绝并建议 v7。波形 float32 舍入、溢出及不可接受下溢同样需要诊断。暂不增加非等间隔、频谱类或 ASCII SAC。

### 9.2 常用头段

| 类别 | 字段 | 要求 |
|---|---|---|
| 标识 | KNETWK、KSTNM、KHOLE、KCMPNM | 公共标识双向映射，避免静默截断 |
| 时间 | NZ*、B、E、IZTYPE | 保持参考点与首时刻含义 |
| 台站位置 | STLA、STLO、STEL | 有效值保留，未知不填零 |
| 方向 | CMPAZ、CMPINC | 保留规范含义，CMPINC 为相对于竖直向上的角度 |
| 事件 | EVLA、EVLO、EVDP、KEVNM | 保存可解释值，不建立事件模型 |
| 物理量 | IDEP、SCALE | 遵循下节，不凭 SCALE 自动换算 |

-12345 未定义标志不作为有效值。若参考时刻改变，对 O/A/Tn 等参考点相关字段不得原样复制；不能正确维护时明确不保留。目标为常用工程时程转换，而非逐字段无损归档。

### 9.3 规范单位与工程数值

SAC 传统定义的 IACC、IVEL、IDISP 分别对应 nm/s²、nm/s、nm，但实际工程数据可能采用 g 或 cm/s² 等尺度。默认读取保留原值及 IDEP 的量信息，单位需要可靠证据或用户明确采用 SAC 规范解释。SCALE 作为元信息，不自动应用。

写出提供两种策略：

- **保留选定数值尺度（建议默认）**：保留当前或明确选择的工程单位。若尺度与 IDEP 规范不一致，使用 IUNKN/未定义并诊断，不能静默写入暗示纳米制单位的标记。若确有旧软件兼容需求，非规范 IDEP 保留只能作为额外显式选项，提示风险。
- **SAC 规范单位**：物理量、单位明确时按 M1 换算为对应纳米制单位，写入匹配 IDEP；未知单位或未标定 counts 拒绝该模式，不重复应用 SCALE。

上述默认策略是对第一阶段无条件保留已知量 IDEP 的有意修正，应在 README 中说明；原默认 v7 和波形尺度不变，更新原有 IDEP 验收条件。不要宣称未知单位的 SAC 可以无损保留全部物理含义。

## 10. M6：PEER AT2/VT2/DT2

在 `peer.cpp` 统一头段、NPTS/DT、描述、Sequential 数值解析和写出逻辑，增加清晰的 VT2/DT2 格式 ID，避免复制三套 Reader/Writer。

| 类型 | 物理量 | 目标单位 |
|---|---|---|
| AT2 | 加速度 | g（保持现有） |
| VT2 | 速度 | 依据可靠规范/样本确认，常见 cm/s |
| DT2 | 位移 | 依据可靠规范/样本确认，常见 cm |

先核查实际产品头段及单位约定，不按扩展名或上述常见单位直接硬编码。若无法确认输出规范，先实现可证实的读取并记录写出受阻。

- 自动检测明确区分三种物理量，不只匹配 `PEER NGA` 或 NPTS/DT；显式格式与头段冲突时诊断，不能将 VT2/DT2 当作 AT2。
- 读取头段声明量、单位、DT、NPTS、描述，复用 Text Parser；DT 正且有限，NPTS 缺失或数量不符延续已有警告及保留全部样本策略。
- Writer 按目标物理量与实际单位验证，使用同量纲换算，准确生成声明单位、DT、NPTS 及描述，不虚构事件信息。不做积分/微分，不允许未知单位或未标定 counts 写成物理时程。
- Helena AT2 作为回归基线；VT2/DT2 使用真实样本和独立对照，若仅有夹具则标注实验性与真实样本待验收。

## 11. M7：标准 miniSEED 写出编码

由 libmseed 统一实现 v2/v3，保留 FLOAT64 默认，新增 FLOAT32、INT32、Steim-2。Steim-1 保留读取，不要求写出。

| 编码 | 条件与校验 |
|---|---|
| FLOAT64 | 有限数值，保留 double 样本 |
| FLOAT32 | 范围可表示，明确允许精度舍入；拒绝溢出和不可接受下溢 |
| INT32 | 每个数值可无损表示为 int32；不截断/四舍五入 |
| Steim-2 | 无损 int32，差分符合编码约束；编码失败不静默切换格式 |

整数编码不专属于 counts，浮点物理值不可为获得压缩而自动量化。若以后需要有损量化应独立设计。

保留 `mseed`（v3）、`mseed2`（v2）兼容入口。写出要求已知绝对首样本时间、正有限 dt、目标可表示的 SID/NSLC。使用 M2 公共字段，显示名称不影响标识；不能自动虚构、改名或回退到 v3。花莲两字符通道的 v2 限制仍须明示，用户可显式映射合法标识。保留编码后 v2 时间及标识核验，并对 v3、其他编码检查实际头段、样本数和时间网格。工程单位、仪器标定及方向等无法由目标标准表示时提供诊断，不虚构标准元数据或强制实现 StationXML。

## 12. M8：CLI、GUI 与 xmake 集成

### 12.1 CLI

保留 `formats`、`info INPUT`、`convert INPUT --to FORMAT -o PATH` 三个命令。新增参数直接对应 Core 选项，避免独立的 CLI 数据模型。以下命名为建议，实际实现应统一帮助、校验与 README。

| 参数建议 | 作用 |
|---|---|
| `--format modified-mseed` | 显式选择专有 Reader |
| `--calibration raw/source` | 保留原始数值或应用源标定；手动标定另给明确系数与目标单位 |
| `--numeric-factor FACTOR` | 通用缩放，兼容已有参数 |
| `--start-time UTC`、`--relative-start SECONDS` | 补充首样本时间 |
| `--network`、`--station`、`--location`、`--channel-code` 或 `--source-id` | 补充公共标识，与现有 `--channel N` 导出选择区分 |
| `--text-layout values/time-values`、`--text-header none/columns/metadata` | 文本输出布局、表头 |
| `--text-delimiter whitespace/tab/comma`、`--precision N` | 文本输出分隔符及精度，区别于输入 `--delimiter` |
| `--sac-version 6/7`、`--sac-unit-mode keep/standard` | SAC 版本及单位策略 |
| `--mseed-encoding float64/float32/int32/steim2` | 标准 miniSEED 输出编码 |

`info` 展示量、单位、时间、公共标识、片段及标定状态；`formats` 明确读写、实验性状态和可选依赖。未知、互斥或目标不适用参数应有明确诊断。以下是使用示意，最终按实际实现确定参数拼写：

```sh
# 兼容已有 Profile
xmake run gmtools info example/Trinidad.dat --profile example/trinidad.profile.json

# 标定后分通道输出工程文本
xmake run gmtools convert /path/to/modified.mseed --format modified-mseed \
  --calibration source --output-unit cm/s2 --to csv \
  --text-layout time-values --text-header metadata -o build/phase3/modified/

# SAC v6 输出
xmake run gmtools convert example/IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac \
  --to sac --sac-version 6 -o build/phase3/sac6/

# miniSEED FLOAT32 输出（源记录须有合法时间和标识）
xmake run gmtools convert /path/to/valid.mseed --to mseed \
  --mseed-encoding float32 -o build/phase3/mseed/
```

### 12.2 GUI

保留当前三栏布局和操作流程：打开文件 → 解析预览 → 选择/编辑通道 → 设置并导出。各模块职责如下：

| 模块 | 修改范围 |
|---|---|
| `override_fields.hpp` | 物理量与单位联动，从源/保持当前等语义，共用 Core 单位列表 |
| `parse_dialog.*` | Modified 格式、标定模式、通用缩放；保留 Profile 与建议、预览复用 |
| `main_window.*` | 通道时间、公共标识编辑；Summary 展示标定和分段；Export 格式专有选项 |
| `plot.*` | 保留当前包络机制；实际数值变化才重新生成显示包络 |
| `background_task.hpp` | 保留后台任务与数据所有权机制，不新增调度系统 |

标准文件已有字段自动读取，用户不必重复录入。Modified 文件显示原始 counts、灵敏度、标定状态、标定后量及单位，明确选择重新解释。修改读入/标定/缩放设置使预览失效，从原始字节重新解析，不重复作用于已标定数组。

Export 根据目标显示精简选项：文本布局、表头、分隔符和精度；SAC 版本与单位策略；miniSEED 版本和编码；PEER 对应量与允许单位。输出单位按选定通道的物理量过滤；量纲不一致时允许保持各自输入单位，不能误导用户统一换算。切换目标后不传递旧目标专有选项。Core 仍独立验证所有条件。

保持当前勾选数量、定位、高亮、覆盖保护、文件结果列表和格式帮助。后台解析/导出期间禁止修改正在使用的数据；工作函数不访问控件。纯元信息修改不重新扫描样本，真实数值变化时重建显示包络；Writer 始终使用完整波形。不进行无关的布局或主题重构。

### 12.3 构建与依赖

唯一入口为 `GMTools/xmake.lua`。保持 `gmtools_core`、`gmtools`、`gmtools_gui` 三个目标，将新源文件加入相应 target，保留 nlohmann/json 和 Qt Widgets/Concurrent 依赖。完整日常使用配置：

```sh
cd GMTools
xmake f -m release --build_gui=y --enable_mseed=y --mseed_sdk=/path/to/libmseed
xmake build -j 4
```

精简 CLI 仍可使用：

```sh
xmake f -m release --build_gui=n --enable_mseed=n
xmake build -j 4
```

SDK 路径以实际环境为准，不硬编码个人目录；保留明确的 libmseed 版本和 SDK 校验。日常构建应包含标准 miniSEED，但无需本阶段强制修改 `enable_mseed` 默认值；在 README 中说明完整构建方式。Windows 构建及发布按用户安排验证，不宣称未测试的平台兼容性。不恢复 CMake。

## 13. 验收计划与完成定义

### 13.1 测试原则与用例

以下是验收计划，不构成自动执行授权。遵守既有开发规则：未经明确要求，不自行运行测试、启动 GUI、截图或进行视觉检查，不自动提交。允许按任务授权完成必要静态检查及编译；记录实际命令、结果和限制。优先复用已有参考样本与少量独立核验，不建立大量测试或完整排列组合。

| 用例 | 核查内容 | 通过条件 |
|---|---|---|
| P3-T1 单位与标定 | 同量纲换算、未知单位、明确标定、缩放、旧 Profile 字段 | 与独立公式一致，跨量纲拒绝，标定/缩放不重复 |
| P3-T2 标准 miniSEED | 花莲 28 通道及一份 Steim-2/整数参考 | N、dt、UTC、标识、数值、片段边界正确 |
| P3-T3 Modified miniSEED | 实际 REST 样本的原始与标定模式 | counts、扩展字段、独立头段时间、物理数值正确；未知时间不伪造 |
| P3-T4 文本 | Trinidad Profile、AT2/miniSEED 导出三类预设 | 默认行为保持；时间、单位、头段及自描述回读正确 |
| P3-T5 SAC | 现有 v6 写 v6/v7，必要时补一份端序参考 | 独立回读确认样本、DELTA、B、UTC、版本、标识；单位策略和精度损失明确 |
| P3-T6 PEER | Helena AT2 回归、VT2/DT2 实际样本或明确夹具 | 量、单位、NPTS、DT 和样本正确；不同量不能直接转换 |
| P3-T7 miniSEED 写出 | 代表性 v2/v3 与四类编码 | 目标编码、样本、时间、标识正确；非整数/编码越界/缺信息正确拒绝 |
| P3-T8 交叉转换 | miniSEED↔SAC、SAC/PEER→文本、补齐信息后 TXT→miniSEED、Modified→文本/AT2 | 必要公共信息按目标能力保留，单位正确，损失或限制明确 |
| P3-T9 旧功能 | Trinidad 建议/Profile，K-NET 夹具（可获得时），覆盖及路径保护 | 已有原则不被破坏，原始文件不修改，失败不产生错误结果 |
| P3-T10 GUI 人工 | 标定、编辑时间/标识、输出选项、后台执行、预览复用 | 由用户或授权后实际操作核查，导出完整波形 |

独立参考优先使用原始头段、Python 数值计算、ObsPy、EarthScope/FDSN 工具或可解析实际目标结构的独立程序。确认参考软件支持的格式版本，不以 GMTools 自写自读作为全部证据。FLOAT32 按目标表示能力评估误差，整数写出要求无损；时间误差依据规范和采样间隔评估，不使用一个宽松阈值掩盖缺口。测试命令、派生文件和日志置于 `build/`，保留可公开的精简复现说明。

### 13.2 目标转换矩阵

| 输入 | 主要目标 | 必要条件 |
|---|---|---|
| 标准 miniSEED | 文本、SAC、miniSEED 2/3 | 保留时间与标识；物理单位需可靠解释；目标编码可表示 |
| Modified miniSEED | 文本、SAC、PEER AT2、标准 miniSEED | 原始或明确标定；AT2 需加速度；miniSEED 需 UTC/SID |
| SAC | 文本、SAC v6/v7、miniSEED、PEER | 依据目标补齐单位、标识与 UTC，明确单位规则 |
| PEER AT2/VT2/DT2 | 文本、同量纲 PEER、SAC、miniSEED | 不做积分；按需补足时间及标识 |
| 通用文本 | 文本、PEER、SAC、miniSEED | 用户补齐目标要求的 dt、量/单位、UTC、标识，不虚构 |

这是目标边界，而不是保证所有记录无条件互转。无法表达必要信息时拒绝写出可以是正确行为。优先保证日常 miniSEED/SAC/PEER → 工程文本链路。

### 13.3 交付及状态

代码交付包含 M1–M8 的全部原定内容；如实际规范、样本、依赖或用户授权不足，明确记录对应条件性任务的限制，不擅自删除计划。更新 `docs/validation/GMTOOLS_PHASE3_RESULTS.md`，记录最终版本、编译配置、实际命令、样本来源及参考值、修复和剩余事项，并区分：

- `IMPLEMENTED`：代码已实现；`COMPILED`：指定构建通过。
- `PASS` / `FAIL`：具有实际运行记录的验收结果。
- `NOT RUN`：未经授权或尚未执行；不写成通过或失败。
- `BLOCKED`：外部样本、依赖等受阻；说明解除条件。
- `NOT IMPLEMENTED`：未完成的任务或子类型，说明是否属于条件性范围。

主体代码开发完成与阶段正式验收完成分别报告。K-NET 正式样本受阻不影响第三阶段其他工作；Modified 和 VT2/DT2 缺真实记录时保持实验性或待验证状态，不声称完整兼容。

## 14. 参考资料与文档维护

- GMTools 当前源码与历史记录：<https://github.com/FeiyueDong/eetb/tree/main/GMTools>，基线 `cce2069245bbeb6d31de53baf163f870ab8d8242`。
- REST Modified miniSEED：<https://github.com/FeiyueDong/rest/tree/main/src/rest_data/src/rest_data_tools/formats/mseed>，移植时记录具体参考提交及许可。
- FDSN miniSEED 3：<https://docs.fdsn.org/projects/miniseed3/en/latest/>；编码：<https://docs.fdsn.org/projects/miniseed3/en/latest/data-encodings.html>；参考数据：<https://docs.fdsn.org/projects/miniseed3/en/latest/appendix.html>；源标识：<https://docs.fdsn.org/projects/source-identifiers/en/v1.0/definition.html>。
- EarthScope libmseed：<https://github.com/EarthScope/libmseed/tree/v3.5.3>。
- SAC 规范（NVHDR、footer、IDEP 等）：<https://ds.iris.edu/files/sac-manual/manual/file_format.html>。
- PEER NGA-West2：<https://peer.berkeley.edu/peer-nga-west2-online-ground-motion-database-updated-and-expanded>；VT2/DT2 具体产品单位与头段以真实文件及可靠规范为准。
- NIED K-NET ASCII：<https://www.kyoshin.bosai.go.jp/en/knetascii/>，保留既有标定约定。

README 更新格式支持矩阵、构建选项、量/单位/标定规则、公共信息限制、CLI 和 GUI 新参数、文本头段约定及各目标默认行为。历史验收记录继续保留，不将旧 CMake 命令当作当前构建入口，不将历史 PASS 自动移用至当前版本。

## 15. 实施提醒

本阶段不是建立更大的转换平台，而是使 GMTools 可靠处理实际最常用的地震动记录。公共机制只服务于已确定的需求；具体格式保持独立、简明的适配。优先完成读取、可靠解释、人工核查、单通道导出的实际闭环，再完善目标版本和编码。标准约定与工程习惯发生冲突时，应明确限制或要求确认，不能隐式改变数据的物理含义。
