# FolderGuard v0.0.1

首次统一发行图形界面和命令行版本的 FolderGuard。

## 下载选择

| 文件 | 类型 | 平台及启动方式 |
| --- | --- | --- |
| `FolderGuard-v0.0.1-GUI-Win32.zip` | GUI 版（32 位） | 解压后运行 `FolderGuard.exe`；目标兼容 Windows 7 SP1 / Windows 10 |
| `FolderGuard-v0.0.1-GUI-x64.zip` | GUI 版（64 位） | 解压后运行 `FolderGuard.exe` |
| `FolderGuard-v0.0.1-CLI-PowerShell.zip` | 命令行版 | 解压后双击 `Start-FolderGuard.cmd`，或运行 `FolderGuard.ps1`；需 Windows PowerShell 5.1 |
| `SHA256SUMS.txt` | 完整性校验 | 三个 ZIP 的 SHA-256 哈希 |

**命令行版是 PowerShell 脚本，不是 CLI EXE。** CLI 继续使用此前 v2.3 菜单逻辑，独立于新 GUI 的四项设置语义。

## GUI 亮点

- 紧凑浅色单页布局，中文字体与圆角按钮优化。
- 浏览选择或输入路径回车载入，启动时默认路径为空。
- 四项设置单行显示，分别控制 ACL 访问保护、禁止删除拒绝 ACE、系统属性 +S、隐藏属性 +H。
- 自动从真实 NTFS 读取状态；刷新、确认、结果和耗时反馈。
- 原生 C++ / Win32 / Direct2D，x86/x64，静态 MSVC 运行库。

## 验证与重要限制

- GUI 的 x86/x64 构建、NTFS 临时目录四选项检查和 GUI 交互自动化测试在 GitHub 托管 Windows Server 2022 虚拟机上执行，**并非 Windows 7 32 位物理机验证**。
- ACL 限制仅作用于顶层目录；解锁可能重置自定义 ACL，建议先备份。
- 顶层目录拒绝 DELETE 不保证防止父目录授予 DELETE_CHILD 的实际删除，不应拿重要数据目录做删除实验。
- +S 和 +H 不提供数据加密或访问保护。

参考： [GUI 使用说明](https://github.com/mrgolftech/FolderGuard/blob/main/README-GUI.md) · [CLI 使用说明](https://github.com/mrgolftech/FolderGuard/blob/main/docs/USAGE.md) · [Windows 实测指南](https://github.com/mrgolftech/FolderGuard/blob/main/tests/MACHINE_VALIDATION.md)
