# R38 stock 频压依据、温度接口与低档 UFS 集成

2026-11-21。本阶段仅主机源码、构建与针对性验证，未查询或启动手机，计数保持 HakiOS77 / iOS16 46。

## 结论与本阶段边界

小米下游 MiCode1492c8e4373b7428bb0accc4f4881e97b36508b9、lithium stock DTS、该机真实 raw64 fuse 的 rawbin0/CPR rev2/primary/partial0/limitation0 已匹配。**下一档是307.2→384.0MHz，只前进一个 stock corner。** 307.2与384.0的 CPU 侧开环策略均为585mV，含15mV最大 aging 余量。不能把开环模型冒充实际 CPU 电压，也不能从数字设定推导模拟电压。

S11此前实读的数字设定为905mV。它是共享供电轨的设定，不能直接降成585mV，也不应仅为“每档升压”而强行高于905mV。后续按电压条件先于频率的顺序：同次重新核对供电与稳定状态；不足时先由有来源且有所有权的后端升压/等待/读回，再升频。当前尚无接入生产的电压写后端，未写电压。

**本阶段没有完成更高频实机稳定性测试，也没有完成 UFS 桌面实机验收。** 本机有效 PMU MHz 仍未取得；307.2MHz是已通过性能对照的配置模型。仍只有三个XNU核，phys0保留UEFI/UFS服务。

## 本机各档的 CPU 侧策略模型

单位MHz/mV；全表已计入stock最大aging。floor/open/ceiling为CPR软件策略，非S11写入表。完整计算过程和每个source:line/hash见 `evidence/cpu-r38-20261121/stock-opp/stock-opp.md/json`。绝对上限字段也是软件策略上限，非器件绝对安全电压。

| stock MHz | aged floor mV | aged open mV | aged ceiling mV |
|---:|---:|---:|---:|
| 307.2 | 505 | 585 | 585 |
| 384 | 505 | 585 | 585 |
| 460.8 | 505 | 585 | 585 |
| 537.6 | 505 | 585 | 585 |
| 614.4 | 520 | 600 | 600 |
| 691.2 | 530 | 610 | 610 |
| 748.8 | 540 | 620 | 620 |
| 825.6 | 555 | 635 | 635 |
| 902.4 | 565 | 645 | 645 |
| 979.2 | 575 | 655 | 655 |
| 1056 | 595 | 675 | 675 |
| 1132.8 | 615 | 695 | 695 |
| 1209.6 | 630 | 710 | 710 |
| 1286.4 | 650 | 730 | 730 |
| 1363.2 | 665 | 745 | 745 |
| 1440 | 690 | 770 | 770 |
| 1516.8 | 710 | 790 | 790 |
| 1593.6 | 735 | 815 | 815 |
| 1670.4 | 755 | 835 | 835 |
| 1747.2 | 775 | 855 | 855 |
| 1824 | 800 | 880 | 880 |
| 1900.8 | 820 | 900 | 900 |
| 1977.6 | 840 | 920 | 920 |
| 2054.4 | 865 | 945 | 945 |
| 2150.4 | 890 | 970 | 970 |
| 2246.4 | 920 | 1000 | 1000 |
| 2342.4 | 945 | 1025 | 1025 |

高档还涉及ACD、mem-acc和供电协作；本表不是把当前低档PLL的L值直接拉至高档的许可。

## 温度与供电来源

`evidence/cpu-r38-20261121/thermal/thermal-read-path.md` 给出TSENS0物理资源0x004a8000/0x2000和CPU clients4/6/9/11。源码是TYPE3、原生有符号12位0.1℃；无需软件借用别的SoC fuse校准公式。采样器最多每sensor六次只读，要求ENABLE及STATUS VALID；无效则温度不可用，绝不用0℃或室温补值。校准来源和转换新鲜度未由当前运行证明。该reader已通过主机测试，**尚未接入原生loader/kernel，更没有真实温度记录**。

`evidence/cpu-r38-20261121/voltage-control/report.md` 给出原厂CPR3共享聚合→S11 SPM→system SAW port0的控制链、完成/稳定顺序。observer读成功不证明写所有权；未猜RPM资源ID、未把总线ACK当电压稳定。纯主机状态模型覆盖温度→升压或保持→读回/等待→切频→负载/结束温度，未接硬件。

## UFS低档接入

主代理新增 `ports/hakios-lithium/return-fastboot/cpu-perf-ufs-profile.h:10`，并在 `loader.inc:46` 的既有pre-CPU_ON位置接入 `cpu-perf-uefi-r38.inc:446`。仅显式 `cpu_perf_fixed=1` 与UFS专用选项同时启用，要求bootcpu1/cpus3、真实UFS块提供者、允许正常文件系统写入、所有未来XNU核OFF、ownerphys0、当前session与全套fuse/S11/BHS/AVS读回。默认关闭；保持R14已成功的307.2MHz模型与11写顺序，不写电压。

