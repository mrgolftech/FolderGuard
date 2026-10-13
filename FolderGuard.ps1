#Requires -Version 5.1
# FolderGuard v0.0.2 (legacy CLI v2.3 semantics) - Windows PowerShell 5.1
# Scope: user-selected NTFS data directory, never the actual Windows Recycle Bin.
$ErrorActionPreference = 'Stop'
$script:Target = $null
$adminSid = New-Object Security.Principal.SecurityIdentifier 'S-1-5-32-544'
$systemSid = New-Object Security.Principal.SecurityIdentifier 'S-1-5-18'
# Authenticated Users: explicit deny DELETE and DELETE_CHILD on the selected directory only.
# SYSTEM retains access. Denial does NOT propagate to child items.
$authenticatedUsersSid = New-Object Security.Principal.SecurityIdentifier 'S-1-5-11'

function Is-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($id)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Pause-Menu { [void](Read-Host '按回车返回菜单') }
function Assert-Target {
    if (-not $script:Target) { throw '请先选择文件夹（菜单 1）。' }
    if (-not (Test-Path -LiteralPath $script:Target -PathType Container)) { throw '目标文件夹不存在或当前账户无法访问。' }
    $d = Get-Item -LiteralPath $script:Target -Force
    if (($d.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw '不允许直接操作链接或联接点目录。' }
}
function Set-Target {
    $raw = (Read-Host '输入目录路径，例如 E:\$RECYCLE.BIN$').Trim().Trim('"')
    if ([string]::IsNullOrWhiteSpace($raw)) { throw '路径不能为空。' }
    if ($raw -notmatch '^[A-Za-z]:\\') { throw '仅支持本地磁盘的绝对路径。' }
    if ($raw -match '[<>"|?*]' -or $raw -match '[!%]') { throw '路径包含不支持的字符。' }
    $p = [IO.Path]::GetFullPath($raw).TrimEnd('\')
    $root = [IO.Path]::GetPathRoot($p).TrimEnd('\')
    if ($p -ieq $root) { throw '不能对磁盘根目录操作。' }
    if ($p -match '(?i)^[a-z]:\\(windows|program files(?: \(x86\))?|programdata|users|system volume information|\$recycle\.bin)(\\|$)') {
        throw '拒绝操作 Windows 系统目录。请使用自建数据目录。'
    }
    $walk = $p.Substring(0,3).TrimEnd('\')
    foreach ($part in $p.Substring(3).Split('\')) {
        $walk = $walk + '\' + $part
        if (Test-Path -LiteralPath $walk) {
            if (((Get-Item -LiteralPath $walk -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw '路径中包含符号链接或联接点，已拒绝。'
            }
        }
    }
    if (-not (Test-Path -LiteralPath $p)) { New-Item -ItemType Directory -Path $p | Out-Null }
    if (-not (Test-Path -LiteralPath $p -PathType Container)) { throw '指定路径不是文件夹。' }
    $script:Target = $p
    Write-Host "当前目录：$p" -ForegroundColor Green
}
function Test-ReparseChildren {
    Write-Host '正在检查子目录是否包含符号链接/联接点（大目录可能需要一些时间）...' -ForegroundColor Cyan
    $link = Get-ChildItem -LiteralPath $script:Target -Recurse -Force -ErrorAction Stop |
        Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 } |
        Select-Object -First 1
    if ($null -ne $link) { throw "目录中存在链接/联接点：$($link.FullName)。为防止误改其他路径 ACL，已中止。" }
}
function Add-SystemAttribute {
    # Add System only; do not alter the existing Hidden flag.
    $item = Get-Item -LiteralPath $script:Target -Force
    $item.Attributes = $item.Attributes -bor [IO.FileAttributes]::System
}
function Test-AntiDeleteAcl($acl) {
    $needed = [int]([Security.AccessControl.FileSystemRights]::Delete -bor
                    [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles)
    foreach ($entry in $acl.Access) {
        $sidText = $entry.IdentityReference.Value
        try {
            $sidText = $entry.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value
        } catch { }
        if (($sidText -eq $authenticatedUsersSid.Value) -and
            ($entry.AccessControlType -eq [Security.AccessControl.AccessControlType]::Deny) -and
            (([int]$entry.FileSystemRights -band $needed) -eq $needed)) {
            return $true
        }
    }
    return $false
}
function Show-QuickOperationResult {
    # Quick mode changes one folder only. Print precisely the folder and the final ACL.
    $item = Get-Item -LiteralPath $script:Target -Force
    $acl = Get-Acl -LiteralPath $script:Target
    $hasS = (($item.Attributes -band [IO.FileAttributes]::System) -ne 0)
    $hasH = (($item.Attributes -band [IO.FileAttributes]::Hidden) -ne 0)
    Write-Host "处理对象：$script:Target" -ForegroundColor Cyan
    Write-Host '处理范围：仅顶层目录（1 个目录，未递归处理子文件/子目录）' -ForegroundColor Cyan
    Write-Host "属性状态：+S=$hasS，+H=$hasH"
    Write-Host "ACL 继承被禁用：$($acl.AreAccessRulesProtected)"
    $hasAntiDelete = Test-AntiDeleteAcl $acl
    Write-Host "根目录防删除拒绝规则：$(if ($hasAntiDelete) {'已设置'} else {'未设置'})"
    Write-Host '注意：父目录若仍授予 DELETE_CHILD，可能绕过根目录 DELETE 拒绝。' -ForegroundColor Yellow
    Write-Host '当前顶层目录 ACL：' -ForegroundColor Cyan
    & icacls.exe $script:Target
    if ($LASTEXITCODE -ne 0) {
        Write-Warning '查看权限时出错；加锁/解锁操作已执行，请在管理员窗口中检查该路径。'
    }
}
function Set-RootLockedAcl([bool]$inheritToChildren) {
    # Replaces only the selected folder's DACL; no explicit child enumeration here.
    $acl = Get-Acl -LiteralPath $script:Target
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($rule in @($acl.Access)) { [void]$acl.RemoveAccessRuleSpecific($rule) }
    $inherit = [Security.AccessControl.InheritanceFlags]::None
    if ($inheritToChildren) {
        $inherit = [Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit
    }
    foreach ($sid in @($adminSid, $systemSid)) {
        $rule = [System.Security.AccessControl.FileSystemAccessRule]::new(
            $sid,
            [System.Security.AccessControl.FileSystemRights]::FullControl,
            $inherit,
            [System.Security.AccessControl.PropagationFlags]::None,
            [System.Security.AccessControl.AccessControlType]::Allow
        )
        [void]$acl.AddAccessRule($rule)
    }
    # Anti-delete ACE is root-only: do not propagate to child files.
    # This blocks DELETE/DELETE_CHILD requested against this directory by
    # interactive authenticated users (including the current administrator).
    # However DELETE_CHILD granted on the *parent* can still allow deletion.
    $antiDeleteRights = [Security.AccessControl.FileSystemRights]::Delete -bor
                        [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles
    $denyDelete = [Security.AccessControl.FileSystemAccessRule]::new(
        $authenticatedUsersSid,
        $antiDeleteRights,
        [Security.AccessControl.InheritanceFlags]::None,
        [Security.AccessControl.PropagationFlags]::None,
        [Security.AccessControl.AccessControlType]::Deny
    )
    [void]$acl.AddAccessRule($denyDelete)
    Set-Acl -LiteralPath $script:Target -AclObject $acl
}
function Quick-Lock-Target {
    Assert-Target
    if (-not (Is-Admin)) { throw '加锁必须以管理员身份运行。' }
    Write-Host '【快速加锁】仅修改目标根目录 ACL，并增加防删除拒绝规则。' -ForegroundColor Yellow
    Write-Host '根目录仅允许 Administrators 和 SYSTEM；对 Authenticated Users 拒绝根目录 DELETE/DELETE_CHILD。' -ForegroundColor Yellow
    Write-Host '授权与拒绝均不继承给子文件；不递归处理现有文件。' -ForegroundColor Yellow
    Write-Host '警告：如果知道子文件完整路径，且子文件自身允许读取，普通账户仍可能访问其内容。' -ForegroundColor Yellow
    Write-Host '原根目录自定义 ACL 会被替换；快速解锁只重置根目录 ACL，不还原原有自定义权限。' -ForegroundColor Yellow
    Write-Host '重要：若父目录授予 DELETE_CHILD，依然有可能删除目标目录；本工具不修改磁盘根目录 ACL。' -ForegroundColor Yellow
    if ((Read-Host '输入 Y 确认快速加锁') -ine 'Y') { Write-Host '已取消。'; return }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    Set-RootLockedAcl $false
    Add-SystemAttribute
    $timer.Stop()
    Write-Host "快速加锁完成：根目录访问受限，并设置防删除拒绝规则与 +S（+H 不变）。耗时 $([math]::Round($timer.Elapsed.TotalSeconds, 1)) 秒。" -ForegroundColor Green
    Show-QuickOperationResult
}
function Quick-Unlock-Target {
    Assert-Target
    if (-not (Is-Admin)) { throw '解锁必须以管理员身份运行。' }
    $path = $script:Target
    Write-Host '【快速解锁】仅对根目录启用继承并重置根目录 ACL，不递归修改子项。' -ForegroundColor Yellow
    Write-Host '若此前执行过【完整加锁】，内部文件可能仍被限制，需要使用【完整解锁】。' -ForegroundColor Yellow
    Write-Host '将保留 +S 和 +H 属性；如需显示目录请另外选择菜单 5。' -ForegroundColor Yellow
    if ((Read-Host '输入 Y 确认快速解锁') -ine 'Y') { Write-Host '已取消。'; return }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & icacls.exe $path '/inheritance:e' '/q'
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "启用根目录继承失败（icacls 退出代码 $code）。" }
    & icacls.exe $path '/reset' '/q'
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "重置根目录 ACL 失败（icacls 退出代码 $code）。" }
    $timer.Stop()
    Write-Host "快速解锁完成：仅根目录 ACL 已恢复父级继承，+S/+H 未改变。耗时 $([math]::Round($timer.Elapsed.TotalSeconds, 1)) 秒。" -ForegroundColor Green
    Show-QuickOperationResult
}
function Lock-Target {
    Assert-Target
    if (-not (Is-Admin)) { throw '加锁必须使用管理员 PowerShell。' }
    Test-ReparseChildren
    $path = $script:Target
    Write-Host '完整加锁会覆盖目录及子项当前自定义 ACL，并只保留管理员与 SYSTEM 的访问权。' -ForegroundColor Yellow
    Write-Host '顶层目录将额外拒绝 Authenticated Users 的 DELETE/DELETE_CHILD 权限，且自动设置 +S。' -ForegroundColor Yellow
    Write-Host '注意：父目录若允许 DELETE_CHILD，可能仍能删除顶层目录；不会修改父目录 ACL。' -ForegroundColor Yellow
    Write-Host '本版本解锁采用恢复父级权限继承，不提供原 ACL 精确还原。' -ForegroundColor Yellow
    $answer = Read-Host '确认加锁？输入 Y 继续'
    if ($answer -ine 'Y') { Write-Host '已取消。'; return }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    Set-RootLockedAcl $true
    Add-SystemAttribute
    Write-Host '根目录已加锁，已设置防删除拒绝规则与 +S 属性（+H 不变）。' -ForegroundColor Green
    # Reset children ONLY; do not reset the just-locked root.
    # /q avoids sending a line for each of potentially 100k files to the terminal.
    Write-Host '正在批量设置子文件和子文件夹权限（安静模式，不逐项显示）...' -ForegroundColor Cyan
    $children = Get-ChildItem -LiteralPath $path -Force -ErrorAction Stop | Select-Object -First 1
    if ($null -ne $children) {
        $glob = Join-Path $path '*'
        & icacls.exe $glob '/reset' '/t' '/q'
        $code = $LASTEXITCODE
        if ($code -ne 0) { throw "对子项批量设置权限时出现错误（icacls 退出代码 $code）。根目录可能已经加锁，请检查失败条目后重试。" }
    }
    $timer.Stop()
    Write-Host "完整加锁完成，耗时 $([math]::Round($timer.Elapsed.TotalSeconds, 1)) 秒。" -ForegroundColor Green
}
function Unlock-Target {
    Assert-Target
    if (-not (Is-Admin)) { throw '解锁必须使用管理员 PowerShell。' }
    $path = $script:Target
    Write-Host "解锁目录：$path" -ForegroundColor Yellow
    Write-Host '此操作启用父目录权限继承，并递归清除子项的自定义 ACL。' -ForegroundColor Yellow
    Write-Host '原有自定义权限将丢失。对大量文件可能需要一段时间。' -ForegroundColor Yellow
    $answer = Read-Host '确认解锁？输入 Y 继续'
    if ($answer -ine 'Y') { Write-Host '已取消。'; return }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    Write-Host '正在启用根目录权限继承...' -ForegroundColor Cyan
    & icacls.exe $path '/inheritance:e' '/q'
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "启用权限继承失败（退出代码 $code），未继续重置。" }
    Write-Host '正在批量恢复继承权限（安静模式，不逐项显示）...' -ForegroundColor Cyan
    & icacls.exe $path '/reset' '/t' '/q'
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw "递归权限重置存在失败项（退出代码 $code），请检查输出。" }
    $timer.Stop()
    Write-Host "完整解锁完成（+S/+H 保持不变），耗时 $([math]::Round($timer.Elapsed.TotalSeconds, 1)) 秒。" -ForegroundColor Green
}
function Set-Hidden([bool]$on) {
    Assert-Target
    $item = Get-Item -LiteralPath $script:Target -Force
    $mask = [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System
    if ($on) { $item.Attributes = $item.Attributes -bor $mask }
    else { $item.Attributes = $item.Attributes -band (-bnot $mask) }
    if ($on) { Write-Host '成功设置 +S +H。' -ForegroundColor Green }
    else { Write-Host '成功取消 -S -H。' -ForegroundColor Green }
}
function Show-Status {
    Assert-Target
    $item = Get-Item -LiteralPath $script:Target -Force
    Write-Host "目录：$script:Target"
    Write-Host "属性：$($item.Attributes)"
    $isS = (($item.Attributes -band [IO.FileAttributes]::System) -ne 0)
    $isH = (($item.Attributes -band [IO.FileAttributes]::Hidden) -ne 0)
    Write-Host "System (+S)：$isS；Hidden (+H)：$isH"
    $acl = Get-Acl -LiteralPath $script:Target
    Write-Host "根目录继承：$(if ($acl.AreAccessRulesProtected) {'已禁用（可能加锁）'} else {'已启用'})"
    Write-Host "防删除拒绝规则：$(if (Test-AntiDeleteAcl $acl) {'已设置（仅作用于本目录）'} else {'未设置'})"
    Write-Host '提示：即使显示已设置，如果父目录具有 DELETE_CHILD，仍可能删除目标目录。' -ForegroundColor Yellow
    & icacls.exe $script:Target
    Write-Host '提示：当前只展示根目录 ACL；此状态不能证明所有子文件都受保护。' -ForegroundColor Yellow
}
while ($true) {
    Clear-Host
    Write-Host '========== FolderGuard v0.0.2 (CLI) ==========' -ForegroundColor Cyan
    Write-Host "当前目录：$(if ($script:Target) { $script:Target } else { '[尚未选择]' })"
    Write-Host '1. 选择 / 创建文件夹'
    Write-Host '2. 完整加锁：递归子文件 + 顶层防删除 +S（较慢）'
    Write-Host '3. 完整解锁：递归恢复权限（含删除拒绝规则）'
    Write-Host '4. 设置 +S +H 属性'
    Write-Host '5. 取消 -S -H 属性'
    Write-Host '6. 查看目录权限及属性'
    Write-Host '7. 在资源管理器中打开目录'
    Write-Host '8. 快速加锁：仅根目录 ACL + 防删除 +S（快速）'
    Write-Host '9. 快速解锁：仅恢复根目录 ACL（含删除拒绝规则）'
    Write-Host '0. 退出'
    Write-Host '======================================' -ForegroundColor Cyan
    $choice = ([string](Read-Host '请选择 0-9')).Trim()
    # Ignore empty Enter (including a stray Enter after returning to the menu).
    if ([string]::IsNullOrWhiteSpace($choice)) { continue }
    if ($choice -eq '0') { break }
    try {
        switch ($choice) {
            '1' { Set-Target }
            '2' { Lock-Target }
            '3' { Unlock-Target }
            '4' { Set-Hidden $true }
            '5' { Set-Hidden $false }
            '6' { Show-Status }
            '7' { Assert-Target; Start-Process explorer.exe -ArgumentList ('"' + $script:Target + '"') }
            '8' { Quick-Lock-Target }
            '9' { Quick-Unlock-Target }
            default { Write-Warning '无效选项。' }
        }
    } catch {
        Write-Host "操作失败：$($_.Exception.Message)" -ForegroundColor Red
    }
    Pause-Menu
}