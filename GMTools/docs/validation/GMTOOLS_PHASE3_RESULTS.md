# GMTools 第三阶段验收记录

2026-11-25，Linux x86_64。下列 T1–T10 结果对应基线 `cce2069245bbeb6d31de53baf163f870ab8d8242` 上的工作树，第三阶段实现随后收录于 `f2acf0d3be7011d77ea857bd4e8708215ba17685`。第三阶段开发文档与当时用户提供的附件逐字节一致。[开发范围及完成定义](../GMTOOLS_PHASE3_DEVELOPMENT.md)

## 构建与入口

M1–M8 主体代码为 **IMPLEMENTED**。完整配置的 Core、CLI 和 GUI，以及关闭 Qt GUI 和标准 miniSEED 的 CLI 配置均为 **COMPILED**。使用 xmake 3.1.2、GCC 16.2.1、Qt Widgets/Concurrent 6.12.0、libmseed 3.5.3。标准 miniSEED 的版本、解码和写出依赖 libmseed；Modified Reader 在精简配置中也能独立解码武汉记录。

本次沿用两套已有的 Release 配置。完整配置通过 `mseed.pc` 定位本地 SDK。下列为在 GMTools 目录内实际使用的构建和检查入口。运行记录、独立核对脚本、派生波形和 GUI 截图均在忽略的 `build/phase3/`。

```sh
env XMAKE_GLOBALDIR=/tmp/gmtools-xmake-global XMAKE_CONFIGDIR="$PWD/build/xmake-mseed-config" \
  XMAKE_SKIP_HISTORY=y PKG_CONFIG_PATH=/tmp/gmtools-libmseed-sdk/lib/pkgconfig xmake build -j 4
env XMAKE_GLOBALDIR=/tmp/gmtools-xmake-global XMAKE_CONFIGDIR="$PWD/build/xmake-cli-config" \
  XMAKE_SKIP_HISTORY=y xmake build -j 3 gmtools
env PYTHONPATH=/tmp/gmtools-acceptance-obspy OPENBLAS_NUM_THREADS=1 python build/phase3/check.py
env PYTHONPATH=/tmp/gmtools-acceptance-obspy OPENBLAS_NUM_THREADS=1 python build/phase3/edges.py
git diff --check
```

## 分项结果

| 用例 | 状态 | 本轮实际核查 |
|---|---|---|
| T1 单位与标定 | PASS | 武汉计数经文件标定与两倍调幅再换算到 cm/s²，逐点和独立公式比较。另核对手动 mm/count 标定与 cm 输出。仅调幅后将已知 counts 指定为加速度会报错。零灵敏度拒绝文件标定。 |
| T2 标准 miniSEED 读取 | PASS | 花莲文件读出 28 条、每条 72000 点。libmseed 发布包中的 v2 INT16、INT32、FLOAT32、FLOAT64、Steim-1、Steim-2 六份参考与 ObsPy 逐点一致。独立构造的 v2 Blockette 100/1000/1001 记录验证了实际采样率、时间修正和负微秒余量；连续包合并，间断、重叠和乱序分别保留。 |
| T3 Modified miniSEED | PASS（wc 实录） | 武汉实录识别出三路加速度和两路辅助记录。北向 240000 个原始整数与 ObsPy 独立解压完全一致。BTime 无效的连续两个包保留为两条未知时间记录。派生包分别检验时间修正位已置与未置。 |
| T4 普通及自描述文本 | PASS | Trinidad v1 Profile 的 2141 点逐样本回读；对默认单列、带元信息的时间/幅值 CSV 与 TSV、Fortran D 指数、列出经过时间和独立相对起点的处理进行运行核对。缺 dt 的时间列、冲突的 CSV 分隔符、头段 dt 与时间列不符均报错。 |
| T5 SAC | PASS | 台站 v6 原件回写 v6/v7 的全部 60000 点与源 float32 完全相同；用 ObsPy 读 v6 检查 NSLC 和 UTC，用二进制字偏移检查 v7 的版本、长度、时间和坐标 footer。ObsPy 生成的大端 v6 也逐点一致。加速度 g 的 keep 模式得到 IUNKN，standard 模式得到 IACC 和换算后的 float32 纳米制波形。 |
| T6 PEER 家族 | PASS | Helena AT2、Tabas VT2、Alhambra DT2 的数值段分别独立解析；同族回写逐点一致，VT2 的 cm/s→mm/s 与 DT2 的 cm→mm 符合比例。另用三点夹具覆盖 D/d 指数和跨量纲拒绝。 |
| T7 miniSEED 写出 | PASS | v2/v3 × FLOAT64/FLOAT32/INT32/Steim-2 共 8 个输出。ObsPy 解码 v2；v3 按 FDSN 字节布局逐记录核验编码、SID、时间、率、总点数及载荷，Steim-2 载荷用 ObsPy 解压。分数输入、int32 越界、超出 30 位的差分、缺 UTC/SID 以及花莲两字符代码写 v2 均拒绝。 |
| T8 跨格式 | PASS（列出的路径） | miniSEED→SAC 保留花莲原通道代码；SAC→miniSEED 保留绝对时间；TXT 补齐 UTC/SID 后写 miniSEED；Modified→工程 CSV/AT2 的加速度逐点与独立结果一致。 |
| T9 原有功能 | PASS（样本及保护路径） | Trinidad Profile、ObsPy 的 K-NET 夹具、拒绝已有目标和多通道中途校验失败时零发布。精简 CLI 读取 Modified 并对标准 miniSEED 给出缺依赖诊断。 |
| T10 Qt 界面 | PASS（Linux 离屏工作流） | 临时驱动链接实际 GUI 对象，通过拖入武汉文件、切换标定、预览失效后重新预览并导入、勾选一路、编辑 UTC/代码、选择 cm/s² 和元信息 CSV、后台导出。Qt 截图核查了导入、编辑和主界面；独立读取 GUI 输出，240000 个时间和幅值与参考一致。 |

