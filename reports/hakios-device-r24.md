# HakiOS R24：原始动态工具通过

R24同次照片确认原始18H17的df在31908ms、mount在5209ms均exit0、signal0、timeout0；父进程cleanup NONE/kill0，DYNRESULT PASS。HFSX只读根切换rc0、原始缓存头匹配、438映射rc0及精确14.8旧Mach入口均可见。120236ms/11055次PPI27后正常定时返回，无panic；168份同次冻结证据与原图已核验。动态工具子项结案，不再复测；P1/R20存储保持通过，P2整体、launchd、基础服务、图形/触摸及SpringBoard仍未通过。累计24次HakiOS临时启动，本次仅归档，无设备操作或分区写入。见reports/hakios-device-r24.md。

df实际耗时超过上一轮30秒预算，足以解释R23测试父进程的超时清理；具体首次加载成本没有单独测量。照片未显示df BSD调用摘要，不能将该显示功能记为真机观测。无需为诊断行再重复已通过的工具测试。

[同次JSON](hakios-device-r24.json) · [动态子项验收](hakios-p2-dynamic-closure.md)
