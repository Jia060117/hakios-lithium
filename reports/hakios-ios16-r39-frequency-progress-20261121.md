R39 逐档频率筛选进展（2026-11-21）

本次接受5次隔离RAM临时启动；不访问物理UFS、不写电压、不刷分区。最高通过90秒性能簇双核并发筛选为537.6MHz配置模型；最终甜点尚未确定。

|尝试|CPU配置模型 MHz|负载范围|持续时间|perf最高°C|结果|Fastboot终点|
|---|---:|---|---:|---:|---|---|
|20|384|三核并发|29.668s|64|pwr触75°C温控|已核实|
|23|384|双perf并发/pwr housekeeping|90s|58.2|通过短筛选|已核实|
|24|460.8|双perf并发/pwr housekeeping|90.001s|62.7|通过短筛选|已核实|
|25|537.6|双perf并发/pwr housekeeping|90s|66.9|通过短筛选|已核实|
|26|614.4|双perf并发/pwr housekeeping|未知|未获日志|未获内核日志，停点未知|未检测到|

20的phys1峰75.5°C，三核校验均valid1并join1，但29.668s提前停载；不能当全三核满载通过，也不能将热限计数当作算错。23/24/25源争用丢字节均0、正常timer/drain、四路stop温度及USB连续性均通过。其完整JSON保留接收器Pipe error终态，未将它替换为进程成功。

26的614.4MHz使用source-backed EARLY/MUX5，CPU策略600mV但S11要求新鲜905mV数字设定、没有电压写入。23:37:02接受，180秒USB枚举窗口未见设备/0帧；23:41:24唯一Fastboot枚举为空，product跳过，无重查或重试。PLL/MUX实值、XNU是否进入、温度、负载及失败原因都未知，需当前屏幕原图，之后人工Fastboot。不得把零日志当频率不稳或自动复位成功。

主机变更：guard04每256批worker温度/年龄保护及1ms让出；guard05默认关闭cpu_rate_soak_perf_only隔离热源；日志重试默认关闭且不放宽丢字节门禁。Noether各35/50场景O0/O2 ASan/UBSan实测、main检查真实输出及测试/构建hash。Kepler给出614及691.2–902.4非ACD direct源码、模型与真实helper主机测试；main保留精确CLKCTL约束、核对源pin/模型表/实际构建与FD，并接入。higher table已有实际rate07内核与support03，691.2候选27主机逐层读回通过，未上机，不是下一次允许跳过614的包。

新构建：rate04 bee740c2、rate05 da4063d1、rate06 55cdbb78、rate07 37bab55a；旧冻结均保留，R20/low01未覆盖。回退依据guard04-main、guard05-main、early06-main、direct07-main的before备份与阶段补丁；原索引fdf775be保持。没有提交vendor二进制。

余下：先用26当前屏幕查明EARLY切换/最终谓词/更早停点；再继续逐档。10分钟热稳模式的独立未应用补丁由Noether准备；未接入当前冻结内核。PMU有效MHz、模拟电压、TSENS校准、桌面长期热稳定、第四核XNU仍未完成。本组RAM测试不能代替UFS桌面验收，UFS自动Fastboot仍未支持。

10分钟模式主机补充：long06未应用补丁已有真实include/pthread专项通过，main核对live base与proposed SHA；只在RAM/perf_only/显式long开关允许600秒负载、720秒返回，75°C停载不变。它没有进入本轮冻结镜像，也未上机；等待614屏幕定位期间不接入额外生产改动。
