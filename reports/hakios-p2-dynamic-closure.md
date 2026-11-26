# P2原始动态工具子项验收

R24同次照片确认原始18H17的df在31908ms、mount在5209ms均exit0、signal0、timeout0；父进程cleanup NONE/kill0，DYNRESULT PASS。HFSX只读根切换rc0、原始缓存头匹配、438映射rc0及精确14.8旧Mach入口均可见。120236ms/11055次PPI27后正常定时返回，无panic；168份同次冻结证据与原图已核验。动态工具子项结案，不再复测；P1/R20存储保持通过，P2整体、launchd、基础服务、图形/触摸及SpringBoard仍未通过。累计24次HakiOS临时启动，本次仅归档，无设备操作或分区写入。见reports/hakios-device-r24.md。

通过范围为完整HakiOS8796执行原始iOS14.8 dyld/cache、df和mount；保留原始程序与内核检查。下一步沿完整上游init完成必要挂载与原始launchd的PID1交接，不将普通子进程启动当作launchd验收。

[JSON与证据哈希](hakios-p2-dynamic-closure.json)
