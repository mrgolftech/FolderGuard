# FolderGuard v0.0.1 · Windows 原生文件夹管理

原生 **C++17 + Win32 + Direct2D 1.0 / DirectWrite**，兼容目标为 Windows 7 SP1 和 Windows 10。无 .NET、Qt、WebView 或 Electron 依赖。

## v0.0.1 紧凑原生界面

- **无侧边栏**：单页浅色工作台，紧凑布局。
- **启动无默认目录**：路径框初始为空；未载入有效 NTFS 目录之前，刷新、设置选项和应用按钮均禁用。
- 「浏览」选择目录后自动读取状态；手动输入或粘贴绝对路径后按 Enter 载入，不再保留「载入目录」按钮。手工路径不存在时会先要求确认是否创建。
- **实际状态摘要**：ACL 继承限制、拒绝删除规则、System (+S)、Hidden (+H) 四项始终来自磁盘读取，而非仅凭按钮记忆状态。
- **集中设置**：仅保留「ACL 权限保护」「禁止删除」「系统属性 +S」「隐藏属性 +H」四个原生 Win32 复选框，先调整草稿，不立即修改磁盘。
- **统一应用**：底部红色按钮「应用设置」，依次处理所需操作；完成时弹窗报告执行结果、动作和耗时，再重新读取磁盘状态，并同步选项高亮。
- **四项真正独立**：ACL 保护将顶层目录权限限制为 Administrators/SYSTEM 且关闭继承（不自动禁止删除或添加 +S）；禁止删除单独增删不继承的精确拒绝 ACE；+S 与 +H 独立控制，互不自动勾选。
- 页面右上角仅保留「刷新状态」；底部显示 v0.0.1 和项目 GitHub 链接。移除了 GUI 中完整递归模式和 ACL 明细窗口，但保留原有 PowerShell 脚本与后端能力。

## 界面与 DPI 适配

- 窗口逻辑画布由 1000×680 缩小至约 **760×470**，状态保持四项摘要。
- 「浏览」「刷新状态」「路径编辑」以及四个勾选项采用 **Win32 原生控件**，具备标准焦点和键盘交互；结果确认继续采用系统消息框。
- 红色「应用设置」、状态卡片和背景保留 Direct2D/DirectWrite 自绘，减少自制控件逻辑。
- **不再分别拉伸横纵轴**：统一按 Windows 系统 DPI 缩放，在小屏幕上按可用桌面空间等比缩小；窗口变化时重新排布控件，避免圆形和文字变形。
- 支持从路径输入框按 Enter 载入、Tab 切换控件；ACL、禁止删除、+S、+H 不再互相锁定。

## 管理员权限

EXE 目前嵌入 **requireAdministrator** Manifest，每次启动需批准 UAC。尽管查询 ACL 可能不需要管理员权限，当前版本为简化单进程权限修改仍统一提权。

## 启动与操作

1. 解压来自 [GitHub Actions](https://github.com/mrgolftech/FolderGuard/actions/workflows/native-build.yml) 的 x86 或 x64 工件。
2. 启动 FolderGuard.exe，允许 UAC。默认目录为空。
3. 浏览选择现有数据目录（自动载入），或输入本地 NTFS 绝对路径后按 Enter。
4. 在「当前保护状态」读取四个真实状态。
5. 修改四个设置选项。**此时仅是草稿，磁盘未修改。**
6. 点击底部红色「应用设置」。涉及 ACL 权限切换时另有权限变更确认。
7. 完成后查看弹窗中的操作、耗时和最终状态。设置项高亮会回到磁盘实际结果，失败时也会重新读取。
8. 可以随时点击右上角「刷新状态」丢弃未应用草稿并以磁盘状态重新同步。

可选命令行初始化现有目录（不会取消路径安全校验）：

```powershell
FolderGuard.exe --path "E:\MyVault"
```

## 权限边界

- GUI 中 ACL 保护会替换目标顶层 DACL，只保留 Administrators/SYSTEM 的访问权；**不能保证内部文件经已知完整路径无法被直接读取**。
- 拒绝 DELETE/DELETE_CHILD 的 ACE 是设置在目标顶层目录上的；若父目录仍有 DELETE_CHILD 授权，右键删除可能成功。
- GUI 中关闭 ACL 保护会通过 icacls /reset 恢复父目录继承，自定义 ACE 不会自动恢复；独立的禁止删除 ACE 会在重置后重新应用。正式操作前务必备份原 ACL。
- +S / +H 完全独立，两者均不等于加密。
- 不应选择系统目录或实际 Windows 回收站；禁止在包含重要数据的目录直接测试删除。

## 实机验证边界

- GitHub CI 运行的是**真实 Windows NTFS 文件系统**，不是模拟状态；会对仅供测试的临时目录实际写入系统属性、隐藏属性与 ACL ACE。
- GitHub 托管运行器属于 Windows Server 2022 虚拟机环境。它**不能证明 Win7 SP1 32 位实体机兼容性**，需要在独立 Win7 32 位机器上使用相同测试程序进行验收。
- 拒绝目标目录 DELETE 的 ACE 不保证阻止来自父目录 DELETE_CHILD 的实际删除；自动化用真实 CreateFile(DELETE) 验证目标 DELETE 权限，不将其解释为绝对不可删除。
- 删除开关仅移除本工具格式的精确拒绝 ACE，不会清除其他更宽的拒绝规则。
- 若旧版 ACL 存在预先设置的其他自定义条目，不应在正式资料上直接尝试解除；先用 icacls 保存 DACL 快照。

## 构建与测试

Visual Studio 2022、CMake、Windows SDK：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

GitHub Actions 使用 Windows Server 2022 Runner 构建 x86/x64，并在运行中的真实 Windows NTFS 临时目录上逐项变更和读取四个设置，核查 DACL/ACE 与实际属性，同时运行 GUI 交互回归。Hosted Runner 属于 Windows 虚拟机，并非物理 Windows 7 真机。**这不等于已经在 Windows 7 SP1、Windows 10 目标系统上完成实机验收。**

详见 [GUI 设计说明](docs/GUI_DESIGN.md) 与 [PowerShell 旧版说明](README.md)。
