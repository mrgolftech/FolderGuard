# FolderGuard v3 native desktop design

## Intent
A lightweight, no-WebView, no-.NET desktop utility for protecting **user-created NTFS data folders**. Win7 SP1 and Windows 10 are targets. The GUI uses a hybrid design: native Win32 EDIT, BUTTON and four AUTOCHECKBOX controls for accessible standard input, plus Direct2D 1.0 / DirectWrite for status summaries and a single red Apply button. The native edit preserves IME, selection and clipboard behavior. Browsing auto-loads folders, and Enter loads typed paths.

## Technical decisions
- C++17 + classic Win32 message loop; `ID2D1HwndRenderTarget` and `IDWriteFactory` (Windows 7 baseline).
- x86/x64 builds using Visual Studio **2022** with `WINVER=0x0601`, `_WIN32_WINNT=0x0601`; do not migrate to VS2026 until Win7 support is removed.
- MSVC static CRT `/MT`; application runs from a portable `.exe`, no third-party UI assets, no service, no network, no Qt or .NET.
- UAC `requireAdministrator` application manifest. GUI launch requires an administrator approval each time; while ACL reading can be unprivileged, this first delivery intentionally keeps the process elevated to simplify modifications.
- Dedicated worker thread for ACL operations, completion via `PostMessageW`; indeterminate bar, elapsed milliseconds, no UI thread blocking.
- Distinct operation confirmation for destructive full ACL changes; no file delete or move buttons.
- Readable, light compact layout with native file browsing, edit, refresh and checkbox widgets, plus rounded status surfaces.
- Compact 760x470 logical viewport; a uniform X/Y scale determined from system DPI (GetDeviceCaps LOGPIXELSX) and desktop work area. Native controls are anchored/relaid out on WM_SIZE, while Direct2D uses a matching uniform transform. No Win8+ DPI API is required. Windows 7 SP1 x86/x64 still needs real-machine validation.

## Independent GUI controls (v0.0.1)

- ACL: toggle DACL inheritance/protection using SetNamedSecurityInfoW and current DACL; do not call icacls /reset or automatically change deletion denial or +S.
- Delete prevention: add/remove one explicit, non-inherited Authenticated Users DENY(DELETE | FILE_DELETE_CHILD) ACE, preserving other ACEs; do not change ACL inheritance. Removing a broader preexisting denial is deliberately refused.
- System and Hidden: independently modify FILE_ATTRIBUTE_SYSTEM and FILE_ATTRIBUTE_HIDDEN with SetFileAttributesW.
- Legacy script and QuickLock/QuickUnlock actions retain their historical semantics; GUI uses only independent actions.
- CI on Windows Server 2022 (x86 under WoW64 plus x64) exercises all toggles on disposable NTFS folders and GUI; no Windows 7 physical-machine qualification is claimed.

## Security architecture
- **Fast lock:** replace root DACL with explicit deny `Authenticated Users: DELETE | FILE_DELETE_CHILD`, allow `Administrators` and `SYSTEM` all access. Deny not inherited. Root inheritance disabled. Automatically set `+S`.
- **Fast unlock:** `icacls <root> /inheritance:e /q` then `icacls <root> /reset /q`. Only root.
- **Full lock:** root DACL inherited allow for Administrators/SYSTEM, root-only deny; reset child ACL with `icacls <root>\\* /reset /t /c /q`. Actual child files are modified; custom original ACEs may be lost.
- **Full unlock:** enable root inheritance then reset root and descendants with `icacls <root> /reset /t /c /q`. Not an exact backup restoration.
- **System/hidden flags:** `SetFileAttributesW` preserving other attributes. `+S` automatic at lock; `+H` user-controlled.
- **Read status:** GetNamedSecurityInfoW, GetSecurityDescriptorControl (root inheritance), GetAce (explicit deny).
- **Guardrails:** reject drive roots, known system-maintained directory segments, non-NTFS volumes, and reparse-point path segments; full lock pre-scans descendants for reparse points. Not race-free; no security boundary against another administrator.

### Important limits
- Target-specific `DELETE` denial cannot block deletion authorized by the parent `DELETE_CHILD` right. This tool does not modify the parent/drive root ACL. **Never assert deletion is impossible**.
- Deny can affect administrators because they are also authenticated users. It does not remove the ability of a sufficiently privileged administrator to change owner/ACL.
- Fast mode does not change child ACL, so known full paths may remain accessible via Windows traverse privileges.
- Full mode changes ACLs across all files and is neither atomic nor automatically reversible to original custom ACLs; back up first.
- No encryption, and file size/volume capacity remains visible to administrative tools.
- Folders with special names are user-provided. The real Windows `$RECYCLE.BIN` and `System Volume Information` are always excluded.
- Windows 7 is end-of-life; compatibility is best effort and is not a security endorsement.

## Test matrix
| Test | OS | Architecture | Result |
| --- | --- | --- | --- |
| GitHub Actions compile | windows-2022 | x86 + x64 | Pending |
| Window opens / live resizing | Win10 | x64 | Pending |
| Window opens / D2D baseline | Win7 SP1 | x86/x64 | Pending |
| Empty directory fast lock/unlock | Win7, Win10 | x64 | Pending |
| Nested ACL full lock/unlock | Win7, Win10 | x64 | Pending |
| Parent DELETE_CHILD behavior | Win7, Win10 | standard user vs admin | Pending |
| CJK IME, 125%/150% DPI | Win7, Win10 | x64 | Pending |

## Follow-ups
1. Exact ACL backup/restore format and failure-safe transaction journal.
2. Per-action elevation and accessible native UI automation peers.
3. Native automated integration tests in NTFS test volume, GUI screenshot regression.
4. Cancellation and granular progress for very large recursive operations.
5. GUI smoke test uses actual window messages and checks real NTFS attribute changes; Windows 7/10 VM acceptance remains outstanding.
