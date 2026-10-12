# FolderGuard v3.0 GUI 测试记录（2026-10-12）

> **验证范围：Windows Server 2022 Runner**。并非 Windows 7/10 真机验收。

## 构建与测试

- [GitHub Actions 完整记录](https://github.com/mrgolftech/FolderGuard/actions/runs/38238669093)
- x64 Release：构建成功；12 项通过、0 失败、0 跳过。
- x86 Release：构建成功；12 项通过、0 失败、0 跳过。
- 测试运行于管理员令牌，使用独立临时 NTFS 测试目录。核心逻辑直接调用 Windows ACL API、icacls，不是 Mock。

测试覆盖：拒绝盘符根目录与系统回收站；创建测试目录；读取 NTFS 状态；设置/清除 +S +H；快速加锁/解锁；顶层 ACL 禁用继承和删除拒绝 ACE；完整加锁/解锁；确保已有测试子文件仍存在；清理临时目录。

## GUI 真实截图

在 x64 Runner 上启动 FolderGuard.exe，使用 tests/CaptureGui.ps1 捕获窗口：**PrintWindow=True，1044 × 788**。

截图位于 [Actions 运行记录](https://github.com/mrgolftech/FolderGuard/actions/runs/38238669093) 的 FolderGuard-v3-GUI-Screenshots 工件内（FolderGuard-PrintWindow.png 和 FolderGuard-Screen.png）。截图反映初始状态，不代表已逐个点击所有按钮。

## 与 PowerShell v2.3 对照

| 功能 | 脚本 v2.3 | GUI v3.0 | 验证情况 |
| --- | --- | --- | --- |
| 选择/创建目标 NTFS 目录 | 有 | 输入框、选择目录、按需创建 | 后端创建通过；GUI 能启动 |
| 快速加锁/解锁 | 有 | 有 | 后端实际通过 |
| 完整加锁/解锁 | 有 | 有 | 后端小样本通过 |
| 加锁自动 +S | 有 | 有 | 后端实际通过 |
| 设置/取消 +S +H | 同时切换 | 分开切换 | 后端实际通过 |
| 顶层禁止删除 ACL 规则 | 有 | 有 | 规则写入通过，右键删除效果未测 |
| 顶层详细 ACL 清单 | icacls 全量打印 | **仅摘要，没有完整 ACE 列表** | 尚未等价 |
| 打开资源管理器 | 有 | 有 | GUI 点击尚未验证 |
| 递归处理大量文件 | 有 | 有，异步 | 只测小目录 |
| 状态显示 | 文本、icacls | 图形摘要、耗时/错误提示 | GUI 界面已捕获 |
| 精确恢复原 ACL | 不支持 | 不支持 | 仍重置继承而非还原快照 |

## 未验证项目和风险

1. Windows 7 SP1、Windows 10 实机运行与 DPI/不同分辨率适配。
2. GUI 全部按钮点击、确认对话框、失败回退与 UAC 行为。
3. 不同标准用户账户跨身份读取测试，以及直接通过已知子文件路径读取。
4. 资源管理器右键删除实际拦截效果：**父目录 DELETE_CHILD 可能绕过目标目录拒绝规则**。
5. 8 万文件规模性能、中断恢复、特殊自定义 ACL。
6. 当前 C++ 版缺少脚本提供的完整 ACL 明细显示。

**结论：核心权限后端已通过自动化小样本测试，GUI 已能启动和绘制，暂不能宣称 Windows 7/10 兼容性及全部用户交互验收完成。**
