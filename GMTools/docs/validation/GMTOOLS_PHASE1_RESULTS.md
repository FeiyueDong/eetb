# GMTools 第一阶段验收记录

验收日期为 2026-11-24（北京时间）。代码基线为 `main` 的 `d73045c`，结果对应本轮修复后的工作树。Linux x86_64 使用 GCC 16.2.1、CMake 4.5.1、Qt Widgets 6.12.0 和 libmseed 3.5.3，构建选项为 `GMTOOLS_BUILD_GUI=ON`、`GMTOOLS_ENABLE_MSEED=ON`。独立参考路径使用 Python 的文本和二进制解析、ObsPy 1.5.2、EarthScope `mseedview`。

另验证了关闭 GUI 和 miniSEED 的纯 CLI 构建。该构建仍识别 miniSEED 签名，`info` 明确提示当前构建缺少适配器。

四份原件的 SHA-256 在执行前后均一致。日志、测试副本和导出都在 Git 忽略的 `build/acceptance/` 下。原始文件来自用户提供的 `example/`。表中“来源”只记录文件本身可见的线索，下载链接、下载日期及再分发许可待确认。

## 样本清单

| 样本 | SHA-256 | 来源线索与独立参考 | 读入结果 |
|---|---|---|---|
| `RSN1_HELENA.A_A-HMC180.AT2` | `b50222e332b15895bfe46e1af271bdebef81d48772884be9de4773b9980cf4a3` | 头段标明 PEER NGA、Helena Montana-01、Carroll College 180，逐 token 解析 ASCII。 | 1 通道，数据从第 5 行开始，NPTS=5093，dt=0.01 s，加速度 g。绝对和相对起点均未知。 |
| `Trinidad.dat` | `9b032d88ccbf996bcd258737c6b19db68deb67eb0f1aff2b341d7f83866513c2` | 头段标明 PEER Strong Motion Database，台站原文为 `090 CDMG STATION 1498`。第 5 行为 `Time[s] Accel[g]`，独立解析第 6 行起的两列。 | 1 通道，2141 样本，dt≈0.01 s，时间列范围 0–21.4 s，相对起点 0 s。按表头显式补入加速度 g，绝对时间未知。 |
| `IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac` | `3039340c6ccef210edc5ed03444512447414c13028a5d0238bc58eea6e412fba` | 下载来源待确认。ObsPy 核对小端 v6，头段台网 TW、台站 5AFCD、位置 00、通道 HNZ。 | 1 通道，60000 样本，DELTA=0.009999999776482582 s。从参考时刻 `2025-02-07T16:46:58.500Z` 加上 B=-120.5 s，得到首样本时间 `2025-02-07T16:44:58Z`。原件 IDEP 未定义，量和单位均未知。 |
| `20240902_Hualian_TAPBA0.mseed` | `37ae8d8ac355dabb654296a50c34ee6487a4eecf479d1b36d1167c1949a9e964` | 下载来源待确认。ObsPy 与 `mseedview` 确认为标准 v2、大端、FLOAT32（编码 4），2016 个 4096 字节记录。 | 台网 CB、台站 TAPBA、位置空、通道 01–28。每通道 1 条连续 Trace，共 28 条，每条 72000 样本，总计 2016000。dt=0.005 s，首时刻均为 `2024-09-02T08:26:25Z`，量和单位均未知。 |

## T0–T7 结果