## 可复核的样本与数值

- **武汉**使用本机已有的 `qrest/qrest_data/resource/wuhan_mseed/WH.XY001-2024-01-01-15-15-00.mseed`，SHA-256 为 `7f1b574adbdb3ffa8db174547a9e4d40245270cf88e3e5a31d7f4f37dbb3bfe3`。独立核对时只将每个包开头六字节替换为标准 v2 序号，保留原始时间、样本编码、载荷及次序，再交给 ObsPy 1.5.2。EIN 首时刻为 `2024-01-01T07:14:59Z`，dt 为 0.005 s，计数前五点为 −3、−3、−1、1、4，范围 −35 至 40。低 30 位为 5000000，对应 0.00002 (m/s²)/count。另两路加速度同为 240000 点；VPB、VEP 分别为 1200、1146 点，按无量纲辅助通道保留原始计数。文件标定验证了协议换算，仪器计量溯源需设备资料。
- **Modified 代码来源**为 REST data 提交 `f0c90d41b8765a6af1c3846cc68d8e694ce80c25`。移植 Steim-2 解码与扩展字段规则，按本项目独立解释 BTime、修正位与分段。专有布局要求单个 Blockette 1000，标准 v2 的扩展链交给 libmseed。[REST 参考实现](https://github.com/FeiyueDong/rest/tree/f0c90d41b8765a6af1c3846cc68d8e694ce80c25/src/rest_data/src/rest_data_tools/formats/mseed) · [随附许可](../../licenses/REST-data-MIT.txt)
- **速度实录**为 Tabas Iran、1978-09-16、Tabas L 分量，文件 `RSN143_TABAS_TAB-L1.VT2`，SHA-256 为 `db1edd7af54b36cb5b3dec53a50627434a471ed31b199d4edc7a596bc5be7586`。原头声明 1650 点、dt=0.02 s、CM/S；范围 −98.79714 至 83.26164 cm/s。只将样本下载到忽略的核对目录。[固定版本的公开处理示例样本](https://github.com/Junjun1guo/PEERMotionFormatProcess-with-Python/blob/362b508c07ef29fa3048dbc095974b70fdab9156/downLoadPeerMotion/RSN143_TABAS_TAB-L1.VT2)
- **位移实录**取自本机 OpenSees 工作坊资料 `Workshops/OpenSeesDays/frame2/ALH360.DT2`，提交 `08ec8ca212eb0687eeb9baf9555def2de1719eaf`，SHA-256 为 `91f8bd48b9cac35988792dde9e9d72b68bc2264443f0dcb934365ee734e6c190`。原头声明 Northridge、Alhambra – Fremont School、360 分量、3000 点、dt=0.02 s，单位 CM，单位字段后还带滤波说明。
- **回归样本**沿用项目内 Helena、Trinidad、花莲和台湾台站 SAC，摘要与第一阶段记录一致。NIED 使用 ObsPy 自带的 `obspy/io/nied/tests/data/test.knet`，按 ObsPy 的 SI 标定结果核验；其 SHA-256 为 `a83a938a0bdabb7173790c8bd1c2aa74a2773d81926469c6017538f52a42e36b`。[项目内样本来源及哈希](GMTOOLS_PHASE1_RESULTS.md) · [标准 miniSEED 编码](https://docs.fdsn.org/projects/miniseed3/en/latest/data-encodings.html) · [SAC 格式](https://ds.iris.edu/files/sac-manual/manual/file_format.html)

## 复现示例

以下命令使用完整 Release 构建。将 `$WH_RECORD` 指向上述武汉文件，`$VT_RECORD` 指向上述 Tabas 文件。

```sh
GM=build/xmake-mseed/linux/x86_64/release/gmtools
"$GM" info "$WH_RECORD" --calibration source
"$GM" convert "$WH_RECORD" --channel 1 --calibration source --output-unit cm/s2 \
  --to csv --text-layout time-values --text-header metadata -o build/phase3/repro.csv
"$GM" info build/phase3/repro.csv
"$GM" convert "$VT_RECORD" --output-unit mm/s --to txt -o build/phase3/repro-velocity.txt
"$GM" info example/Trinidad.dat --profile example/trinidad.profile.json
```

## 尚待验收的范围

- **NOT RUN**：Modified 的 wt、ws 实录。本轮真实覆盖来自 wc。小端 Steim-2 通过将 EarthScope 小端测试载荷装入派生 Modified 包头核对，不能替代相应设备的原始记录。
- **NOT RUN**：Windows/MSVC 构建、真实桌面的 DPI 与主题组合、所有目标格式在 GUI 上的逐项人工操作。SAC 和 miniSEED 的各编码选项主要经共用 Core 与 CLI 核验。
- **BLOCKED**：NIED 正式台网样本仍缺可用授权文件。现有 K-NET 夹具支持基本回归；正式样本用于进一步确认实际产品差异。

## 收尾修复

2026-11-25，以 `main` 的 `f2acf0d` 为基线。以下记录对应收尾后的工作树。[收尾开发文档](../GMTOOLS_PHASE3_CLOSEOUT.md)

| 范围 | 本轮状态 | 交付与证据 |
|---|---|---|
| F1 自描述文本 | IMPLEMENTED | v1 头段可选的 `provenance.steps` 记录实际标定、调幅、导出换算。写出换算时只修改临时 Trace。回读将步骤映射到 `history.*`，由主字段解释现存样本。CLI 的 `info` 和 GUI 的 Metadata 展示历史，Summary 区分本次标定、既往标定及调幅后的 counts。 |
| F2 Modified 时间 | IMPLEMENTED | 以整数微秒求实际起点时间差，按采样率和片段累计点数求期望时间差。在原合并容差内按最近整数纳秒累计最大绝对偏差，半纳秒向上取整。每段至多一次容差提示，给出通道与首个受影响包。 |
| F3 复现入口 | IMPLEMENTED | 从原 `build/phase3/check.py`、`edges.py` 的参考核对路径提取关键比较。维护脚本使用本地输入和新输出目录，ObsPy 仅为可选验收依赖。 |
| F4 整理 | IMPLEMENTED | 核对重复开发文档的 SHA-256 为 `1b65f592733f1ae730a8dd035b776b29e70eaa9d7f9286adc512b56c3c664a2e`，移除附件副本并忽略项目根目录的 `.codex-attachments/`。四份示例的来源线索、缺少的授权材料和依赖许可已列入 README。版本保持 0.1.0。 |
| Linux Release | COMPILED | Xmake 3.1.2、GCC 16.2.1。完整配置构建 Core、CLI 和 GUI，使用 Qt 6.12.0 和 libmseed 3.5.3。关闭 GUI 与标准 miniSEED 后再次构建 Core 和 CLI，仍编译 Modified Reader。 |
| C1–C5 运行验收 | NOT RUN | 本轮仅做静态检查和编译，未执行新脚本或转换样本。历史 T1–T10 的运行证据见上表。 |
| C6 界面验收 | NOT RUN | 本轮未启动 GUI。Summary、Metadata 和导出的人工复核待执行。 |

`git diff --check` 与核验脚本的 Python AST 语法检查通过。四份仓库原件的 SHA-256 与第一阶段样本清单一致。

两套构建使用独立的 Xmake 配置和产物目录。以下保留实际使用的构建选项，`QT_SDK` 指向本机 Qt SDK，完整配置的 `PKG_CONFIG_PATH` 包含 libmseed 的 `mseed.pc`。本机借助 `XMAKE_GLOBALDIR` 复用已有包缓存。

```sh
export XMAKE_SKIP_HISTORY=y
export XMAKE_CONFIGDIR="$PWD/build/phase3-closeout-build/config"
xmake f -m release -o build/phase3-closeout-build/full --policies=package.fetch_only \
  --build_gui=y --enable_mseed=y --qt="$QT_SDK"
xmake build -j 4

export XMAKE_CONFIGDIR="$PWD/build/phase3-closeout-build/config-lite"
xmake f -m release -o build/phase3-closeout-build/lite --policies=package.fetch_only \
  --build_gui=n --enable_mseed=n
xmake build -j 4 gmtools

git diff --check
python3 -c "import ast, pathlib; ast.parse(pathlib.Path('scripts/verify_core_formats.py').read_text(encoding='utf-8'))"
```

复核时，脚本将文本和 SAC 样本按其输入精度逐点比较，用手工 counts 夹具检查旧 v1、损坏来源字段和再次导出。提供指定武汉文件后，时间用例以 100 µs 的 BTime 刻度构造完全连续、容差内、超容差和未知时间的包，用有理数独立计算期望残差。提供 ObsPy 后，另以独立解码样本检查武汉标定公式和花莲各通道。脚本只覆盖上述路径。Windows/MSVC、桌面 DPI/主题、GUI 全选项、wt/ws 实录及 NIED 正式样本的状态沿用上一节。[核验脚本与参数示例](../../README.md#本地复核)
