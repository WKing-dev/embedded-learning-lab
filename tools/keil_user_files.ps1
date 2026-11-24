# ============================================================================
# keil_user_files.ps1
#   把 user 层源码与头文件路径写回 Keil 工程（MDK-ARM）
#
# 背景：CubeMX 每次“生成代码”都会重写 EnDat.uvprojx，
#       手加的包含路径和分组会丢失，而 USER CODE 区的内容会保留。
#       本脚本可重复执行：内容已正确时不做任何改动。
#
# 用法：powershell -ExecutionPolicy Bypass -File "tools\keil_user_files.ps1"
# ============================================================================

$ErrorActionPreference = 'Stop'

$projRoot = Split-Path -Parent $PSScriptRoot
$uvprojx  = Join-Path $projRoot 'MDK-ARM\EnDat.uvprojx'

if (-not (Test-Path $uvprojx)) {
    Write-Error "找不到工程文件：$uvprojx"
}

# user 层包含路径（模块内的 .h 与 .c 同级，需要逐个加入）
$userIncludes = @(
    '../user',
    '../user/application',
    '../user/bsp/bsp_endat',
    '../user/modules/Lic411'
)

# CubeMX 生成的固有包含路径
$coreIncludes = @(
    '../Core/Inc',
    '../Drivers/STM32F4xx_HAL_Driver/Inc',
    '../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy',
    '../Drivers/CMSIS/Device/ST/STM32F4xx/Include',
    '../Drivers/CMSIS/Include'
)

# user 层分组：组名 → 文件名 → 相对路径
$userGroups = @(
    @{ Name = 'user/application';    File = 'app_encoder.c'; Path = '../user/application/app_encoder.c' },
    @{ Name = 'user/bsp/bsp_endat';  File = 'bsp_endat.c';  Path = '../user/bsp/bsp_endat/bsp_endat.c' },
    @{ Name = 'user/modules/Lic411'; File = 'lic411.c';     Path = '../user/modules/Lic411/lic411.c' }
)

# 需要清理的旧文件条目
$staleFiles = @('lic411_dma_test.c', 'endat_transport.c', 'lic411_param.c')

$nl = "`n"
$text = [System.IO.File]::ReadAllText($uvprojx, [System.Text.Encoding]::UTF8)
$changed = $false

# ---- 1. 包含路径：整体对齐到期望值 ----
$desiredPath = (($userIncludes + $coreIncludes) -join ';')
$mPath = [regex]::Match($text, '<IncludePath>([^<]+)</IncludePath>')
if (-not $mPath.Success) {
    Write-Error '工程结构异常：找不到 IncludePath'
}
if ($mPath.Groups[1].Value -ne $desiredPath) {
    $text = $text.Replace('<IncludePath>' + $mPath.Groups[1].Value + '</IncludePath>',
                          '<IncludePath>' + $desiredPath + '</IncludePath>')
    $changed = $true
    Write-Host '[+] 更新包含路径'
} else {
    Write-Host '[=] 包含路径已正确'
}

# ---- 2. 清理旧文件条目 ----
foreach ($stale in $staleFiles) {
    $pattern = '(?s)\s*<File>\s*<FileName>' + [regex]::Escape($stale) + '</FileName>.*?</File>'
    if ($text -match $pattern) {
        $text = [regex]::Replace($text, $pattern, '')
        $changed = $true
        Write-Host "[+] 移除旧条目 $stale"
    }
}

# ---- 3. 判断分组是否已正确 ----
$needRebuild = $false
foreach ($group in $userGroups) {
    $hasName = $text.Contains('<GroupName>' + $group.Name + '</GroupName>')
    $hasFile = $text.Contains('<FilePath>' + $group.Path + '</FilePath>')
    if (-not ($hasName -and $hasFile)) {
        $needRebuild = $true
    }
}

if (-not $needRebuild) {
    Write-Host '[=] 分组已正确'
} else {
    # 先清掉所有旧的 user/* 分组，再按当前目录结构重建
    $oldGroups = [regex]::Matches($text, '(?s)\s*<Group>\s*<GroupName>user/[^<]*</GroupName>.*?</Group>')
    foreach ($m in $oldGroups) {
        $text = $text.Replace($m.Value, '')
        $changed = $true
    }

    $anchor = '        <Group>' + $nl + '          <GroupName>Drivers/STM32F4xx_HAL_Driver</GroupName>'
    if (-not $text.Contains($anchor)) {
        Write-Error '工程结构异常：找不到 Drivers 分组锚点'
    }

    foreach ($group in $userGroups) {
        $block  = '        <Group>' + $nl
        $block += '          <GroupName>' + $group.Name + '</GroupName>' + $nl
        $block += '          <Files>' + $nl
        $block += '            <File>' + $nl
        $block += '              <FileName>' + $group.File + '</FileName>' + $nl
        $block += '              <FileType>1</FileType>' + $nl
        $block += '              <FilePath>' + $group.Path + '</FilePath>' + $nl
        $block += '            </File>' + $nl
        $block += '          </Files>' + $nl
        $block += '        </Group>' + $nl

        $text = $text.Replace($anchor, $block + $anchor)
        Write-Host "[+] 新增分组 $($group.Name)"
    }
    $changed = $true
}

# ---- 4. 写回并校验 ----
if ($changed) {
    [System.IO.File]::WriteAllText($uvprojx, $text, (New-Object System.Text.UTF8Encoding($false)))
}

try {
    $doc = [xml](Get-Content $uvprojx -Raw)
    $count = $doc.SelectNodes('//File').Count
    Write-Host "OK: 工程已就绪，共 $count 个源文件"
} catch {
    Write-Error "工程文件 XML 校验失败：$($_.Exception.Message)"
}

# ---- 5. 自检：源文件必须真实存在 ----
$missing = 0
foreach ($group in $userGroups) {
    $full = Join-Path (Join-Path $projRoot 'MDK-ARM') $group.Path
    if (-not (Test-Path $full)) {
        Write-Host "[!] 文件不存在：$($group.Path)"
        $missing++
    }
}
if ($missing -eq 0) { Write-Host 'OK: 所有 user 源文件均存在' }
