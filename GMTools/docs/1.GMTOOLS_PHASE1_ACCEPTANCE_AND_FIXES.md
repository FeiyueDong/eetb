# GMTools 第一阶段验收、修复与完善开发文档

> 构建入口已迁移至 xmake。本文保留当时的 CMake 选项、命令和产物路径作为历史记录；当前操作请使用 [README 的构建说明](../README.md#构建)。

> 项目：EETB / GMTools  
> 文档类型：阶段性开发与验收任务说明  
> 阶段定位：**第一阶段收尾（真实数据验收 + 定向修复 + 少量可用性完善）**  
> 基线：GitHub `FeiyueDong/eetb` 的 `main` 分支，评审时最新提交 `d73045c`（`init repo`，2026-11-24，北京时间）  
> 依据：`GMTools/docs/PROJECT_PRINCIPLES.md`、`GMTools/docs/GMTOOLS_PHASE1_DEVELOPMENT.md`、现有 GMTools 源码与用户提供的 `example/` 文件清单  
> **说明**：目前只掌握四个本地样本的文件名和文件大小，**尚未读取其文件内容**；以下关于采样率、样本数、通道数、单位或具体编码的描述均作为**待检项目**，不得先验填写为真实结果。

## 1. 任务目标与边界

第一阶段已建立 `FileData / Trace`、格式注册、文本及 PEER 解析、SAC/miniSEED 实验性适配、CLI、Qt GUI 和逐 Trace 导出机制。本轮不是重新设计架构，而是用少量真实文件证明“**读对 → 解释对 → 导出对**”，修复证据明确的问题，使项目达到可信的最小可用版本。

本轮目标：

1. 让四份实际数据进入固定的验收样本表，记录文件来源、格式特征和已确认的参考值。
2. 核查格式识别、读取、元信息提取、单通道写出与独立回读；对同一文件的所有通道分别处理，不跨文件合并。
3. 以可重现的结果区分**程序错误**、**输入格式超出当前声明的支持范围**和**源文件本身信息不足**。
4. 修复已确定的低风险缺陷及测试揭示的阻断问题，保持 Core/Formats/CLI/GUI 共享处理逻辑。
5. 更新 README 中的格式支持矩阵、已知限制与验收结论，明确正式支持和实验性支持的边界。

**本轮不开发**：COSMOS/SMC/K-NET 新适配器、跨文件三分量合并、一个文件写多通道、复杂任务管理、滤波与频谱、通用格式自动猜测引擎、动态插件或大量自动化 UI 测试。既有 REST 特殊 miniSEED 变体仅在上述基础验收完成且已有明确样本和规范时单独评估，不作为本轮必须完成的内容。

## 2. 现有样本与测试职责

样本目录（相对 EETB 仓库根目录）：`GMTools/example/`。

| 文件 | 用户给出的大小（字节） | 初始预期（仅按扩展名） | 主要验证点 |
|---|---:|---|---|
| `20240902_Hualian_TAPBA0.mseed` | 8,257,536 | miniSEED 候选；不能预判是标准 v2/v3 还是变体 | 版本、数据编码、Trace 数、源标识、时间连续性/分段、样本总数、数值及输出限制 |
| `IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac` | 240,632 | SAC Binary 候选 | 大小端、v6/v7、NPTS、DELTA、B、绝对时间、IDEP、数值回读 |
| `RSN1_HELENA.A_A-HMC180.AT2` | 78,673 | PEER AT2 候选 | 头段的 NPTS、DT、单位、单通道连续排列、AT2 与 CSV 互转 |
| `Trinidad.dat` | 34,501 | **未知组织方式的文本候选** | 是否为纯文本、前几行内容、数据起始行、Sequential/Columnar、是否有时间列、单位是否可确认 |

说明：`.dat` **不能根据后缀直接认定为单列、CSV 或 PEER**；同理 `.mseed` 不等于已证明兼容标准 miniSEED。任何错误日志都要先与实际文件头段和独立读取工具交叉核对。

### 2.1 样本管理与参考信息

- **保留原始文件，不修改、不覆盖、不做隐式滤波/重采样/标定**。
- 样本可继续放在本地 `GMTools/example/`，**不强制提交**四个文件到 GitHub；尤其是 8 MB 级 miniSEED。若决定纳入仓库，先检查数据来源的共享许可、文件体积和仓库管理策略。
- 在 `GMTools/docs/validation/` 或等效目录记录一个简短的样本清单：原文件名、SHA-256、来源/下载链接（如已知）、下载日期（如已知）、实际识别格式/版本、参考工具、关键字段和问题备注。未知信息写“待确认”，不得补造。
- 输出统一到**独立的临时目录**（例如 `GMTools/build/acceptance/`），不要把实验性输出混入 `example/`。
- 对同一文件确认至少：能否识别，Trace 数、各 Trace 样本数、dt、时间起点、物理量、单位；对 miniSEED 另记录分段与源标识；对 SAC 另记录头段版本与字节序。

## 3. 环境与执行约定

### 3.1 构建

以仓库根目录 `EETB/` 为工作目录，以下命令为建议实例；实际 Qt/libmseed 安装位置按本机修改：

```bash
# CLI + GUI + miniSEED（本机已安装 libmseed 3.5.3 或兼容的 3.x CMake 包时）
cmake -S GMTools -B GMTools/build -DCMAKE_BUILD_TYPE=Release \
  -DGMTOOLS_BUILD_GUI=ON -DGMTOOLS_ENABLE_MSEED=ON
cmake --build GMTools/build --config Release

# 如果暂时没有 libmseed，先验证文本 / PEER / SAC / GUI；
# miniSEED 记录为“环境未满足”，不能记为“解析失败”。
# cmake -S GMTools -B GMTools/build -DCMAKE_BUILD_TYPE=Release \
#   -DGMTOOLS_BUILD_GUI=ON -DGMTOOLS_ENABLE_MSEED=OFF

GMTools/build/gmtools formats
GMTools/build/gmtools --help
```

注意 `formats` 所显示的 miniSEED 能力取决于编译选项。**测试日志必须记录 `GMTOOLS_ENABLE_MSEED` 的值**及 libmseed 版本，避免把未编入适配器当成格式兼容性问题。

### 3.2 原始数据只读与输出隔离

```bash
mkdir -p GMTools/build/acceptance
sha256sum GMTools/example/* > GMTools/build/acceptance/original.sha256
```

所有导出指向 `GMTools/build/acceptance/` 下的子目录；**任何测试均不允许使用 `--overwrite` 指向 `GMTools/example/`**。完成后重新计算源文件哈希进行对比。Git 仓库已有忽略规则可能未覆盖 `example/` 和验证产物，检查 `git status`，按是否需要跟踪决定 `.gitignore`，不要自动把真实样本或大型输出提交入库。

## 4. 验收流程与用例

每个用例形成**一行结果记录**：`PASS / FAIL / UNSUPPORTED / BLOCKED`、输入、执行参数、预期与实际、关联日志/输出、是否需修复。

- `PASS`：在已声明支持范围内，完成结果和参考值对照。
- `FAIL`：已声明支持，但读取、导出、数值、时间或元数据行为不正确。
- `UNSUPPORTED`：实际格式/编码明确不在支持范围，程序清楚拒绝并说明；不是“功能通过”。
- `BLOCKED`：缺依赖、缺合法参考数据、未知单位、测试条件不足等原因，暂无法判断。

### T0：格式检测与基本元信息（四份样本都做）

```bash
GMTools/build/gmtools info GMTools/example/RSN1_HELENA.A_A-HMC180.AT2
GMTools/build/gmtools info GMTools/example/IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac
GMTools/build/gmtools info GMTools/example/20240902_Hualian_TAPBA0.mseed
GMTools/build/gmtools info GMTools/example/Trinidad.dat
```

核查：

- 输出格式与真实内容一致，非标准/不可识别内容不被“猜测成功”。
- Trace 数、每条 N、dt、单位、相对/绝对起始时间来自实际文件或显式参数，不无依据填充。
- `source.*`、`peer.*`、`sac.*`、`mseed.*` 等重要属性来源可解释；无单位时显示 unknown。
- 自动检测失败后，使用 `--format` 手动指定；如果仍失败，保留诊断，不直接修改输入数据。

### T1：PEER AT2（必需通过）

1. 用 `info` 提取实际 `NPTS`、`DT`、单位（不预先假定其数值）。
2. 以文件头段和独立读取方法核对原始数值顺序、样本数和采样间隔。
3. AT2 → CSV，检查输出为**一通道独立文件**；随后 CSV → AT2 时，**明确补回 CSV 无法保存的 dt 和单位**。例如：

```bash
GMTools/build/gmtools convert GMTools/example/RSN1_HELENA.A_A-HMC180.AT2 \
  --to csv -o GMTools/build/acceptance/at2_to_csv/

# 第二步通过实测得到的 DT 填入 <实际DT>，确认原输入单位为 g 后才可用：
# gmtools convert <上一步输出.csv> --dt <实际DT> --input-unit g \
#   --to at2 -o GMTools/build/acceptance/csv_to_at2/
```

验收：样本数一致；CSV 数值与 AT2 的 g 数值（或明确转换后的单位）匹配；再次 AT2 回读不改变 dt；对缺失/不符 NPTS 的故意改造副本有准确警告且不静默补样本。

### T2：SAC（实验性能力单独验收）

```bash
GMTools/build/gmtools convert \
  GMTools/example/IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac \
  --to sac -o GMTools/build/acceptance/sac_roundtrip/

GMTools/build/gmtools convert \
  GMTools/example/IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac \
  --to csv -o GMTools/build/acceptance/sac_to_csv/
```

核查：SAC 头段版本、大小端、`DELTA`、`NPTS`、`B`、起始时间、`IDEP` 与原始样本。输出 SAC 再用 GMTools 和**独立 SAC/ObsPy 读取器**验证。考虑 float32 编码精度，使用相应的**数值容差**，不要求字节完全相等，也不要求保留所有原格式专有元数据。

**定向修复项**：当前 `src/formats/sac.cpp::write_sac()` 将 `IDEP` 写为未定义值，即使 `trace.quantity` 是已知加速度/速度/位移。应优先检查并修正可表达的 `IDEP` 映射；**不得**由 `IDEP` 推断 g、cm/s² 等工程单位。通过带已知 `IDEP` 的实际/最小样本回读验证。

### T3：标准 miniSEED（实验性能力单独验收）

```bash
GMTools/build/gmtools info GMTools/example/20240902_Hualian_TAPBA0.mseed

# 只有 info 成功、确认源标识与绝对时间满足 Writer 要求后才执行写出：
GMTools/build/gmtools convert GMTools/example/20240902_Hualian_TAPBA0.mseed \
  --to mseed -o GMTools/build/acceptance/mseed_roundtrip/
```

重点核查：

- 真正的 miniSEED 版本（2/3）、原始编码类型、记录数、独立 Trace 数，以及相同源标识的连续记录是否正确拼接。
- 不连续/重叠/乱序、采样参数改变时，是否拆为独立 Trace 并提供诊断；**绝对不自动补零、裁剪或覆盖**。
- 时间戳精度、样本数、起始时间及采样间隔与独立工具的结果一致。
- 标准 miniSEED 通常不足以直接确定工程物理单位：未知单位时应保持 Unknown，不能直接把输出称为 g 加速度。
- 逐通道写出后独立回读；如果原文件不符合当前 Writer 的源标识/时间约束，明确记录 `UNSUPPORTED` 或 `BLOCKED`，**不要强行改写标识伪造转换成功**。
- 如果证实文件使用国内或仪器专有变体，单独记录格式差异及具体报错，不把它归结成“标准 miniSEED 解析失败”。

数据量较大，额外观察 GUI 的导入耗时、绘制响应与内存是否可接受；第一版仍允许一次性加载，不为单份 8 MB 文件提前引入流式架构。

### T4：Trinidad.dat（真实通用文本适配，必需完成“可解释性”判断）

首先查看**实际文件头段**，确定其是否为纯文本、行列结构、是否存在时间列及记录参数，而不是直接套用某个预设：

```bash
file GMTools/example/Trinidad.dat
sed -n '1,35p' GMTools/example/Trinidad.dat
GMTools/build/gmtools info GMTools/example/Trinidad.dat
```

根据真实内容选择以下之一：

- 单列样本或多列连续样本：`--format text --first-line N --layout sequential`。
- 每列独立通道：`--format dat --first-line N --layout columnar [--data-columns ...]`。
- 含时间列：在 Columnar 基础上加 `--time-column N`，应能计算并校验等间隔 `dt`。
- 无 dt/单位：允许读样本与预览，按目标格式要求提示补充；**不得**自行推断 dt 或加速度单位。

验收：数据起始行明确，N 与数值顺序可人工/独立脚本核验；如文件需要目前未支持的编码、固定宽度或 Fortran `D` 数值，记录差异并评估最小修复，**不要为了单文件推导出复杂的通用自动识别系统**。

### T5：最小可控 TXT/CSV 边界样本（少量、必需）

四份真实数据不一定覆盖通用解析器的全部关键语义，因此增加 **3～4 份很小的临时测试文本**，放在 `GMTools/build/acceptance/fixtures/`，无需引入大测试框架：

1. **多列连续样本**：如两行三列，Sequential 应得 1 Trace / 6 samples；未指定布局不得被默认为三通道。
2. **时间 + 两个数据列**：Columnar + 时间列应得 2 Traces，dt 由时间列得出，输出两个单通道文件。
3. **非等间隔时间列**：明确报错，不静默取平均 dt。
4. **缺少采样间隔/单位**：允许 `info` / 简单文本导出；转换成需要已知加速度单位与 dt 的 AT2 必须拒绝或要求用户补充。

另用一个**已声明 NPTS 与实际值不符的 PEER 副本**验证警告；不必为每种错误建立重复测试。

### T6：文件输出行为与数值一致性（必需）

- 多通道输入默认各 Trace 一个文件；`--channel N` 只生成所选通道文件，编号从 1 开始。
- 重名/空名通道输出名称不冲突；已存在文件默认拒绝覆盖；使用 `--overwrite` 时不允许覆盖输入源文件。
- 目标 Writer 需要的信息不足时，错误说明具体缺失项，不生成宣称单位正确的错误文件。
- **物理单位转换**：选一个已确认的加速度 Trace，校验 `1 g = 9.80665 m/s² = 980.665 cm/s²`，回读比较输入和输出值；区分 `--input-unit` 的“解释/重标记”和 `--output-unit` 的“实际数值转换”。
- 记录写出前和独立回读后的 `N`、dt、开始时间（若目标可表示）、样本值范围/误差。纯文本 Writer 不保存 dt/单位的警告属于已知限制，不按“不保留元数据”笼统判错。

### T7：GUI 最小人工核验（必需）

使用其中至少一个标准文本、一个通用文本和一个多通道/多记录文件，逐项人工确认：

1. 打开文件后可以看到明确的格式检测结果和错误/警告；文本原文有行号，能够定位第一条数据所在行。
2. 修改布局、起始行、时间列和采样参数后，**Preview parse** 反映修改，并能确认导入。
3. 主界面对输入文件的**所有 Trace 分别绘图**，轴含义正确：有 dt 时为经过时间；无 dt 时是样本序号；各通道独立纵轴。
4. 选中单一 Trace 查看/修改基本属性，导出勾选通道；修改后预览和写出保持一致。
5. 在重解析可能丢失通道编辑时有明确提示，输出冲突和失败不静默发生。

不要求本轮自动截图对比、UI 视觉回归或全面重绘界面；如发现明显遮挡、布局难用、绘图异常，再按具体现象修复。

## 5. 开发修复优先级与限制

| 级别 | 触发条件/已知问题 | 修复原则与交付要求 |
|---|---|---|
| P0 | 样本值、顺序、N、dt、绝对起点明显错误；错误单位被当成已知加速度；导出覆盖原文件 | 阻断验收；定位到 Reader、单位逻辑或 Writer，修复后重复相应用例 |
| P0 | 文件实际在声明支持范围内但 Reader 崩溃、错误成功或静默丢分段 | 修复同一根因，给出清晰诊断，不用“容错”吞掉问题 |
| P1 | `write_sac()` 不写已知 `IDEP`；实际可表达的元信息不必要丢失 | 小范围定向修复，真实/独立回读验证，保持工程单位仍为 Unknown 的正确语义 |
| P1 | `Trinidad.dat` 等文本由于小范围解析规则差异无法使用 | 先确认文件实际结构，仅添加必要的通用规则或文本头段解释；避免按文件名硬编码 |
| P1 | miniSEED/SAC 已支持子类型的时间精度或导出存在误判 | 最小改动并明确可支持边界；不扩大“支持全部版本”的表述 |
| P2 | 普通 GUI 布局、原始文本定位、诊断提示不清楚 | 仅修复实际复现的问题，不在验收阶段全面重做 UI |
| 延后 | 自定义 Profile 管理、复杂编码、多目录批量、SMC/K-NET、新型信号处理 | 记录后续阶段，不纳入本轮验收 |

### 5.1 一个已知代码点

`GMTools/src/formats/sac.cpp::write_sac()` 当前对 `IDEP` 使用 `unknown_dependent`，即使 `Trace::quantity` 具有明确值。应按 SAC 标准映射已知 `Acceleration / Velocity / Displacement`；无匹配时仍写未定义。不要把该修复扩大成 SAC 全头段复制或单位猜测。

### 5.2 对测试发现问题的处理顺序

**复现问题 → 提取期望值依据 → 区分输入与解释问题 → 进行最小修复 → 仅重跑相关用例 + 一项必要回归**。不为未出现的问题引入复杂兼容层。对超出第一版支持范围的数据，先给清晰的不支持诊断；只有具有普遍性并能通过现有解析模型表达时，才评估新增处理。

## 6. 独立验证方法与误差原则

- **标准格式**：优先用 ObsPy、SAC 工具、libmseed 自带工具或数据来源提供的参考参数，对 SAC/miniSEED 进行独立解析；任选一种与程序实现足够独立的参考路径，避免单纯“GMTools 导出 → GMTools 回读”形成循环证明。
- **PEER / 文本**：直接读取头段并用独立的最小数值脚本验证样本总数、前/后若干值、峰值绝对值、dt；标准格式可额外用 SeismoSignal / ViewWave 验证兼容性。
- **误差原则**：文本 double→文本允许浮点序列化误差；SAC float32 按该精度设容差；miniSEED 编码/时间戳精度依格式实际能力确定。先在验收记录中说明容差的理由，不机械使用一个对所有格式通用的阈值。
- **单位与时间不能单看数值**：两个时程样本看似一致，但若单位或时间起点不一致，仍判定不满足相应目标格式的正确性。
- 如独立工具对某个文件也无法识别，将其作为格式特征调查项，而不是立即标记 GMTools FAIL。

## 7. 推荐执行次序及提交安排

### A. 样本调查与基线记录

1. 检查 `git status`，记录当前提交与构建选项；生成输入哈希。
2. 执行 `formats`、四份文件的 `info`；读取文本头段并取得独立参考值。
3. 给四份样本分别填写验证表，标明普通支持、实验性、未知或环境阻断。

**交付**：`docs/validation/` 下的样本清单与首轮检查结果；不要修改文件内容。

### B. 最小功能闭环验收

1. 优先完成 PEER → CSV、通用 DAT → TXT/CSV 和 3～4 份小文本测试。
2. 确认时间/单位/输出命名、覆盖保护、错误诊断。
3. 基础闭环通过后，再分别核查 SAC 和 miniSEED；其中 miniSEED 依赖未安装时独立标注 BLOCKED。

**交付**：T0、T1、T4、T5、T6 的结果；T2、T3 的单独实验性报告。

### C. 定向修复与回归

1. 先处理 P0；修复 SAC `IDEP`；再处理由真实样本直接揭示的 P1。
2. 仅补充能防止本次错误再次发生的少量代码级检查或简短脚本。
3. 每项修复记录原始现象、触发条件、代码位置、解决方式、核验结果。

**交付**：针对性修复提交，可采用 `fix: ...`、`test: ...`、`docs: ...` 等简短提交信息。

### D. GUI 人工验收与文档收尾

1. 人工确认典型文本、二进制、多通道导入和逐通道导出；只改明显问题。
2. 更新 `GMTools/README.md` 的已支持格式、实验性边界、典型命令及限制。
3. 形成阶段结论：基础闭环是否通过；SAC/miniSEED 各自达到何种支持级别；下阶段可进入的格式扩展范围。

**交付**：完整验收摘要和更新后的 README。

## 8. 结果记录模板（建议与样本清单一起保存）

```markdown
### 样本 / 用例：<输入文件> — <T编号>
- 输入 SHA-256：<真实值>
- GMTools 版本/提交：<真实值>
- 构建：<系统/编译器/Qt版本/miniSEED开关与libmseed版本>
- 输入识别结果：<格式、版本、编码、字节序（适用时）>
- 参考依据：<头段/独立工具/人工数值核对>
- 参考：<通道数、各N、dt、开始时间、单位与数值>
- 执行命令或操作：<实际命令>
- 实际结果：<记录>
- 结论：PASS / FAIL / UNSUPPORTED / BLOCKED
- 原因与后续行动：<无则写无>
```

可以把 T0–T7 汇总为一张很短的完成表，测试记录应记录**实际运行结果**，不能直接用本开发文档的“预期”替代。

## 9. 完成标准与下一阶段入口

**第一阶段最小可用版本通过**需满足：

- T0 对基础样本可产生可信的识别与诊断；T1、T4、T5、T6 对第一版声明支持的文本/PEER 核心功能通过。
- CLI/GUI 共享核心转换行为，GUI 完成一次最小人工操作核查（T7），不存在已知 P0 问题。
- 数据正确性及单位/时间的必需约束有证据支撑；对缺失元数据不猜测，对目标格式无法保留的信息给出提示。
- README、样本表与验收记录同步更新，示例数据来源与许可边界清楚。

**SAC 与 miniSEED 的结论单独记录**：能够经独立工具验证其已声明子类型的读取/写出，才考虑由实验性调整为正式支持；否则继续标记实验性或已知限制，不阻塞文本与 PEER 基础版本验收。

本轮结束后，优先推进的工作才是新格式的样本驱动适配（例如 K-NET/SMC、PEER 其他类型或 REST 专有 miniSEED 变体），而不是再次修改 `Trace` 模型、Reader/Writer 总接口或重建 CLI/GUI 架构。
