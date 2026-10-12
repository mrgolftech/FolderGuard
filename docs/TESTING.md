# 手工验收清单（v2.3）

> 所有测试都应使用**独立空测试目录**（例如 `E:\FolderGuard-Test`），不要使用真正的回收站、系统目录或已有重要数据目录。测试不应依靠实际删除重要目录判断是否成功。

## 环境准备

- Windows 10 / Windows 11，Windows PowerShell 5.1；
- 一个 NTFS 数据分区；
- 一个管理员账户 + 一个标准用户账户（用于隔离验证）；
- 空目录 `E:\FolderGuard-Test`，其中新建 `sample.txt` 和子目录 `sub\inside.txt`；
- 用 `icacls` 备份测试目录原权限，并确认可还原。

参考：

```powershell
$path = 'E:\FolderGuard-Test'
Get-Volume -DriveLetter E | Format-List DriveLetter,FileSystem
icacls.exe $path
icacls.exe $path /save 'D:\folderguard-test-acl.txt' /t
```

## 基本测试

| ID | 操作 | 预期 |
| --- | --- | --- |
| T01 | 双击 CMD 启动器 | 弹出 UAC，进入中文菜单，无乱码 |
| T02 | 输入不存在的数据目录 | 菜单 1 创建并显示绝对路径 |
| T03 | 设置 +S +H | 属性同时出现 System 与 Hidden |
| T04 | 去除 -S -H | 属性移除，原文件不受影响 |
| T05 | 快速加锁 | 顶层 ACL 继承关闭；Administrators/SYSTEM Allow；Authenticated Users 顶层 Deny DELETE/DELETE_CHILD；+S |
| T06 | 查看状态 | 显示目录、属性、顶层 ACL 及防删除警告 |
| T07 | 快速解锁 | 顶层继承恢复并重置；子项 ACL 不被本操作主动重置；+S/+H 保留 |
| T08 | 完整加锁 | 测试子项继承管理员/SYSTEM 权限，目录自动 +S |
| T09 | 完整解锁 | 测试目录与文件恢复继承；原自定义 ACL 不保证还原 |
| T10 | 空回车、非法菜单值 | 不误触权限操作，不发生 PowerShell 异常 |
| T11 | 选择真正 `E:\$RECYCLE.BIN` | 被保护逻辑拒绝 |
| T12 | 选择盘符根目录 `E:\` | 被拒绝 |
| T13 | 标准用户直接打开顶层目录 | 根据 ACL 拒绝访问；以实际标准用户验证 |
| T14 | 管理员访问 | 可通过提权管理；不误认为 ACL 可对抗管理员 |
| T15 | 快速加锁后的已知子文件直接访问 | 记录是否可能绕过顶层目录遍历（可能成功） |
| T16 | 加锁后的右键删除行为 | **只在空测试目录**验证；若可删除，应记录父目录 DELETE_CHILD 的影响，不能判定脚本误报“绝对防删除” |
| T17 | 含重解析点的完整加锁 | 拒绝跨链接递归更改 ACL |

## 大目录性能测试

使用独立样本目录，分别运行快速与完整加锁，并对比耗时。至少记录：

- NTFS / SSD / HDD 类型及 Windows 版本；
- 文件和目录数量；
- 脚本版本、使用菜单；
- 执行耗时；
- `icacls` 退出码、错误数；
- 是否返回菜单及最终 ACL 状态。

不要为了性能测试在真实的 8 万多文件目录上重复执行完整解锁。无输出不代表挂起，应等脚本完成消息或检查进程状态。

## 验收边界

- 显示 Deny ACE ≠ 保证防删除；父目录 `DELETE_CHILD` 是明确未覆盖的前提。
- 快速加锁 ≠ 子文件严格访问保护。
- 完整解锁是权限重置而非历史 ACL 精确恢复。
- 加锁不加密，`+S/+H` 不是安全边界。

## 当前状态

此清单用于 Windows 实机验证；截至首次仓库提交，尚未提供 Windows 上自动化测试日志。不得将以上“预期”误写成“已经通过”。
