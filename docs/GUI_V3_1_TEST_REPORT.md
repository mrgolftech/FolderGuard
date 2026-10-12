# FolderGuard GUI v3.1 实测记录

日期：2026-10-12。

## 目标

将 GUI 切换为浅色主题，补充只读 ACL 明细，并验证窗口交互与权限后端。

## 已完成

- 采用轻量 Direct2D 1.0 绘制浅色 GUI：白色卡片、浅灰底色、蓝色主按钮。
- 新增“ACL 明细”可滚动窗口：展示顶层目录所有者、继承状态、SID/账户名称、允许/拒绝 ACE、权限掩码和继承标志，并支持复制。
- 支持 `FolderGuard.exe --path "E:\MyVault"` 初始化目录，仍受原有安全检查保护。
- 现有 Manifest 仍为 `requireAdministrator`，启动时必须 UAC 提权。

## Windows Server 2022 Actions 实测

- [验证通过的运行记录](https://github.com/mrgolftech/FolderGuard/actions/runs/38242424170)
- x86 和 x64 编译通过。
- Windows x64 管理员令牌下，**13 项 NTFS 后端测试通过，0 失败，0 跳过**：新增权限明细报告内容断言。
- GUI 自动点击（并验证真实 NTFS 属性）：
  - System +S ✅
  - System -S ✅
  - Hidden +H ✅
  - Hidden -H ✅
  - ACL 明细窗口成功打开、报告控件存在 ✅
- Direct2D 初始窗口截图通过 `PrintWindow` 成功截取，可在运行记录的 `FolderGuard-v3-GUI-Screenshots` 工件下载。

## 和 PowerShell v2.3 的差异

| 功能 | GUI v3.1 |
| --- | --- |
| 快速/完整加解锁 | 已实现，后端临时目录测试通过 |
| 加锁自动 +S | 已实现，后端测试通过 |
| 单独设置/清除 +S/+H | 已实现，GUI 点击测试通过 |
| 顶层 DELETE/DELETE_CHILD 拒绝 ACE | 已实现，后端写入和报告测试通过 |
| 显示顶层详细 ACL | 已实现：独立可复制报告 |
| 打开资源管理器 | 已实现，尚未自动点击验证 |
| 大量文件任务 | 后台线程，暂缺万级文件压力测试 |
| 原始 ACL 精确备份恢复 | 未实现；解锁使用继承重置 |
| 右键删除拦截 | **未验证**；可能受父目录 DELETE_CHILD 权限影响 |

## 仍待实机验证

1. Windows 7 SP1（x86/x64）和 Windows 10（多版本）真实运行兼容性。
2. DPI 125%/150%、多分辨率、中文 IME、屏幕阅读器等。
3. 完整 ACL 操作中途断电/取消、8 万级文件性能。
4. 不同普通用户/管理员之间的访问差异及右键删除测试。**不要在真实重要数据上测试删除**。

> 注意：Windows Server 2022 的通过结果，不能等同 Windows 7/10 的实机通过。FolderGuard 是 ACL/隐藏工具，不是加密工具。
