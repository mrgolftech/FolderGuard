# FolderGuard v0.0.1 · 命令行版（PowerShell CLI）

此压缩包是 FolderGuard **v0.0.1 产品发布中的命令行版本**。其核心脚本沿用历史 PowerShell v2.3 菜单实现，**不是独立编译的原生 EXE**，操作语义与 GUI v0.0.1 不完全一致。

## 运行环境

- Windows 10 / 11；Windows 7 SP1 需**自行安装 Windows PowerShell 5.1**后方可尝试运行，尚未完成 Win7 32 位真机兼容性验收。
- 支持 NTFS 的本地磁盘、管理员权限；Windows PowerShell 5.1（非 PowerShell 7 必需）。
- 同目录下保留 `FolderGuard.ps1` 和 `Start-FolderGuard.cmd` 两个文件。

## 使用

直接双击 `Start-FolderGuard.cmd`，确认 Windows UAC 提示，即进入中文交互菜单。

也可在已提升权限的 CMD 窗口执行：

```cmd
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\FolderGuard.ps1"
```

菜单包含选定目录、快速或完整 ACL 加锁/解锁、系统/隐藏属性切换、ACL 状态查询等功能。**CLI 历史加锁动作会自动设置 +S 与禁止删除 ACE，与 GUI 四项独立设置不同。**

## 注意事项

- 本程序不会加密文件，禁止删除 ACE 不保证阻止父目录 `DELETE_CHILD` 授权的删除。
- 解锁或完整操作可能重置已有自定义 ACL，应事先备份 ACL。
- 请只对专用临时测试目录验证，勿直接操作系统目录或重要数据。
- 详细说明见 [仓库 docs/USAGE.md](https://github.com/mrgolftech/FolderGuard/blob/main/docs/USAGE.md)。
- 本压缩包无需额外编译，但需要系统具备 Windows PowerShell 5.1。
