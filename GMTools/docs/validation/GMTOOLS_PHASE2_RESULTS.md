# GMTools 第二阶段样本与开发记录

> 构建入口已迁移至 xmake。本文保留当时的 CMake 选项、命令和产物路径作为历史记录；当前操作请使用 [README 的构建说明](../../README.md#构建)。

本轮以 `main` 的 `ce14599` 为基线，调查日期为 2026-11-24（北京时间）。旧样本与第一阶段的独立参考值见[第一阶段验收记录](GMTOOLS_PHASE1_RESULTS.md)。

## NIED 样本

| 用途 | 来源、日期和状态 | 样本与参考值 |
|---|---|---|
| K-NET 正式验收 | 2026-11-24 访问 NIED 公布的 [MYG0041103111446.NS](https://www.kyoshin.bosai.go.jp/kyoshin/download/knet/data/2011/03/20110311144600/MYG0041103111446.NS)，HTTP 401。 | 原件未取得。SHA-256、通道参数和参考值待核。 |
| KiK-net 方向回归 | 同日访问官方 [FKSH101103111446.NS2](https://www.kyoshin.bosai.go.jp/kyoshin/download/kik/data/2011/03/20110311144600/FKSH101103111446.NS2)，HTTP 401。 | 原件未取得。SHA-256、地表方向和参考值待核。 |
| 开发参考 | 已安装的 ObsPy 1.5.2 提供 `obspy/io/nied/tests/data/test.knet` 夹具，随 [ObsPy 1.5.2 发布包](https://pypi.org/project/obspy/1.5.2/#files)提供。头段有测试用备忘，原始下载记录与样本再分发条件待确认。 | SHA-256 `a83a938a0bdabb7173790c8bd1c2aa74a2773d81926469c6017538f52a42e36b`。第 18 行是首个数据行。AKT013、E-W、100 Hz、5,900 个值。Scale Factor 为 `2000(gal)/8388608`。 |

独立读取上述夹具得到 Record Time 为 `1996/08/11 03:12:39` JST，首样本为 `1996-08-10T18:12:24Z`。按头段比例对有符号整数换算，首、末值分别为 `-4.340410232543945`、`-3.643035888671875` cm/s²，范围为 `[-8.418560028076172, 0.08988380432128906]` cm/s²。这些数值由 Python 逐 token 读取原始计数、计算有理数比例，并以 ObsPy 核对 N、dt 和 UTC。ObsPy 将该夹具的 SI 标定因子单列，比较样本时应先确认参考工具返回的是 counts、gal 还是 m/s²。夹具只用于解释格式和准备定向回归。 [ObsPy NIED Reader](https://github.com/obspy/obspy/blob/master/obspy/io/nied/knet.py)

NIED 的波形下载要求注册。后续取得正式文件时记录下载日期、原始 SHA-256，并按 NIED 要求引用数据 DOI `10.17598/NIED.0004`。再分发条件以注册和数据提供方条款为准。官方格式说明明确规定首样本为 JST 触发时刻减 15 s，且头段峰值基于扣除均值的序列。GMTools 对 counts 只应用头段标定，保存所报峰值供参考。 [NIED 数据使用要求](https://www.kyoshin.bosai.go.jp/en/) · [下载路径](https://www.kyoshin.bosai.go.jp/en/https_download/) · [ASCII 格式](https://www.kyoshin.bosai.go.jp/en/knetascii/)

## 格式调查

| 格式 | 数据结构与第一批适配边界 | 首次验收所需文件 | 状态 |
|---|---|---|---|
| PEER VT2 / DT2 | 先从明确的速度或位移头段读量与单位，再复用 AT2 的 NPTS、DT 和逐行展开逻辑。速度、位移单位需要独立的量纲校验。尽早拒绝将速度、位移头段当成 AT2。 | 一对来源可复查且明确写出单位的原始 VT2 和 DT2。核对量、单位、NPTS、DT 与独立读值。 [PEER NGA-West2](https://peer.berkeley.edu/peer-nga-west2-online-ground-motion-database-updated-and-expanded) | NOT IMPLEMENTED |
| COSMOS Text v1.20 | 文字、整型、实数头段和可变说明之后带数据段前导行。段首声明点数、量、单位及 Fortran 格式。V0 可以是原始计数，V1 是未校正工程时程，V2 是处理后的物理时程，V3 是谱。建议先限定为独立的一段等间隔工程加速度，随后考虑定宽、串接的分量或其他物理量。 | 一份带完整单位和步长声明的单段 V1 或 V2，以及一份带定宽或多段的文件用于界定拒绝范围。 [COSMOS v1.20](https://www.strongmotioncenter.org/vdc/cosmos_format_1_20.pdf) | NOT IMPLEMENTED |
| USGS SMC | 传统 SMC 有文本、整数、实数和可变注释头段。其等间隔序列与非等间隔时间—幅值对需要各自解释。等间隔工程值可适配现有 Trace，固定宽度字段和可能的非等间隔序列需要先处理或明确拒绝。CESMD 同时列出 CSMIP 和 COSMOS 数据，后缀不足以区分。 | 一份带已知单位的等间隔 SMC 波形，再选一份非等间隔样本验证边界。 [USGS SMC](https://escweb.wr.usgs.gov/nsmp-data/smcfmt.html) · [CESMD 格式](https://www.strongmotioncenter.org/NCESMD/reports/DataFormat.pdf) | NOT IMPLEMENTED |

## 实现与验收状态

执行了 Linux x86_64 Release 编译。Qt 构建启用 `GMTOOLS_BUILD_GUI=ON`、`GMTOOLS_ENABLE_MSEED=ON`，使用 GCC 16.2.1、Qt Widgets 6.12.0、nlohmann/json 3.12.0 和 libmseed 3.5.3。另编译了两个开关均关闭的纯 CLI。以下状态区分编译、独立样本调查和 GMTools 运行验收。

| 用例 | 状态 | 依据或条件 |
|---|---|---|
| P2-T1 Trinidad + Profile | 待运行 | Profile 见 [`trinidad.profile.json`](../../example/trinidad.profile.json)。对照第一阶段的 1 Trace、2,141 个样本和 0.01 s。另用显式 `--input-unit cm/s2` 重标记并验证样本数值保持。 |
| P2-T2 文本建议 | 待运行 | 用 Trinidad 和少量带时间标签、无标签、时间不均匀的表格对照候选。建议只在用户确认后参与解析。 |
| P2-T3 Profile 非法输入 | 待运行 | 检查未知版本、非法字段及重复 JSON 键的错误退出，并确认输出目录没有结果文件。 |
| P2-T4、T5 K-NET 波形与时刻 | BLOCKED | 正式样本入口要求登录。目前仅有官方规范与 ObsPy 夹具独立参考。 |
| P2-T6 K-NET 导出 | BLOCKED | 正式样本到位后对照 `counts × Scale Factor` 和 `gal / 980.665`，并核对单条导出。 |
| P2-T7 K-NET 非法头段 | 待运行 | 在临时副本修改采样频率或比例，对照准确的错误位置并保护原件。 |
| P2-T8 旧格式回归 | 待运行 | 使用第一阶段四份参考文件及关键导出对照，miniSEED 需启用 libmseed。 |
| P2-T9 GUI 人工核验 | 待运行 | 检查预览建议、应用、载入和保存配置、标准格式信息及逐通道导出。 |
| P2-T10 条件格式 | NOT IMPLEMENTED | PEER VT2/DT2、COSMOS 和 SMC 完成本轮格式范围调查。 |

正式样本到位后，可将原件存放在本地路径，输出另置于构建目录。以下命令以启用 miniSEED 的阶段构建路径为例。

```sh
./build/phase2/gmtools info example/Trinidad.dat --profile example/trinidad.profile.json
./build/phase2/gmtools info example/Trinidad.dat --profile example/trinidad.profile.json --input-unit cm/s2
./build/phase2/gmtools info /path/to/station.NS --format knet-ascii
./build/phase2/gmtools convert /path/to/station.NS --to csv -o build/phase2/acceptance/csv/
./build/phase2/gmtools convert /path/to/station.NS --to at2 -o build/phase2/acceptance/at2/
```

阶段开发范围和通过条件见[第二阶段开发文档](../GMTOOLS_PHASE2_DEVELOPMENT.md)。