| 用例 | 结论 | 执行参数、参考和实际结果 |
|---|---|---|
| T0 格式与元信息 | PASS | 对四份原件运行 `info`。AT2、SAC 和 miniSEED 自动识别，其元信息与样本清单一致。Trinidad 自动模式在第 1 行第 1 列提示设置数据起始行。显式解释后确认 dt、样本数、起点和单位。 |
| T1 AT2 | PASS | `--to csv` 后以 `--dt 0.01 --input-unit g --to at2` 转回，独立解析与 GMTools 回读均为 5093 个样本，dt 和单位保持。幅值最大绝对误差为 0。将 NPTS 改为 5094 或删去时，均保留 5093 个实际样本并给出对应警告。 |
| T2 SAC | PASS（实验性子集） | `--to sac`、`--to csv`。ObsPy 回读 v7 波形和首样本 UTC，Python 解包 v7 footer 核对精确 DELTA 和 B。两种目标均为 60000 样本、最大幅值误差 0。使用分别指定 `--quantity acceleration/velocity/displacement` 的导出验证 IACC/IVEL/IDISP，GMTools 回读均保留物理量且单位仍为 Unknown。 |
| T3 miniSEED v2 读取、v3 写出 | PASS（实验性子集） | `--to mseed` 产出 28 个 v3 FLOAT64 文件。独立解包每条记录的源标识、时间、周期与样本，并同 ObsPy 的 v2 数据对照。全部样本逐值一致，每条输出记录的时间戳与原始参考采样网格一致。28 个输出也通过 `mseedview -s` 和 GMTools 回读。 |
| T3 miniSEED v2 写出与片段边界 | UNSUPPORTED（花莲原件直接写 v2） / PASS（对照用例） | `--to mseed2` 明确拒绝花莲样本的两字符通道标签，未产生目标文件。派生的三字符 NSLC 副本以 FLOAT64 写出后经 ObsPy 核对标识、dt、首时刻和 2020 个样本。另以连续、跳过、重复及调序的原始记录并构造一条采样率改变的记录，确认断点产生 5 条独立 Trace，样本数各为 1010，诊断分别包含缺口、重叠或乱序、采样间隔改变。 |
| T4 Trinidad | PASS | `--format dat --first-line 6 --layout columnar --time-column 1 --input-unit g`。TXT 和 CSV 各输出一条通道，2141 个幅值与独立脚本逐值一致。dt 相对十进制 0.01 s 的差异在 double 舍入范围内。 |
| T5 文本边界 | PASS | 两行三列的连续数值按行读为 1 Trace、6 样本，省略布局则明确拒绝。含时间的三列 CSV 分出 2 条各 3 样本的 Trace，dt=0.01 s。不等间隔时间列报错。未知 dt 和单位允许数值文本导出，AT2 转换同时提示缺失采样间隔和加速度单位，未产生目标文件。 |
| T6 导出与单位 | PASS | 默认导出所有通道，`--channel 2` 仅输出第二条。重名得到 `EW` 和 `EW_CH02`，空名得到 `CH01` 等独立名称。已有目标默认拒绝覆盖，显式覆盖副本有效，输入路径仍受保护。真实 AT2 的 g→m/s²、g→cm/s² 及转回 g 通过逐样本对照，最大允许相对误差为 `4e-16`。`--input-unit` 单独使用时数值保持。 |
| T7 Qt 人工操作 | PASS（Linux X11 虚拟显示） | 在隔离的 1280×900 Xvfb 显示上检查实际 GUI。打开 Helena AT2、Trinidad 与花莲 miniSEED，检查原文行号、解析反馈、样本序号及经过时间轴、不同通道的独立纵轴。花莲的 28 条 Trace 可逐条滚动预览，观察到程序峰值常驻内存约 97 MiB。编辑第 2 通道名称及 dt 后，检查轴和来源提示，取消重解析保留编辑。勾选第 2 通道导出 72000 个数值，与 ObsPy 通道 02 逐值一致。再次同路径导出显示冲突并报告写出 0 个文件。 |

幅值范围经独立参考路径计算。Helena AT2 为 `[-0.1255038, 0.1607605] g`，Trinidad 为 `[-0.1556, 0.1936] g`，SAC 为 `[-2.1730866432, 2.0557689667]`，花莲全通道为 `[-2454.11083984375, 30.709924697875977]`。SAC 波形源和目标均为 float32，本次以相同 float32 数值对照。miniSEED 由 float32 导出为 float64，所有幅值应精确保留。AT2 和 DAT 的波形也逐值比较。时间对照分别使用秒、SAC 参考时间加 B、miniSEED 纳秒首时刻与采样网格。

## 定向修复

- `src/formats/peer.cpp` 允许有有效 DT 的 AT2 缺失 NPTS，保留实际样本数并明确警告。`src/formats/registry.cpp` 同时报告 AT2 导出缺少的采样和单位条件。
- `src/formats/text.cpp` 在确认数值行后才提示选择多列布局。Trinidad 的非数字表头现在直接指出行、列并提示设置首数据行。显式参数仍按原文确定。
- `src/formats/sac.cpp` 写出标准 IDEP 映射，读回时保持工程单位未知。绝对起点与相对起点能通过毫秒参考时刻同时表达时保留两者。实测 SAC 的 B 从修复前的 0 s 恢复为源值 -120.5 s，首时刻仍为 `2025-02-07T16:44:58Z`。
- `src/gui/main.cpp` 缩短信息区和解析反馈中的采样间隔及跨度显示，解决 0.005 s 被显示为长串 IEEE 754 尾数的问题。编辑及核心数据精度保持。

## 复现与边界

本机保留的命令和输出日志位于 `build/acceptance/baseline/` 和 `build/acceptance/verified-*/`，本轮运行摘要为 `build/acceptance/final-summary.log`。数值核验脚本为 `build/acceptance/verify_phase1.py`，临时样本在 `build/acceptance/fixtures/`，界面证据和操作记录位于 `build/acceptance/gui/`。在独立输出目录运行原件完整性检查及主要转换：

```sh
sha256sum -c build/acceptance/original.sha256
./build/gmtools formats
./build/gmtools convert example/RSN1_HELENA.A_A-HMC180.AT2 --to csv -o build/acceptance/recheck/peer/
./build/gmtools convert example/Trinidad.dat --format dat --first-line 6 \
  --layout columnar --time-column 1 --input-unit g --to csv -o build/acceptance/recheck/dat/
./build/gmtools convert example/IES.07w.5AFCD.TW.A1.HNZ.2025.02.07.16.46.58.sac \
  --to sac -o build/acceptance/recheck/sac/
./build/gmtools convert example/20240902_Hualian_TAPBA0.mseed --to mseed -o build/acceptance/recheck/mseed/
```

文本与 AT2 基础闭环通过。SAC 和 miniSEED 保持实验性，本记录仅覆盖表中实际执行的子类型和编码。花莲源标签通过 libmseed 映射到 v3 标识，例如 `01` 对应 `FDSN:CB_TAPBA__0_1_`，未手工改名以强行写回 v2。其他编码、专有 miniSEED 变体、Windows 与桌面环境差异待分别核验。后续可按样本开展新格式适配。[本轮验收方案](../GMTOOLS_PHASE1_ACCEPTANCE_AND_FIXES.md)
