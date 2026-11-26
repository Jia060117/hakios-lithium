"""Record display delivery and remaining physical acceptance, with no phone access."""
from datetime import datetime
import hakios


def line(path, needle):
    for i,text in enumerate(path.read_text(encoding='utf-8').splitlines(),1):
        if needle in text:return i
    raise RuntimeError('Missing documented source anchor: '+str(path)+':'+needle)


def main():
    root=hakios.ROOT;spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);work=hakios.verify_workspace(spec)
    ready=hakios.read(root/'reports/hakios-ios16-boot-ready.json')
    hakios.require(ready['revision']==16 and ready['passed'],'Frozen R16 required')
    hakios.require(ready['inputs']==hakios.inputs(spec),'Display inputs changed')
    hakios.require(ready==hakios.read(root/ready['frozen_contract_path']),'Display contract differs')
    for name,digest in ready['evidence_sha256'].items():
        hakios.require(hakios.sha(root/name)==digest,'Frozen evidence changed: '+name)
    prior=hakios.read(root/'artifacts/hakios/lithium-ios16/phase4-r15/contract.json')
    hakios.require(ready['cmdline']==prior['cmdline'],'A frozen R15 boot argument changed')
    proofs=['reports/hakios-ios16-r16-checks.json','reports/hakios-ios16-r16-package-checks.json',
        'reports/hakios-ios16-phase4-build-checks.json','reports/hakios-ios16-phase4-userspace-build.json',
        'reports/hakios-ios16-boot-userspace-r16.json','reports/hakios-ios16-package-host-checks.json',
        'reports/hakios-kernel-build.json','reports/hakios-support-build.json']
    for name in proofs:hakios.require(hakios.read(root/name).get('passed',True),'Failed proof: '+name)
    contract=hakios.read(root/'ports/hakios-lithium/diagnostics/r16.json')
    anchors={
        'ports/hakios-lithium/diagnostics/display-profile.h':'lithium_display_resolve(',
        'ports/hakios-lithium/diagnostics/display-kernel.inc':'lithium_display_dt_log(',
        'ports/hakios-lithium/diagnostics/display-userspace.inc':'lithium_display_dump(',
        'ports/hakios-lithium/patches/0016-lithium-display-profile.patch':'+\tuint32_t scale',
        'scripts/hakios_r16.py':'def framebuffer_source',
        'scripts/prepare-hakios-ios16-r16.py':'def main',
        'scripts/test-hakios-ios16-r16.py':'def main',
        'scripts/test-hakios-ios16-r16-package.py':'def main'}
    changed={name:line(root/name,needle) for name,needle in anchors.items()}
    active={name:line(work/name,needle) for name,needle in {
        'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp':'struct lithium_display_profile profile',
        'userland/src/inject_sb.c':'if (lithium_display_native_statusbar()) return;'}.items()}
    tasks=['hakios-r16-prepare','hakios-deps','hakios-r16-review','hakios-r16-regressions',
        'hakios-kernel','hakios-support','hakios-r16-userspace','hakios-r16-build-check',
        'hakios-r16-freeze','hakios-r16-package','hakios-r16-package-checks']
    result=dict(revision=16,status='host_prepared_display_acceptance_pending',passed=True,
        prepared_at=datetime.now().astimezone().isoformat(),image_path=ready['image_path'],
        image_sha256=ready['image_sha256'],image_bytes=ready['image_bytes'],kernel_sha256=ready['kernel_sha256'],
        baseline=hakios.PIN,userspace='16.5/20F66/D22AP/arm64',payload_boot_args=ready['cmdline'],
        boot_args_byte_identical_to_r15=True,backup={k:v for k,v in contract['backup'].items() if k!='prior_inputs'},
        source_lines=changed,active_source_lines=active,build_tasks=tasks,
        evidence_sha256={name:hakios.sha(root/name) for name in proofs},
        physical_panel=dict(width=1080,height=2040,stride_bytes=3240,format='BGR24'),
        render=dict(width=750,height=1334,format='BGRA32',prior_stride_measured=3072),
        intended_profile=dict(scale=2,artwork_subtype=1334,points=[375,667]),
        prior_uiscreen_bounds_measured=False,framebuffer_mapping_changed=False,
        phone_query_performed=False,phone_boot_performed=False,phone_flash_performed=False,
        userdata_reinstall_required=False,hardware_verified=False,dock_verified=False,
        labels_verified=False,content_bounds_verified=False,mg_answers_verified=False,
        uiscreen_bounds_verified=False,physical_touch_verified=False)
    hakios.write(root/'reports/hakios-ios16-r16-prepared.json',result)
    build='\n'.join('.\\scripts\\run-wsl-limited.ps1 -Task '+t for t in tasks)
    source='\n'.join('- `'+name+':'+str(number)+'`' for name,number in changed.items())
    active_text='\n'.join('- `'+str(work/name)+':'+str(number)+'`' for name,number in active.items())
    md=f'''# R16 显示配置修订交付

已完成源码备份、修订、完整 HakiOS/Mu/D22AP/用户空间构建及冻结检查。本轮没有查询、重启或刷写手机；当前手机继续运行 R15。**Dock、标签和实际屏幕边界验收均未验证。**

## 结论和修复范围

`3240` 是 BGR24 物理面板的每行字节数，`3240 / 3 = 1080`。没有观测到 810 像素宽的缓冲；810 来自将三字节像素误按四字节计算。BGRA32 渲染面另有 3072 字节对齐 stride，对应可见宽 750、行容量 768，二者不是物理面板行格式。

R15 实测 backboardd 接收 `750×1334 / scale=3`；供给 D22AP 身份解析为 artwork subtype 2436。其 `250×444.667` 点只是尺寸除法，R15 没有真实 UIScreen.bounds 记录。此配置与屏幕类别不一致是具体修正对象，尚不能将全部照片缺陷归因于它。

R16 对 Lithium 当前准确的 `750×1334` 且未显式指定 fbscale/fbsubtype 的配置，复用上游设备树设置路径，采用 scale=2、artwork subtype=1334，目标逻辑尺寸为 `375×667`。显式参数及其它画布沿用上游含义。设备树确认为该配置时，SpringBoard 不再安装 iPhone X 的 split status-bar fallback，而由 UIKit 选择原生 provider。物理面板、stride、BGRA→BGR24、CPU/UFS/RAM 和完整画面到面板的映射保持原行为。

原映射把整个画布映射至 1080×2040，不会按几何公式裁掉左右边缘，但仍有约 6.2% 的纵横比例差异。此轮保留映射以单独验证显示 profile 修正，等比居中尚未实施；若仍需消除拉伸，应在尺寸日志通过后另作修订。

只读尺寸链路及 R15 行号见 `reports/hakios-ios16-display-audit.md`。

## 关键改动和当前行号

{source}

实际活动源码（WSL）：

{active_text}

其余脚本改动仅注册 R16、验证精确 before/after 哈希链并延续已有构建与回归门禁。没有去掉功能或放宽未知改动检查。实际函数 ASan/UBSan、显式 profile 保留、DT 属性缺失/格式异常、R15 原文反向重构、既有 1650 项像素对照通过；46 项编译指令检查、413 项用户空间任务、16 项实际包检查及 R16 payload 参数逐字节读回通过。主机检查不等于真机通过。

备份分支 `{result['backup']['branch']}`；外部 dirty-source.zip SHA256 `{result['backup']['sha256']}`。分支保留原提交，外部压缩包保留 R15 未提交源码和实际编译树差异；R15 冻结镜像未覆盖。Apple 二进制和供给固件没有加入 Git 或共享材料。

## 冻结镜像与全部启动参数

私有镜像：`{ready['image_path']}`，{ready['image_bytes']} 字节。

SHA256：`{ready['image_sha256']}`。

```text
{ready['cmdline']}
```

该 payload 参数与冻结 R15 **逐字节一致**，尤其 `bootcpu=1 cpus=1`。加载器另外补充 debug、rd、动态分配的 ufs_shared_pa 和实际 hakios_kver；完整运行参数以新日志 `LITHIUM BOOTARGS` 为准，不能沿用上次动态地址。

```powershell
{build}
wsl -d Ubuntu -u jia --exec python3 /mnt/f/IOS-MIX1/scripts/test-hakios-ios16-r16-package.py
```

以上命令已完成。真机准备好 Fastboot 后，使用受保护入口临时启动新包一次：

```powershell
python scripts/boot-hakios-ios16.py --boot
```

该入口检查新冻结证据和设备身份、提前启动 USB 接收；不需要重新刷 userdata。此交付未执行这条启动命令。

## 回传日志与照片

保留 `LITHIUM BOOTARGS`、`LITHIUM RUN A`，以及所有 `LITHIUM DISPLAY DT / GEOMETRY / STATUSBAR / MG / UISCREEN / WINDOWS / WINDOW`，加上 `MiniIOMobileFramebuffer: display`、backboardd `main display info` 和实际 IOSurface 创建行。

预期：DT before 3/3/2436，after 2/2/1334；GEOMETRY render=750×1334、panel=1080×2040、panel_stride=3240、panel_bpp=24、auto_profile=1；UIScreen 预期 bounds=0,0,375,667、scale=2，nativeBounds/nativeScale 和 MG 每项须读取真实值。MG ScreenDimensions 若不存在，会明确打印 unavailable，不伪造返回值。日志在主队列运行后分三次有界输出，构造器不会查询 UIScreen。R15 缺失的 UIKit 实测值不能事后补成“修复前实测”。

请用另一设备正对手机拍一张完整主屏幕，四边和整个 Dock 都在照片中；若标签不清，再补一张底部两排图标与 Dock 的近照，保持镜头尽量垂直。无需守屏或抓瞬间。

验收：Dock 四个图标完整、标签不被下一行遮挡、界面内容不越过左右及底部。所有这些项目、MG/UIScreen 真机值、原生状态栏 provider 结果、物理触摸均**未验证**。首次图层或白苹果不是本次验收通过。
'''
    (root/'reports/hakios-ios16-r16-prepared.md').write_text(md,encoding='utf-8',newline='\n')
    note=('R16 显示修订已冻结（2026-11-18）：750×1334 隐式 profile 对齐 scale2/subtype1334，新增 DT/MG/UIScreen/window 有界日志；'
        'BGR24 物理面板1080×2040/stride3240保持，810像素是误按4字节计算；全部R15 payload参数逐字节保留，包括bootcpu=1 cpus=1。'
        '完整构建/回归/包检查通过，未查询/重启/刷写手机，不重刷userdata，R15运行保留。Dock/标签/边界和新尺寸真机结果未验证。'
        '备份archive/ios16-r15-before-r16-20261118及外部dirty-source.zip已校验；见reports/hakios-ios16-r16-prepared.md/json。')
    for name in ('AGENTS.md','PLAN.md','OPERATIONS.md'):
        path=root/name;text=path.read_text(encoding='utf-8')
        if not text.startswith('R16 显示修订已冻结'):path.write_text(note+'\n\n'+text,encoding='utf-8',newline='\n')
    state=hakios.read(root/'project.json')
    state.update(stage='iOS16 phase4 R16 display correction frozen; R15 phone remains running',
        current_report='reports/hakios-ios16-r16-prepared.json')
    state['work_status'].update(status='ios16_phase4_r16_display_prepared',
        next_step='When Fastboot is physically ready, temporary boot R16 once and verify Dock, labels, bounds and runtime size chain')
    state['active_kernel'].update(kernel_sha256=ready['kernel_sha256'],latest_kernel_binary_hardware_verified=False,
        status='R16 display build frozen; current physical phone still R15; new display acceptance pending')
    state['ios16_r16']=result
    hakios.write(root/'project.json',state)
    print('R16 delivery recorded; boot arguments preserved; physical display acceptance explicitly unverified')


if __name__=='__main__':main()
