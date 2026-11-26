R14 prepared: fixed the CPU diagnostic logical-ID bound. Full HakiOS kernel/Mu/D22AP/413 jobs, existing regressions plus strict CPU enumeration ASan/UBSan and 46 instruction/16 package checks passed. Actual frozen binary calls ml_get_max_cpu_number and uses its inclusive bound before cpu_to_processor. UFS/2880MiB/3 XNU CPUs/600-second test retained; no HFS/flush/clock change or userdata reinstall. R13 image/log/photo and dirty-source backup retained. Runtime/HFS diagnosis/SpringBoard still unverified.

Image: artifacts/hakios/lithium-ios16/phase4-r14/lithium-hakios-ios16-r14.img
SHA256: 6201aae3c699f601488c2fd59540f59a5453844ace89384ce919fbc5393c67fc