CPU初始化session与RAM自动返回cookie分开，**不产生UFS storage seal或Fastboot返回证明**。UFS自动返回仍未具备，不能用这个包作为无人值守自动返回测试。没有改变现有分区或刷写任何内容。

独立产物：`artifacts/hakios/lithium-ios16/phase4-r38-ufs-perf-low01/lithium-hakios-ios16-r38-ufs-perf-low01.img`

- image SHA256 `bb4e8e23e0cc7d9af8198e6974c92cd51cbfdbc2c070b181c59027a608acb5b6`，16334848bytes。
- 复用已构建的完整HakiOS内核 `eb5b407451e41be6b2b14ff4c6b7d24d034b1ba30470e396de0d989c35a83a4a`；未为未改的内核/用户态重复构建。
- root与R34桌面root逐字节相同，保留R20显示1080×2040/stride3240及三核touch_smp参数。
- 新loader/Mu实际编译；从最终FD解压嵌套FV并按GUID解析，确认真实UFS与Fastboot EFI的PE32字节完全一致；整包Android/FAT/payload逐层读回。
- 参数与全部组件哈希见该目录contract.json。`return_fastboot=false`，定时仍为原10800秒；**未上机**。

## 验证与并行执行记录

- Hilbert：27档实际fuse计算器、17项针对性测试。main独立复跑、核对DTS5300与10337–10360以及原计算流程，匹配。
- Locke：TSENS主来源、只读解析器8235场景/147954check、AArch64 freestanding检查。main核对测试源hash及原驱动CTRL/VALID/type/sign逻辑；未冒充实机温度。
- Newton：供电链与稳定时序、17项纯主机顺序测试。main独立复跑，确认未生成猜测的电压写接口。
- Curie：实际UFS helper/loader42case2244check；RAM基线5case1363check，ASan/UBSan通过。main读回实际源hash/编译日志/关键mock控制流。
- Goodall：独立UFS构建脚本。实际loader/Mu编译通过；首次后检查因Fv.map不列压缩模块而停止，未假称编译失败。改用最终FD→LZMA→嵌套FV→FFS→PE32精确证明后通过，不重复构建、不删除旧工作区。main重新解析最终FD并核对10个实际写出组件。
- main：UFS共享生产文件、打包、复核、报告与可回退记录。没有并行生产写冲突。只跑对应新增逻辑，没有历史总回归。

构建命令（已经执行；新目录不可重复覆盖）：

```powershell
wsl -d Ubuntu -u root --exec systemd-run --unit=ios-mix1-r38-ufs-low01 --wait --pipe --collect -p MemoryMax=14G -p MemoryHigh=12G -p MemorySwapMax=1G -p CPUQuota=1200% -p TasksMax=1024 -p User=jia -p WorkingDirectory=/mnt/f/IOS-MIX1 --setenv=JOBS=12 --setenv=MAKEFLAGS=-j12 /home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/scripts/build-hakios-r38-ufs-support.py --build
```

主机测试命令：

```powershell
python -B evidence/cpu-r38-20261121/stock-opp/test_stock_opp.py
python -B evidence/cpu-r38-20261121/voltage-control/test_model.py
wsl -d Ubuntu -u jia --exec /home/jia/ios-mix1-build/venv/bin/python -B /mnt/f/IOS-MIX1/evidence/cpu-r38-20261121/ufs-tests/run-tests.py
```

## 后续真机顺序与未验证项

1. 先把只读TSENS接入，仍用已经通过的307.2MHz/RAM自动返回配置，记录真实温度与raw/valid/时间戳，不增加频率变量。
2. 同次供电数字配置、稳定条件和有效温度确认后，仅测试下一档384MHz，记录升频前、两个perf核负载中、结束的温度、错误及性能。温度不可用则停止高档；无需再次申请用户已授予的测试权限。
3. 对照一致负载，不以三分钟微基准称长期稳定，也不以编程模型当PMU实测。
4. UFS桌面包继续做原生温度集成及写入封存/自动返回；这些就绪前不启动当前10800秒、不能自动回Fastboot的包。
5. 第四核仍依赖释放residentUFSworker，本次不使用cpus4伪验收。

R14已通过的RAM包、R20及旧冻结和索引保持不变；无固件/Apple二进制加入补丁，无git reset/clean/add-A。完整JSON记录未验证项及作者。
