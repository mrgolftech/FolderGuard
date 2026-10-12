# FolderGuard v3.1 · Native Windows desktop

**C++17 + Win32 + Direct2D 1.0 + DirectWrite**. A lightweight, native GUI for the existing PowerShell FolderGuard project, targeted at Windows 7 SP1 and Windows 10.

![Build](https://github.com/mrgolftech/FolderGuard/actions/workflows/native-build.yml/badge.svg)

## What this version implements

- Custom light interface drawn with Direct2D; native path edit control and Vista+/Win7 folder picker.
- Fast and full root ACL lock/unlock, explicit non-inherited delete-deny ACE, auto `+S` on lock.
- Independently toggle `System (+S)` and `Hidden (+H)` attributes.
- Read-only **ACL details** window: owner, inherited/explicit ACEs, access masks, DENY/ALLOW, and copy report.
- NTFS checks, restricted paths, recursive reparse-point scan before full lock.
- Elevated process via embedded `requireAdministrator` UAC manifest (permission prompt at every start); background worker thread for lengthy permission updates.
- Compiled **x86 and x64 EXE** from Visual Studio 2022 GitHub Actions; `/MT` static VC runtime, no .NET or third-party runtime.

## How to run

1. Download the `FolderGuard-v3-x64` or `FolderGuard-v3-Win32` GitHub Actions artifact once a build has succeeded.
2. Extract and launch `FolderGuard.exe` with administrator approval.
3. Choose or enter a **user-created folder** on an NTFS disk; start with an empty test directory such as `E:\FolderGuard-Test`.
4. Select Quick Lock / Quick Unlock, or Full Lock / Full Unlock for recursive ACL changes. `+S` is applied automatically when locking; `+H` is independently toggled.
5. Check the status panel for root inheritance and delete-deny indicator. Use **ACL 明细** to inspect and copy individual ACEs.

To open a specific folder directly from a command prompt or shortcut (the path is still validated before any modification):

```powershell
FolderGuard.exe --path "E:\\MyVault"
```

### Administrator permissions

The EXE currently has a `requireAdministrator` manifest: **UAC elevation is required at application startup**, including when you only want to view ACL details. This deliberately favors a simple single-process implementation. A future version can use read-only normal-user startup with on-demand elevation for ACL modification.

**Important:** The program does not prevent deletion in all ACL arrangements. Parent `DELETE_CHILD` may permit deletion despite target deny, and administrators can change DACLs. Fast lock does not protect child files with permissive ACLs. No encryption is performed. Full unlock resets ACLs and can destroy custom permissions. Never test deletion on important files.

## Build locally (Visual Studio 2022)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
# build/Release/FolderGuard.exe
```

For x86, replace `-A x64` with `-A Win32`.

> Win7 compatibility requires Win7 SP1 and a suitable installation of Direct2D 1.0 / DirectWrite system libraries. Direct2D 1.1/DirectComposition are intentionally not used. VS2026 is not an acceptable toolchain target for Win7.

See [native implementation design](docs/GUI_DESIGN.md) and the [legacy PowerShell documentation](README.md).

## Current verification status

CI on Windows Server 2022 verifies **13 NTFS backend cases** plus real-window mouse actions for System/Hidden on/off and opening the ACL viewer (x64); builds x86/x64. This does **not** constitute Windows 7 SP1 or Windows 10 runtime validation, which is still required before a stable release. Check GitHub Actions status before using an artifact; first test on disposable data only.
