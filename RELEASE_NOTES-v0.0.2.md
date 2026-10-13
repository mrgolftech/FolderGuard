# FolderGuard v0.0.2 · ACL 核验与中文路径兼容性增强

此次版本修复目录 ACL “继承已关闭却显示已保护”的误判，并强化设置后的逐项回读验证。提供 **GUI Win32、GUI x64、CLI PowerShell** 三个独立压缩包。

## 下载选择

| 文件 | 类型 | 使用方法 |
| --- | --- | --- |
| `FolderGuard-v0.0.2-GUI-Win32.zip` | 32 位原生 GUI | 解压运行 `FolderGuard.exe`，目标兼容 Windows 7 SP1 32 位及较新的 Windows |
| `FolderGuard-v0.0.2-GUI-x64.zip` | 64 位原生 GUI | 解压运行 `FolderGuard.exe` |
| `FolderGuard-v0.0.2-CLI-PowerShell.zip` | PowerShell 命令行版 | 解压并双击 `Start-FolderGuard.cmd`；需要 Windows PowerShell 5.1 |
| `SHA256SUMS.txt` | 完整性验证 | 包含以上三个 ZIP 的 SHA-256 |

**注意：CLI 仍采用旧版 PowerShell v2.3 菜单及操作语义，只随产品更新为 v0.0.2；GUI 的四个独立选项和严格回读验证不自动适用于该脚本。**

## 相比 v0.0.1 的变化

- **中文及混合 Unicode 路径测试**：在实际 NTFS 临时目录上验证中文、日文与空格混合名称的路径；覆盖 ACL 写入、解锁、拒绝删除、+S/+H、递归操作。
- **防止 ACL 虚假成功**：不再把仅关闭 DACL 继承视为完整 ACL 保护。只有顶层 DACL 满足 Administrators/SYSTEM 允许规则、无额外不符合预期的 ACE，才认为 FolderGuard ACL 已启用。
- **逐项写入后验证**：ACL、禁止删除、系统属性、隐藏属性均在修改后重新读取，并检查其他三个选项是否被意外改变。
- **全局二次核验**：GUI 汇总操作结束后再次读取状态；窗口接收完成消息时也复核实际 NTFS 状态是否一致。未达到预期不会显示整体成功。
- **改善诊断信息**：对关闭继承但 ACL 不符合保护策略的目录明确告警；结果窗口展示实际 ACL 状态与继承标志。
- **版本一致性**：同步 GUI 界面、EXE 资源版本、CMake、文档及发行工件版本号为 v0.0.2。

## 验证与风险边界

- 发布流水线在 GitHub 托管的 Windows Server 2022 虚拟机上运行 Win32/x64 编译、真实 NTFS 安全测试与 GUI 自动化交互测试，另对 CLI 进行 Windows PowerShell 5.1 语法校验。**不能宣称已经通过 Windows 7 32 位真机兼容性测试**。
- GUI ACL 只修改并校验所选**顶层目录**；已知完整子文件路径的访问能力不在顶层状态验证范围内。
- “禁止删除”仅核实目标 DACL 的拒绝 ACE，**父目录授予的 DELETE_CHILD 权限仍可能允许删除**，不可据此承诺完全防删除。
- `icacls /reset` 会恢复父目录继承而**不是精确恢复原自定义 ACL**。建议在修改重要目录前备份原始 ACL；不要对真实资料进行试删。
- +S 与 +H 只是属性，不提供加密保护。

详情参阅 [GUI 使用说明](https://github.com/mrgolftech/FolderGuard/blob/main/README-GUI.md)、[CLI 使用说明](https://github.com/mrgolftech/FolderGuard/blob/main/README-CLI.md) 及 [Windows 验收指南](https://github.com/mrgolftech/FolderGuard/blob/main/tests/MACHINE_VALIDATION.md)。
