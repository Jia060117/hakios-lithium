# 第九轮 HFS MD5 panic 与第十轮修复

第九轮内核 `1afe0a00b73c4ac2615fbcaab8425a27f6892ec9f35f41d99301ffe8108cb859`。照片 `reports/local/20261113-172930-455158-native-xnu/native-xnu-hfs-md5-panic.jpg`，SHA256 `62cb8b128fab89aed0eef05773fee9ec4cb93593c1a0319143bea570b8eeeb64`。

照片显示UFS DMA成功、容量/GPT检查通过、HFSX卷头CRC32=0x6143e5cf以及BSD root=ufsroot0（major2/minor0）。这是卷头读回和根设备选择证据，不能当作全部16GiB读回或挂载完成。

PC=0xfffffff00773317c，LR=0xfffffff0076df408，FAR=0x20，ESR=0x96000005，x22=0。冻结二进制定位PC为MD5Init+0x34的 `ldr x20,[x22,#0x20]`；前一条将g_crypto_funcs载入x22。LR是hfs_getvoluuid调用MD5Init后的返回地址。该函数使用MD5计算HFS版本3 UUID，原7195包装依赖未注册的corecrypto函数表。

0022补丁复用固定ios-sailfish提交6f27e41ff249f2680540ebe8ee5ab982c5e641c9的0027补丁，以及BearSSL提交7bea48e5e850ab4cafbe68d3765cdaba13a86d6f的MD5和小端codec。保留7195的高/低字字节计数与88字节上下文ABI，明确初始化临时缓冲；非LITHIUM代码保持原路径。没有禁用MD5、跳过UUID生成或修改根卷。

验证使用真实corecrypto_md5包装及提取的hfs_getvoluuid函数：RFC1321七个固定向量、十一种填充/块边界输入，各以四种分段宽度更新并复制上下文；零长度、64位字节计数进位、三种HFS UUID与缓存路径通过。新内核MD5入口直接调用BearSSL，冻结第九轮的空表路径被拒绝。报告 `reports/ios14-native-md5-validation.json`。1704370条内核指令扫描、alias和DMA检查及启动包离线校验通过，未运行QEMU。

第十轮于 `2026-11-14T15:00:36.912397+08:00` 从已核验MIX Fastboot临时启动。内核SHA256 `efd37ae450521734f4746da2420adef7f05ff76fae725fe518644efb1f6071eb`；镜像SHA256 `eb6463e43f688204243d3bd50654b9fcc51585b6ce63e625caae6d43bb7a6186`。冻结目录 `reports/local/20261114-150036-826563-native-xnu`，含MD5验证报告与0022补丁。Fastboot接受命令，当前等待手机屏幕；根挂载及launchd均未宣称通过。本轮没有重新写入userdata。
