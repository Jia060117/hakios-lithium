# R38 power05：只修晚到DONE判断

与power04精确参数相同，唯一生产变化是先处理观察到的DONE/错误，再对仍未完成事务检查100us期限；100次poll、时间回退拒绝、错误保留、零供电/PLL写均不变。

Russell追加晚到DONE/晚到busy边界mock，ASan/UBSan及O2分别495cases/83563checks；main逐行核对两处移动与source先DONE语义，实际增量编译成功并核对编译输入hash、BootShim/Android/FAT/APFS/loader/Mu读回。

镜像 `ec7f2b5dd80b7d0c468fcb6005b35c48a63b087fa8ffa77a311b07d6d7c01deb`，内核 `eb5b407451e41be6b2b14ff4c6b7d24d034b1ba30470e396de0d989c35a83a4a`。未上机；下一步一次RAM采集正常结束回Fastboot。
