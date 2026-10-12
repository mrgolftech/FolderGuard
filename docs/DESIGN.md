# 设计说明：FolderGuard v2.3

## 1. 目标与非目标

**目标**：提供一个管理员可执行的、带中文交互菜单的 Windows 文件夹访问控制工具。对于已知的 NTFS 数据目录，可切换顶层快速模式或批量完整模式，可增删 System/Hidden 属性，并显示根目录 ACL 与“防删除”规则。

**非目标**：对抗本地管理员、抵抗离线读取、保证目录永远无法删除、对文件进行加密、构造真正的系统回收站或替换 Windows 权限体系。

## 2. 运行架构

```text
Start-FolderGuard.cmd
  └─ powershell.exe -NoProfile -ExecutionPolicy Bypass
      └─ Start-Process powershell.exe -Verb RunAs（请求 UAC）
          └─ FolderGuard.ps1
              ├─ Set-Target / Assert-Target（路径选择与保护）
              ├─ Quick-Lock-Target / Quick-Unlock-Target
              ├─ Lock-Target / Unlock-Target
              ├─ Set-Hidden / Add-SystemAttribute
              └─ Show-Status（icacls + ACL API + 属性）
```

使用 Windows PowerShell 5.1/.NET FileSystemSecurity 读写顶层 ACL，批量子项处理调用 Windows 自带 `icacls.exe`。

## 3. 模式与权限模型

### 3.1 顶层快速加锁

`Set-RootLockedAcl($false)`：

1. 获取目标顶层目录的 DACL；
2. 关闭继承，并删除继承的 ACE（`SetAccessRuleProtection(true,false)`）；
3. 删除剩余显式访问规则；
4. 给予 `BUILTIN\Administrators`（`S-1-5-32-544`）和 `SYSTEM`（`S-1-5-18`）完全控制，`InheritanceFlags=None`；
5. 给 `Authenticated Users`（`S-1-5-11`）添加顶层**显式拒绝** `DELETE` + `DeleteSubdirectoriesAndFiles`（`DELETE_CHILD`），不向子文件传播；
6. 为目录设置 `System`（`+S`），不改变 `Hidden`（`+H`）状态。

此实现针对**当前顶层目录**。不保证子目录中已有独立 ACL 的数据访问安全；Windows 默认允许绕过遍历检查。

### 3.2 顶层快速解锁

依次运行：

```powershell
icacls.exe $path /inheritance:e /q
icacls.exe $path /reset /q
```

只处理目标顶层目录；`+S/+H` 不变。此步骤**不会精确恢复原来各个自定义 ACE**。

### 3.3 完整加锁

- 先检查子树是否存在重解析点（符号链接/目录联接点）。
- 调用 `Set-RootLockedAcl($true)`，为管理员与 SYSTEM 的允许规则配置容器/对象继承；顶层防删除拒绝规则仍不继承。
- 添加顶层 `+S`。
- 对可见子项执行 `icacls <target>\* /reset /t /q`，利用顶层新的可继承 ACE 重置子项 ACL。
- 不逐文件回显成功消息，减少大目录终端输出开销。

**影响**：会覆盖内部自定义 ACL。这里的“完整”指批量保护访问，不表示对每个内部文件添加防删除拒绝规则；子项的防删除效果也依赖其父目录权限及文件自身权限。

### 3.4 完整解锁

```powershell
icacls.exe $path /inheritance:e /q
icacls.exe $path /reset /t /q
```

先恢复根目录继承，再递归重置全部 ACL；不会还原历史自定义 ACL，且不会修改 `+S/+H`。

### 3.5 属性与访问控制正交

`+S` = System；`+H` = Hidden。菜单 4/5 分别增删两个属性，不递归修改子文件。ACL 的加锁/解锁是另一维度；解锁时属性保持不变。

## 4. 为什么“防删除”不是绝对保护

在 NTFS 上，对目标删除可能通过**目标自身的 DELETE**，或**父目录的 DELETE_CHILD** 授权。在目标目录上设置显式 Deny `DELETE` 与 `DELETE_CHILD` 并不撤销父目录上的 `DELETE_CHILD`。

因此，v2.3 **不会触碰父目录的 ACL**，避免误伤整个盘符根目录；但这意味着在某些用户/父目录权限组合下，资源管理器右键删除仍然可能成功。工具在状态页提示该边界。拒绝 `Authenticated Users` 也可能影响当前管理员用户的普通删除行为；显式 Deny 应慎用。

其他风险：

- 管理员能够通过获得/更改权限恢复访问。
- 快速模式不修复子项原来的显式授权；知道精确文件路径仍可能读取。
- 文件夹可见性不是访问权限，更不是加密。
- 若子树出现联接点/符号链接或路径竞态，递归修改可能带来跨目录风险。
- ACL 写入不是整个子树的原子事务；中途失败可能形成部分生效状态。
- 目录已经锁定时，再运行“选择目录/状态查询”可能受当前进程实际令牌影响。

## 5. 安全护栏及已知限制

| 防护/设计 | 当前实现 | 限制 |
| --- | --- | --- |
| 管理员检查 | 执行 ACL 更改前检查 | 管理员仍可接管 |
| 本地绝对盘符路径 | 正则校验 `^[A-Za-z]:\\` | 未强制验证 NTFS，某些文件系统不支持 ACL |
| 拒绝系统目录 | 对 Windows/Users/ProgramData/系统回收站等根下名称做初步限制 | 不是完整的系统目录白名单/黑名单，不代替人工复核 |
| 拒绝链接 | 选目录时检查路径各级链接；完整模式检查子树 | 非原子，有竞态及扫描开销 |
| 加锁确认 | 需要输入 Y | 仍可能误选目录 |
| ACL 恢复 | 恢复继承并重置 | 不是备份还原，原有自定义 ACE 丢失 |
| 大量文件输出 | `icacls /q` | 消除输出不等于消除文件系统写入成本 |
| 防删除 | 目标顶层显式 Deny DELETE/DELETE_CHILD | 父目录 DELETE_CHILD 可能绕过 |
| 数据机密性 | 不提供加密 | 建议 BitLocker/EFS |

## 6. 文件布局与版本管理

```text
FolderGuard/
├── FolderGuard.ps1           # 主程序（v2.3）
├── Start-FolderGuard.cmd     # UAC 启动器
├── README.md                 # 项目首页
├── docs/
│   ├── DESIGN.md             # 本文
│   ├── USAGE.md              # 使用说明
│   └── TESTING.md            # 手工验收用例
└── .gitignore
```

代码采用 UTF-8；Windows PowerShell 5.1 的中文输出建议将 `.ps1` 文件保持为 **UTF-8 with BOM**，以免发生中文乱码。首次公开提交保留既有 v2.3 的行为，不宣称已经通过 Windows 实机测试。

## 7. 后续演进建议

- 加入目标 ACL 自动备份 + 可审计的原权限恢复机制。
- 将“阻止浏览”和“严格子文件访问保护”分成明确的安全级别。
- 可选增加独立受保护父目录，配合父目录 DELETE_CHILD 规则，但绝不默认修改盘符根目录。
- 增加异常中断后恢复向导、命令执行日志、测试用 NTFS 虚拟磁盘环境。
- 给所有敏感操作补充集成测试及测试前后 `icacls` 差异断言。

参见 [使用说明](USAGE.md) 和 [测试清单](TESTING.md)。
