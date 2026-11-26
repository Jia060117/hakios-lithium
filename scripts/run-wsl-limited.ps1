[CmdletBinding()]
param(
    [ValidateSet('hakios-audit', 'hakios-version-audit', 'hakios-prepare', 'hakios-check', 'hakios-deps', 'hakios-support', 'hakios-storage', 'hakios-dynamic', 'hakios-kernel', 'hakios-build-check', 'hakios-package', 'hakios-r9-prepare', 'hakios-r9-checks', 'hakios-r9-review', 'hakios-r9-return-checks', 'hakios-r9-static-checks', 'hakios-r9-regressions', 'hakios-r9-userspace', 'hakios-r9-build-check', 'hakios-r9-freeze', 'hakios-r9-package', 'hakios-r9-package-checks', 'hakios-r10-prepare', 'hakios-r10-checks', 'hakios-r10-review', 'hakios-r10-regressions', 'hakios-r10-userspace', 'hakios-r10-build-check', 'hakios-r10-freeze', 'hakios-r10-package', 'hakios-r10-package-checks', 'hakios-r11-prepare', 'hakios-r11-checks', 'hakios-r11-review', 'hakios-r11-regressions', 'hakios-r11-userspace', 'hakios-r11-build-check', 'hakios-r11-freeze', 'hakios-r11-package', 'hakios-r11-package-checks', 'hakios-r12-prepare', 'hakios-r12-checks', 'hakios-r12-review', 'hakios-r12-regressions', 'hakios-r12-userspace', 'hakios-r12-build-check', 'hakios-r12-freeze', 'hakios-r12-package', 'hakios-r12-package-checks', 'hakios-r13-prepare', 'hakios-r13-checks', 'hakios-r13-review', 'hakios-r13-regressions', 'hakios-r13-userspace', 'hakios-r13-build-check', 'hakios-r13-freeze', 'hakios-r13-package', 'hakios-r13-package-checks', 'hakios-r14-prepare', 'hakios-r14-checks', 'hakios-r14-review', 'hakios-r14-regressions', 'hakios-r14-userspace', 'hakios-r14-build-check', 'hakios-r14-freeze', 'hakios-r14-package', 'hakios-r14-package-checks', 'hakios-r15-prepare', 'hakios-r15-checks', 'hakios-r15-review', 'hakios-r15-regressions', 'hakios-r15-userspace', 'hakios-r15-build-check', 'hakios-r15-freeze', 'hakios-r15-package', 'hakios-r15-package-checks', 'hakios-r16-prepare', 'hakios-r16-checks', 'hakios-r16-review', 'hakios-r16-regressions', 'hakios-r16-userspace', 'hakios-r16-build-check', 'hakios-r16-freeze', 'hakios-r16-package', 'hakios-r16-package-checks', 'hakios-r17-prepare', 'hakios-r17-checks', 'hakios-r17-review', 'hakios-r17-regressions', 'hakios-r17-userspace', 'hakios-r17-build-check', 'hakios-r17-freeze', 'hakios-r17-package', 'hakios-r17-package-checks', 'hakios-r18-prepare', 'hakios-r18-checks', 'hakios-r18-review', 'hakios-r18-regressions', 'hakios-r18-userspace', 'hakios-r18-build-check', 'hakios-r18-freeze', 'hakios-r18-package', 'hakios-r18-package-checks', 'hakios-r19-prepare', 'hakios-r19-checks', 'hakios-r19-review', 'hakios-r19-regressions', 'hakios-r19-userspace', 'hakios-r19-build-check', 'hakios-r19-freeze', 'hakios-r19-package', 'hakios-r19-package-checks', 'kernel', 'native-package', 'root-transfer')]
    [string]$Task = 'hakios-check',
    [ValidateSet(1,3)]
    [int]$XnuCpus = 3
)

