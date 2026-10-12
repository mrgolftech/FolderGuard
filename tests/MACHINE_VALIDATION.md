# FolderGuard v0.0.1 · Windows 实体机验收指南

## 自动化覆盖与限制

- GitHub Actions 在 Windows Server 2022 托管虚拟机（x64 + WoW64 x86）中编译和执行本项目，并对 **真实 NTFS 临时目录** 做权限与属性写入和回读，不使用模拟 ACL。
- `FolderGuardSmoke.exe` 执行四项独立开关及组合测试，包括读取 DACL、对目标目录安全描述符调用 `AccessCheck(DELETE)`、保护后再次验证数据存在以及加锁和解锁恢复。
- `tests/GuiInteraction.ps1` 驱动原生 Win32 复选框，通过系统 `Get-Acl`、`FileAttributes` 验证落盘结果；用于可自动运行桌面 Session 的 Windows 环境。
- **Windows 7 SP1 32 位实体电脑尚未自动验证**。GitHub Hosted Runner 不是实体电脑，也不运行 Windows 7。不要把 Windows Server 2022 结果表述成 Windows 7 真机验证。
- 目标目录拒绝 DELETE 不等于阻止父目录授权的 `DELETE_CHILD`，因此 **绝不在真实用户数据上直接试删**，也不声明“无法被删除”。

## Windows 7 SP1 32 位实体机步骤

1. 从成功的 GitHub Actions 工作流下载 `FolderGuard-v0.0.1-Win32` 及 `FolderGuard-v0.0.1-Win32-NTFS-Validation`。
2. 解压 `FolderGuard.exe`、`FolderGuardSmoke.exe` 到独立目录。此验证会修改测试目录 ACL，需以管理员身份启动命令提示符。
3. 确认测试盘符为 NTFS，在管理员 CMD 中运行：
   ```cmd
   fsutil fsinfo volumeinfo C:
   FolderGuardSmoke.exe
   ```
   所有测试使用系统 TEMP 下独立临时目录，确认输出 `RESULT pass=... fail=0 skip=0`。
4. ACL 加锁会替换顶层 DACL，解锁会重置原有自定义 ACE；仅对**临时空目录**测试，不得直接在重要目录上执行。\n5. 启动 `FolderGuard.exe`，在空的手工测试目录上操作。依次独立开启/关闭：ACL 访问保护、禁止删除、系统属性 +S、隐藏属性 +H。每次点击“应用设置”后刷新，核对四项指示与对应原生复选框是否和实际状态一致。
5. 使用管理员 CMD 对测试目录做只读核验（不要删除）：
   ```cmd
   icacls "C:\\FolderGuardTest"
   attrib "C:\\FolderGuardTest"
   ```
   ACL 保护时应显示 DACL 继承受保护，且显式允许 ACE 仅保留 Administrators/SYSTEM；禁止删除时检查目标目录独立的 Authenticated Users 拒绝 ACE；+S/+H 对应 `S` 和 `H`。
6. 建议在 Windows 7 32 位 100%、125% DPI 及 1024×768 分辨率下检查文字和控件有无重叠。实机仍需人工确认 UAC 提示、文件夹选择器、中文输入法以及 4 个开关的交互。
7. 每轮结束时恢复测试目录状态，确保四个开关全部为关闭。若验证失败，先保留路径与日志再用 `icacls` 确认权限；不要对系统目录执行任何测试。

## 验收记录

| 项目 | GitHub Windows VM | Windows 7 SP1 32 位实体机 |
| --- | --- | --- |
| x86 EXE 编译与启动 | CI 自动测试 | 待测试 |
| 四项独立开关+组合状态 | CI 真实 NTFS 自动测试 | 待测试 |
| DELETE 目标 DACL 访问检查 | `AccessCheck` 自动测试 | 待测试 |
| 100%/125% DPI，中文输入法、UAC | 部分 GUI 截图覆盖 | 待测试 |
| 上层目录 DELETE_CHILD 绕过风险 | 已列为安全边界，不承诺防删除 | 仅在空测试目录评价 |

成功与失败日志请粘贴到 GitHub Issue，并附上系统版本、位数、DPI、盘符文件系统类型。
