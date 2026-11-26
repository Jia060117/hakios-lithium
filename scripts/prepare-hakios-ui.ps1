[CmdletBinding()]
param(
    [ValidateSet('prepare', 'abi', 'configure', 'refine', 'methods', 'summary', 'workspace', 'audit', 'check', 'deps', 'support', 'root', 'p0', 'kernel', 'package', 'freeze', 'tests', 'permissions-tests', 'identity-audit', 'identity-prepare', 'identity-tests', 'ready-verify')]
    [string]$Task = 'prepare'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resources = Get-Content -LiteralPath (Join-Path $projectRoot 'config/hakios-resources.json') -Raw | ConvertFrom-Json
$linuxRoot = (& wsl.exe -d Ubuntu -u jia --exec wslpath -u $projectRoot).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the HakiOS project path in Ubuntu' }
$entry = @{
    prepare = @('prepare-hakios-ios14-ui-integration.py'); abi = @('audit-hakios-ios14-ui-abi.py')
    configure = @('configure-hakios-ios14-ui.py'); workspace = @('hakios.py', 'prepare')
    refine = @('refine-hakios-ui-abi.py')
    methods = @('adapt-hakios-ios14-surface-methods.py')
    summary = @('refine-hakios-ui-summary.py')
    audit = @('hakios.py', 'audit')
    check = @('hakios.py', 'check'); deps = @('hakios-dependencies.py')
    support = @('build-hakios-support.py'); root = @('build-hakios-ios14-dynamic.py')
    p0 = @('close-hakios-p0.py'); kernel = @('hakios.py', 'build-kernel')
    package = @('package-hakios-lithium.py'); freeze = @('freeze-hakios-ui.py')
    tests = @('test-hakios-ui.py')
    'permissions-tests' = @('test-hakios-ramroot-permissions.py')
    'identity-audit' = @('audit-hakios-ios14-entitlements.py')
    'identity-prepare' = @('prepare-hakios-ios14-identity.py')
    'identity-tests' = @('test-hakios-ios14-identity.py')
    'ready-verify' = @('verify-hakios-ui-ready.py')
}[$Task]
# Same exclusive unit and approved resource configuration as the frozen launcher.
# Python checks live WSL memory/CPU/swap and task cgroup limits before preparing anything.
$arguments = @('-d', 'Ubuntu', '-u', 'root', '--exec', 'systemd-run',
    '--unit=ios-mix1-hakios', '--wait', '--pipe', '--collect',
    '-p', "MemoryMax=$($resources.task_memory_gib)G", '-p', "MemoryHigh=$($resources.task_memory_high_gib)G",
    '-p', "MemorySwapMax=$($resources.task_swap_gib)G", '-p', "CPUQuota=$($resources.task_cpu_quota_percent)%",
    '-p', "TasksMax=$($resources.task_max_processes)", '-p', 'User=jia', '-p', "WorkingDirectory=$linuxRoot",
    "--setenv=JOBS=$($resources.build_jobs)", "--setenv=MAKEFLAGS=-j$($resources.build_jobs)",
    "--setenv=MAKEJOBS=--jobs=$($resources.build_jobs)",
    '/usr/bin/python3', "$linuxRoot/scripts/$($entry[0])")
if ($entry.Count -gt 1) { $arguments += $entry[1..($entry.Count - 1)] }
& wsl.exe @arguments
exit $LASTEXITCODE