$ErrorActionPreference = 'Stop'
if ($Task -in @('kernel', 'native-package', 'root-transfer', 'hakios-dynamic', 'hakios-storage', 'hakios-package', 'hakios-version-audit')) {
    throw "Historical task '$Task' is disabled by AGENTS.md and the approved iOS16 phase boundary."
}
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resources = Get-Content -LiteralPath (Join-Path $projectRoot 'config/hakios-resources.json') -Raw | ConvertFrom-Json
$linuxRoot = (& wsl.exe -d Ubuntu -u jia --exec wslpath -u $projectRoot).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve project path in Ubuntu' }
$probe = & wsl.exe -d Ubuntu -u jia --exec python3 -c 'import json,os,pathlib; m=dict(x.split(":",1) for x in pathlib.Path("/proc/meminfo").read_text().splitlines()); print(json.dumps(dict(cpus=os.cpu_count(),memory=int(m["MemTotal"].split()[0])*1024,swap=int(m["SwapTotal"].split()[0])*1024)))'
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect Ubuntu resource limits' }
$limits = $probe | ConvertFrom-Json
if ($limits.memory -gt ($resources.wsl_memory_gib * 1GB) -or
    $limits.memory -lt (($resources.wsl_memory_gib - 1) * 1GB) -or
    $limits.cpus -ne $resources.wsl_processors -or $limits.swap -ne ($resources.wsl_swap_gib * 1GB)) {
    throw 'Apply the approved WSL configuration in config/hakios-resources.json and restart WSL first.'
}
$command = switch ($Task) {
    'hakios-audit' { 'audit' }
    'hakios-prepare' { 'prepare' }
    'hakios-check' { 'check' }
    'hakios-kernel' { 'build-kernel' }
}
$entryScript = "$linuxRoot/scripts/hakios.py"
if ($Task -eq 'hakios-deps') {
    $entryScript = "$linuxRoot/scripts/hakios-dependencies.py"
}
$python = '/usr/bin/python3'
if ($Task -eq 'hakios-support') {
    $entryScript = "$linuxRoot/scripts/build-hakios-support.py"
    $python = '/home/jia/ios-mix1-build/venv/bin/python'
}
if ($Task -eq 'hakios-build-check') {
    $entryScript = "$linuxRoot/scripts/check-hakios-ios16-build.py"
}
if ($Task -eq 'hakios-package') {
    $entryScript = "$linuxRoot/scripts/package-hakios-lithium.py"
}
if ($Task -eq 'hakios-storage') {
    $entryScript = "$linuxRoot/scripts/build-hakios-storage-probe.py"
}
if ($Task -eq 'hakios-dynamic') {
    $entryScript = "$linuxRoot/scripts/build-hakios-ios14-dynamic.py"
}
if ($Task -eq 'hakios-version-audit') {
    $entryScript = "$linuxRoot/scripts/audit-hakios-ios14-version.py"
}
if ($Task -eq 'hakios-r9-prepare') {
    $entryScript = "$linuxRoot/scripts/prepare-hakios-ios16-r9.py"
}
if ($Task -eq 'hakios-r9-checks') {
    $entryScript = "$linuxRoot/scripts/test-hakios-ios16-r9.py"
}
if ($Task -eq 'hakios-r9-review') {
    $entryScript = "$linuxRoot/scripts/review-hakios-ios16.py"
}
if ($Task -eq 'hakios-r9-return-checks') {
    $entryScript = "$linuxRoot/scripts/test-hakios-ios16-return.py"
}
if ($Task -eq 'hakios-r9-static-checks') {
    $entryScript = "$linuxRoot/scripts/test-hakios-ios16-static.py"
}
if ($Task -eq 'hakios-r9-regressions') {
    $entryScript = "$linuxRoot/scripts/run-hakios-ios16-r9-regressions.py"
}
if ($Task -eq 'hakios-r9-userspace') {
    $entryScript = "$linuxRoot/scripts/build-hakios-ios16-userspace.py"
}
if ($Task -eq 'hakios-r9-build-check') {
    $entryScript = "$linuxRoot/scripts/check-hakios-ios16-build.py"
}
if ($Task -eq 'hakios-r9-freeze') {
    $entryScript = "$linuxRoot/scripts/freeze-hakios-ios16-boot-userspace.py"
}
if ($Task -eq 'hakios-r9-package') {
    $entryScript = "$linuxRoot/scripts/package-hakios-ios16.py"
}
if ($Task -eq 'hakios-r9-package-checks') {
    $entryScript = "$linuxRoot/scripts/test-hakios-ios16-package.py"
}
if ($Task -like 'hakios-r10-*') {
    $r10Entry = switch ($Task) {
        'hakios-r10-prepare' { 'prepare-hakios-ios16-r10.py' }
        'hakios-r10-checks' { 'test-hakios-ios16-r10.py' }
        'hakios-r10-review' { 'review-hakios-ios16.py' }
        'hakios-r10-regressions' { 'run-hakios-ios16-r10-regressions.py' }
        'hakios-r10-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r10-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r10-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r10-package' { 'package-hakios-ios16.py' }
        'hakios-r10-package-checks' { 'test-hakios-ios16-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r10Entry"
}
if ($Task -like 'hakios-r11-*') {
    $r11Entry = switch ($Task) {
        'hakios-r11-prepare' { 'prepare-hakios-ios16-r11.py' }
        'hakios-r11-checks' { 'test-hakios-ios16-r11.py' }
        'hakios-r11-review' { 'review-hakios-ios16.py' }
        'hakios-r11-regressions' { 'run-hakios-ios16-r11-regressions.py' }
        'hakios-r11-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r11-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r11-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r11-package' { 'package-hakios-ios16.py' }
        'hakios-r11-package-checks' { 'test-hakios-ios16-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r11Entry"
}
if ($Task -like 'hakios-r13-*') {
    $r13Entry = switch ($Task) {
        'hakios-r13-prepare' { 'prepare-hakios-ios16-r13.py' }
        'hakios-r13-checks' { 'test-hakios-ios16-r13.py' }
        'hakios-r13-review' { 'review-hakios-ios16.py' }
        'hakios-r13-regressions' { 'run-hakios-ios16-r13-regressions.py' }
        'hakios-r13-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r13-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r13-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r13-package' { 'package-hakios-ios16.py' }
        'hakios-r13-package-checks' { 'test-hakios-ios16-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r13Entry"
}
if ($Task -like 'hakios-r14-*') {
    $r14Entry = switch ($Task) {
        'hakios-r14-prepare' { 'prepare-hakios-ios16-r14.py' }
        'hakios-r14-checks' { 'test-hakios-ios16-r14.py' }
        'hakios-r14-review' { 'review-hakios-ios16.py' }
        'hakios-r14-regressions' { 'run-hakios-ios16-r14-regressions.py' }
        'hakios-r14-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r14-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r14-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r14-package' { 'package-hakios-ios16.py' }
        'hakios-r14-package-checks' { 'test-hakios-ios16-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r14Entry"
}
if ($Task -like 'hakios-r15-*') {
    $r15Entry = switch ($Task) {
        'hakios-r15-prepare' { 'prepare-hakios-ios16-r15.py' }
        'hakios-r15-checks' { 'test-hakios-ios16-r15.py' }
        'hakios-r15-review' { 'review-hakios-ios16.py' }
        'hakios-r15-regressions' { 'run-hakios-ios16-r15-regressions.py' }
        'hakios-r15-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r15-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r15-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r15-package' { 'package-hakios-ios16.py' }
        'hakios-r15-package-checks' { 'test-hakios-ios16-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r15Entry"
}
if ($Task -like 'hakios-r16-*') {
    $r16Entry = switch ($Task) {
        'hakios-r16-prepare' { 'prepare-hakios-ios16-r16.py' }
        'hakios-r16-checks' { 'test-hakios-ios16-r16.py' }
        'hakios-r16-review' { 'review-hakios-ios16.py' }
        'hakios-r16-regressions' { 'run-hakios-ios16-r16-regressions.py' }
        'hakios-r16-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r16-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r16-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r16-package' { 'package-hakios-ios16.py' }
        'hakios-r16-package-checks' { 'test-hakios-ios16-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r16Entry"
}
if ($Task -like 'hakios-r17-*') {
    $r17Entry = switch ($Task) {
        'hakios-r17-prepare' { 'prepare-hakios-ios16-r17.py' }
        'hakios-r17-checks' { 'test-hakios-ios16-r17.py' }
        'hakios-r17-review' { 'review-hakios-ios16.py' }
        'hakios-r17-regressions' { 'run-hakios-ios16-r17-regressions.py' }
        'hakios-r17-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r17-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r17-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r17-package' { 'package-hakios-ios16.py' }
        'hakios-r17-package-checks' { 'test-hakios-ios16-r17-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r17Entry"
}
if ($Task -like 'hakios-r18-*') {
    $r18Entry = switch ($Task) {
        'hakios-r18-prepare' { 'prepare-hakios-ios16-r18.py' }
        'hakios-r18-checks' { 'test-hakios-ios16-r18.py' }
        'hakios-r18-review' { 'review-hakios-ios16.py' }
        'hakios-r18-regressions' { 'run-hakios-ios16-r18-regressions.py' }
        'hakios-r18-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r18-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r18-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r18-package' { 'package-hakios-ios16.py' }
        'hakios-r18-package-checks' { 'test-hakios-ios16-r18-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r18Entry"
}
if ($Task -like 'hakios-r19-*') {
    $r19Entry = switch ($Task) {
        'hakios-r19-prepare' { 'prepare-hakios-ios16-r19.py' }
        'hakios-r19-checks' { 'test-hakios-ios16-r19.py' }
        'hakios-r19-review' { 'review-hakios-ios16.py' }
        'hakios-r19-regressions' { 'run-hakios-ios16-r19-regressions.py' }
        'hakios-r19-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r19-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r19-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r19-package' { 'package-hakios-ios16.py' }
        'hakios-r19-package-checks' { 'test-hakios-ios16-r19-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r19Entry"
}
if ($Task -like 'hakios-r12-*') {
    $r12Entry = switch ($Task) {
        'hakios-r12-prepare' { 'prepare-hakios-ios16-r12.py' }
        'hakios-r12-checks' { 'test-hakios-ios16-r12.py' }
        'hakios-r12-review' { 'review-hakios-ios16.py' }
        'hakios-r12-regressions' { 'run-hakios-ios16-r12-regressions.py' }
        'hakios-r12-userspace' { 'build-hakios-ios16-userspace.py' }
        'hakios-r12-build-check' { 'check-hakios-ios16-build.py' }
        'hakios-r12-freeze' { 'freeze-hakios-ios16-boot-userspace.py' }
        'hakios-r12-package' { 'package-hakios-ios16.py' }
        'hakios-r12-package-checks' { 'test-hakios-ios16-package.py' }
    }
    $entryScript = "$linuxRoot/scripts/$r12Entry"
}
# A fixed unit name refuses a second simultaneous task, including dependency setup.
$arguments = @('-d', 'Ubuntu', '-u', 'root', '--exec', 'systemd-run',
    '--unit=ios-mix1-hakios', '--wait', '--pipe', '--collect',
    '-p', "MemoryMax=$($resources.task_memory_gib)G", '-p', "MemoryHigh=$($resources.task_memory_high_gib)G", '-p', "MemorySwapMax=$($resources.task_swap_gib)G",
    '-p', "CPUQuota=$($resources.task_cpu_quota_percent)%", '-p', "TasksMax=$($resources.task_max_processes)", '-p', 'User=jia',
    '-p', "WorkingDirectory=$linuxRoot", "--setenv=JOBS=$($resources.build_jobs)", "--setenv=MAKEFLAGS=-j$($resources.build_jobs)", "--setenv=MAKEJOBS=--jobs=$($resources.build_jobs)",
    $python, $entryScript)
if ($Task -eq 'hakios-r9-userspace') { $arguments += '--phase4' }
if ($Task -eq 'hakios-r9-build-check') { $arguments += '--phase4' }
if ($Task -in @('hakios-r9-freeze', 'hakios-r9-package')) { $arguments += @('--revision', '9') }
if ($Task -in @('hakios-r10-userspace', 'hakios-r10-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r10-freeze', 'hakios-r10-package')) { $arguments += @('--revision', '10') }
if ($Task -in @('hakios-r11-userspace', 'hakios-r11-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r11-freeze', 'hakios-r11-package')) { $arguments += @('--revision', '11') }
if ($Task -in @('hakios-r12-userspace', 'hakios-r12-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r12-freeze', 'hakios-r12-package')) { $arguments += @('--revision', '12') }
if ($Task -notlike 'hakios-r19-*' -and $Task -notlike 'hakios-r18-*' -and $Task -notlike 'hakios-r17-*' -and $Task -notlike 'hakios-r16-*' -and $Task -notlike 'hakios-r15-*' -and $Task -notlike 'hakios-r14-*' -and $Task -notlike 'hakios-r13-*' -and $Task -notlike 'hakios-r12-*' -and $Task -notlike 'hakios-r11-*' -and $Task -notlike 'hakios-r10-*' -and $Task -notin @('hakios-deps', 'hakios-support', 'hakios-build-check', 'hakios-storage', 'hakios-dynamic', 'hakios-package', 'hakios-version-audit', 'hakios-r9-prepare', 'hakios-r9-checks', 'hakios-r9-review', 'hakios-r9-return-checks', 'hakios-r9-static-checks', 'hakios-r9-regressions', 'hakios-r9-userspace', 'hakios-r9-build-check', 'hakios-r9-freeze', 'hakios-r9-package', 'hakios-r9-package-checks')) { $arguments += $command }
if ($Task -in @('hakios-r13-userspace', 'hakios-r13-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r13-freeze', 'hakios-r13-package')) { $arguments += @('--revision', '13') }
if ($Task -eq 'hakios-r13-package') { $arguments += @('--cpus', "$XnuCpus") }
if ($Task -in @('hakios-r14-userspace', 'hakios-r14-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r14-freeze', 'hakios-r14-package')) { $arguments += @('--revision', '14') }
if ($Task -eq 'hakios-r14-package') { $arguments += @('--cpus', "$XnuCpus") }
if ($Task -in @('hakios-r15-userspace', 'hakios-r15-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r15-freeze', 'hakios-r15-package')) { $arguments += @('--revision', '15') }
if ($Task -in @('hakios-r16-userspace', 'hakios-r16-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r16-freeze', 'hakios-r16-package')) { $arguments += @('--revision', '16') }
if ($Task -in @('hakios-r17-userspace', 'hakios-r17-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r17-freeze', 'hakios-r17-package')) { $arguments += @('--revision', '17') }
if ($Task -in @('hakios-r18-userspace', 'hakios-r18-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r18-freeze', 'hakios-r18-package')) { $arguments += @('--revision', '18') }
if ($Task -in @('hakios-r19-userspace', 'hakios-r19-build-check')) { $arguments += '--phase4' }
if ($Task -in @('hakios-r19-freeze', 'hakios-r19-package')) { $arguments += @('--revision', '19') }
& wsl.exe @arguments
exit $LASTEXITCODE
