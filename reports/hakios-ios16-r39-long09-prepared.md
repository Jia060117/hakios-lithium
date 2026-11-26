R39 十分钟持续负载候选

默认仍为90秒负载/180秒会话。新增显式cpu_rate_soak_long=1只允许隔离RAM、perf_only1、三核在线和720秒会话；worker GO后跑600秒。温控75°C、无效或过期测温停载、三worker退出确认及原正常timer/USB/Fastboot流程保持。

完整HakiOS rate09内核c1e5c4e0实际构建，复用已冻结support04；候选33=614.4、34=537.6、35=691.2、36=748.8MHz均已逐层打包/主机admission。新独立run-cpu-r39-long-measure.py仅接受600/720 profile，780–1080秒一次终点核验，USB最长1270秒；原R37runner不改，19项纯主机测试通过。long06实际include/pthread补丁专项通过，main核对完整patched源码hash后接入；不是只凭测试名称判断。

每档只在上机后验证持续温度/校验/返回，当前全部长测候选未启动。shared S11保持新鲜读回的905mV数字配置，无regulator写，CPU模型按原厂档位。十分钟负载仍不等同整日长稳，也不是UFS桌面或第四核验收。

回退见evidence/cpu-r39-20261121/long09-main/r39-long09-followup.patch及before。
