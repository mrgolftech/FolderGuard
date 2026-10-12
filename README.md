# FolderGuard

## v0.0.1 发布版本

- **[GUI 版](README-GUI.md)**：C++17 + Win32 + Direct2D，绿色版 `FolderGuard.exe`，分别提供 Windows x86 和 x64 构建。GUI 使用 ACL 权限保护、禁止删除、系统属性 +S、隐藏属性 +H 四项独立开关。
- **[命令行版](README-CLI.md)**：Windows PowerShell 5.1 中文交互菜单，包含 `FolderGuard.ps1` 与 `Start-FolderGuard.cmd`，无需编译。命令行脚本沿用历史 v2.3 操作语义，**不等同于 GUI 四项独立开关**。
- **[查看 v0.0.1 GitHub Release 下载](https://github.com/mrgolftech/FolderGuard/releases/tag/v0.0.1)**：提供 GUI x86、GUI x64、CLI PowerShell 三个 ZIP 包及 SHA-256 校验文件。
- **[版本发布说明](RELEASE_NOTES-v0.0.1.md)** · [GUI 设计说明](docs/GUI_DESIGN.md) · [Windows 验收指南](tests/MACHINE_VALIDATION.md)

> 目标 Windows 7 SP1 / Windows 10。GitHub Actions 已在 Windows Server 2022 虚拟机上验证原生 GUI 的 x86/x64 构建及真实 NTFS 状态，**不能视为 Windows 7 32 位实体机验收**。ACL 不是加密；禁止删除不能保证抵挡父目录 `DELETE_CHILD` 权限；请先在空测试目录验证。

---

## 命令行版（PowerShell 5.1）详细说明

适用 Windows 10/11；Windows 7 SP1 需要先安装 Windows PowerShell 5.1，且兼容性仍待验证。

## 功能

- 交互式中文菜单，支持自定义目标文件夹路径，自动创建不存在的目录。
- **快速加锁/解锁**：仅修改目标文件夹顶层 ACL，不主动递归处理子文件。
- **完整加锁/解锁**：批量调整目标文件夹及内部文件的 ACL；大量文件可能耗时较长。
- 加锁时自动添加 `+S`（系统）属性，`+H`（隐藏）属性单独控制。
- 根目录防删除：尝试对 `Authenticated Users` 显式拒绝 `DELETE` 与 `DELETE_CHILD`，不继承给子项。
- 查看当前目录的属性、根 ACL 和防删除规则状态。
- CMD 启动器弹出 UAC，请求管理员权限后运行 PowerShell。

## 快速开始

1. 下载同一目录下的 `FolderGuard.ps1` 和 `Start-FolderGuard.cmd`。
2. 双击 `Start-FolderGuard.cmd`，在 UAC 提示中选择“是”。
3. 选 **1**，输入目标文件夹（例如 `E:\TestVault`；也支持自建 `E:\$RECYCLE.BIN$`）。
4. 选 **4** 设置 `+S +H`；选 **8** 快速加锁（或选 **2** 完整加锁）。
5. 需要存取文件时选 **9** 快速解锁（或选 **3** 完整解锁）；必要时选 **5** 取消 `+S +H`。
6. 选 **6** 查看 ACL 和属性状态。

**不要对 Windows 自带的 `E:\$RECYCLE.BIN`、`System Volume Information`、系统盘根目录或其他系统目录执行加锁。**

## 菜单

| 选项 | 功能 |
| --- | --- |
| 1 | 选择 / 创建目录 |
| 2 | 完整加锁：递归保护，附加顶层防删除、`+S` |
| 3 | 完整解锁：启用继承并递归重置 ACL |
| 4 | 设置 `+S +H` |
| 5 | 取消 `-S -H` |
| 6 | 查看权限与属性 |
| 7 | 使用资源管理器打开 |
| 8 | 快速加锁：仅顶层 ACL、防删除、`+S` |
| 9 | 快速解锁：仅重置顶层 ACL |
| 0 | 退出 |

## 权限与数据安全说明

- **ACL 不是加密**；具备管理权限的用户通常能够恢复访问，离线读取也不受隐藏属性保护。
- **快速模式并不严格保护子文件**：若用户知道子文件完整路径且子文件 ACL 允许读取，仍可能访问。
- **防删除不保证成功**：删除还可能通过父目录的 `DELETE_CHILD` 授权完成。当前版本不会修改父目录 ACL，特别不会修改盘符根目录 ACL。
- **完整加锁/解锁会覆盖自定义 ACL**：解锁不是恢复原 ACL 快照，而是通过 `icacls /reset` 恢复继承权限。加锁前应备份数据与 ACL。
- **不保证加锁后能阻止右键删除**：请在空测试目录验证；不应拿存放重要资料的目录做删除测试。
- 添加 `+S` 或 `+H` 仅影响显示行为，无法限制数据读取。
- 对于保密数据，请使用 BitLocker、EFS 或其他经过验证的加密方案。

## 文档

- [使用说明](docs/USAGE.md)：安装、菜单、操作流程、常见问题、手工恢复。
- [设计说明](docs/DESIGN.md)：架构、ACL 权限模型、快速/完整模式、风险边界。
- [测试清单](docs/TESTING.md)：建议在真实 Windows 10/11 上执行的验收用例。

## 开发与验证

当前版本以 Windows PowerShell 5.1 为目标，必须在 **NTFS 分区**测试。项目脚本目前完成基础代码检查，但**尚未在当前交付环境对 Windows 10/11 的 ACL 行为进行自动化或实机验证**。

项目中未附带对目录执行破坏性删除的自动化测试，请参考测试清单逐项验证。

## 版本

初始开源版本：**v2.3**（含顶层防删除拒绝规则）。

> 本仓库是个人文件夹访问控制工具，**与同名第三方商业软件无关**。
