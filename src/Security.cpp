#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include "Security.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace security {
namespace {
std::wstring ErrorText(DWORD error) {
    LPWSTR buffer = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring result = n && buffer ? std::wstring(buffer, n) : L"Windows 错误 " + std::to_wstring(error);
    if (buffer) LocalFree(buffer);
    return result;
}
std::wstring TrimPath(const std::wstring& input) {
    std::wstring p = input;
    while (!p.empty() && (p.front() == L' ' || p.front() == L'\t' || p.front() == L'"')) p.erase(p.begin());
    while (!p.empty() && (p.back() == L' ' || p.back() == L'\t' || p.back() == L'"')) p.pop_back();
    std::replace(p.begin(), p.end(), L'/', L'\\');
    return p;
}
std::wstring Canonical(const std::wstring& input) {
    auto p = TrimPath(input);
    if (p.size() < 4 || p[1] != L':' || p[2] != L'\\' ||
        !((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) ||
        p.find(L'"') != std::wstring::npos) return {};
    std::vector<wchar_t> buf(32768);
    auto n = GetFullPathNameW(p.c_str(), static_cast<DWORD>(buf.size()), buf.data(), nullptr);
    if (!n || n >= buf.size()) return {};
    std::wstring result(buf.data(), n);
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    if (result.size() <= 3 || result.size() >= MAX_PATH - 1) return {};
    // Deny device syntax, ADS, and unportable trailing dots/spaces.
    if (result.find(L':', 2) != std::wstring::npos) return {};
    if (result.back() == L'.' || result.back() == L' ') return {};
    return result;
}
std::wstring Lower(std::wstring p) {
    std::transform(p.begin(), p.end(), p.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return p;
}
bool SafePath(const std::wstring& p, std::wstring& why) {
    const auto l = Lower(p);
    // Never mutate system-maintained folders, anywhere in the path.
    for (const auto& forbidden : {L"\\windows", L"\\program files", L"\\program files (x86)",
        L"\\programdata", L"\\system volume information", L"\\$recycle.bin", L"\\recovery", L"\\boot"}) {
        std::wstring f(forbidden);
        auto pos = l.find(f);
        if (pos != std::wstring::npos && (pos + f.size() == l.size() || l[pos + f.size()] == L'\\')) {
            why = L"目标路径包含系统保护目录：" + f;
            return false;
        }
    }
    // Reject reparse points at every existing path segment (symlinks/junctions).
    std::wstring part = p.substr(0, 3);
    for (size_t i = 3; i <= p.size();) {
        size_t next = p.find(L'\\', i);
        if (next == std::wstring::npos) next = p.size();
        if (next > i) {
            if (part.size() > 3) part += L'\\';
            part += p.substr(i, next - i);
            const DWORD attr = GetFileAttributesW(part.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
                why = L"目录路径含符号链接/联接点，已拒绝操作。";
                return false;
            }
        }
        if (next == p.size()) break;
        i = next + 1;
    }
    return true;
}
bool IsNtfs(const std::wstring& p) {
    std::wstring root = p.substr(0, 3);
    wchar_t fs[MAX_PATH] = {};
    return GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs, MAX_PATH) &&
        _wcsicmp(fs, L"NTFS") == 0;
}
Result Guard(const std::wstring& input, bool create) {
    auto p = Canonical(input);
    if (p.empty()) return {false, L"请选择盘符下的正常绝对目录路径（不能为磁盘根目录、超长路径或带尾随点号的目录）。"};
    std::wstring why;
    if (!SafePath(p, why)) return {false, why};
    if (!IsNtfs(p)) return {false, L"目标所在分区必须为 NTFS。"};
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES && create) {
        const int r = SHCreateDirectoryExW(nullptr, p.c_str(), nullptr);
        if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS)
            return {false, L"创建目录失败：" + ErrorText(r)};
        attr = GetFileAttributesW(p.c_str());
    }
    if (attr == INVALID_FILE_ATTRIBUTES) return {false, L"目录不存在。"};
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return {false, L"路径不是目录。"};
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return {false, L"禁止操作符号链接或联接点目录。"};
    return {true, L"目标目录已就绪。"};
}
bool MakeSid(WELL_KNOWN_SID_TYPE type, std::vector<BYTE>& sid) {
    sid.resize(SECURITY_MAX_SID_SIZE);
    DWORD size = static_cast<DWORD>(sid.size());
    return CreateWellKnownSid(type, nullptr, sid.data(), &size) != FALSE;
}
Result LockRoot(const std::wstring& path, bool inheritedAllow, bool withDeny=true) {
    std::vector<BYTE> au, admins, system;
    if (!MakeSid(WinAuthenticatedUserSid, au) || !MakeSid(WinBuiltinAdministratorsSid, admins) ||
        !MakeSid(WinLocalSystemSid, system)) return {false, L"无法创建系统安全标识符 (SID)。"};
    EXPLICIT_ACCESSW access[3] = {};
    auto setEntry = [](EXPLICIT_ACCESSW& e, PSID sid, DWORD perms, ACCESS_MODE mode, DWORD inherit) {
        e.grfAccessPermissions = perms;
        e.grfAccessMode = mode;
        e.grfInheritance = inherit;
        e.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        e.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        e.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
    };
    DWORD count=0;
    // Independent GUI ACL switch intentionally does NOT add a delete deny.
    if(withDeny)setEntry(access[count++], au.data(),DELETE|FILE_DELETE_CHILD,DENY_ACCESS,NO_INHERITANCE);
    const DWORD inheritance = inheritedAllow ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
    setEntry(access[count++], admins.data(), GENERIC_ALL, GRANT_ACCESS, inheritance);
    setEntry(access[count++], system.data(), GENERIC_ALL, GRANT_ACCESS, inheritance);
    PACL acl = nullptr;
    DWORD e = SetEntriesInAclW(count, access, nullptr, &acl);
    if (e != ERROR_SUCCESS) return {false, L"构造 ACL 失败：" + ErrorText(e)};
    auto modifiable = path;
    e = SetNamedSecurityInfoW(&modifiable[0], SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
    if (acl) LocalFree(acl);
    if (e != ERROR_SUCCESS) return {false, L"写入目标 ACL 失败：" + ErrorText(e)};
    return {true, L"顶层 ACL 已设置。"};
}
Result RunIcacls(const std::wstring& path,const std::wstring& args);
Result ToggleDeleteAce(const std::wstring& p,bool enable);

// ACL protection is an access restriction, not merely a DACL inheritance bit.
// Legacy quick-lock semantics are preserved (Administrators/SYSTEM only on root).
// This GUI action deliberately excludes the delete-deny ACE and +S.
// Disabling resets to parent inheritance: pre-existing custom ACEs are NOT restored.
Result ToggleAclInheritance(const std::wstring& p, bool protect) {
    const auto previous=ReadStatus(p);
    if(!previous.error.empty())return {false,L"读取 ACL 状态失败："+previous.error};
    Result r;
    if(protect)r=LockRoot(p,false,false);
    else {
        r=RunIcacls(p,L"/inheritance:e /q");
        if(r.ok)r=RunIcacls(p,L"/reset /q");
    }
    if(!r.ok)return r;
    // Preserve the independent delete-deny checkbox even when ACL is replaced/reset.
    if(previous.denyDelete) {
        r=ToggleDeleteAce(p,true);
        if(!r.ok)return {false,L"ACL 已切换，但恢复独立禁止删除规则失败："+r.message};
    }
    return {true,protect?L"仅保留 Administrators/SYSTEM 对顶层目录的访问；禁止删除与属性独立。":
        L"已恢复顶层目录继承的 ACL（原自定义 ACE 不会自动恢复）。"};
}

// FolderGuard owns only a non-inheriting, exact DELETE | DELETE_CHILD deny ACE.
// A different/broader pre-existing deny ACE is never removed by this toggle.
bool IsDeleteAce(const void* ace,PSID auth,bool exact) {
    const auto* head=static_cast<const ACE_HEADER*>(ace);
    if(head->AceType!=ACCESS_DENIED_ACE_TYPE ||
       (head->AceFlags & (INHERITED_ACE|INHERIT_ONLY_ACE)))return false;
    const auto* denied=static_cast<const ACCESS_DENIED_ACE*>(ace);
    if(!EqualSid(const_cast<PSID>(static_cast<const void*>(&denied->SidStart)),auth))return false;
    constexpr DWORD rights=DELETE|FILE_DELETE_CHILD;
    return exact?(denied->Mask==rights && (head->AceFlags &
        (CONTAINER_INHERIT_ACE|OBJECT_INHERIT_ACE))==0):
        (denied->Mask & rights)==rights;
}

Result ToggleDeleteAce(const std::wstring& p,bool enable) {
    std::vector<BYTE> auth;
    if(!MakeSid(WinAuthenticatedUserSid,auth))return {false,L"无法创建 Authenticated Users SID。"};
    PSECURITY_DESCRIPTOR sd=nullptr;
    PACL old=nullptr;
    DWORD err=GetNamedSecurityInfoW(const_cast<LPWSTR>(p.c_str()),SE_FILE_OBJECT,
                                    DACL_SECURITY_INFORMATION,nullptr,nullptr,&old,nullptr,&sd);
    if(err!=ERROR_SUCCESS)return {false,L"读取现有 DACL 失败："+ErrorText(err)};
    if(!old){LocalFree(sd);return {false,L"目标目录 DACL 为空，已拒绝修改。"};}
    bool hasExact=false,hasBroader=false;
    for(DWORD i=0;i<old->AceCount;++i){
        void* ace=nullptr;
        if(!GetAce(old,i,&ace))continue;
        hasExact |= IsDeleteAce(ace,auth.data(),true);
        hasBroader |= IsDeleteAce(ace,auth.data(),false);
    }
    if(enable && hasBroader){LocalFree(sd);return {true,L"现有禁止删除 ACE 已生效，保持原样。"};}
    if(!enable && !hasExact){
        LocalFree(sd);
        return hasBroader?Result{false,L"存在非本工具格式的禁止删除 ACE；为防止误删其他权限，拒绝清除。"}:
            Result{true,L"没有本工具的禁止删除 ACE，无须变更。"};
    }
    const DWORD sidBytes=GetLengthSid(auth.data());
    const DWORD newAceBytes=sizeof(ACCESS_DENIED_ACE)-sizeof(DWORD)+sidBytes;
    const DWORD newSize=old->AclSize+(enable?newAceBytes:0);
    std::vector<BYTE> memory(newSize);
    auto* replacement=reinterpret_cast<PACL>(memory.data());
    if(!InitializeAcl(replacement,newSize,ACL_REVISION)){LocalFree(sd);return {false,L"初始化 DACL 失败。"};}
    if(enable && !AddAccessDeniedAceEx(replacement,ACL_REVISION,NO_INHERITANCE,
                                      DELETE|FILE_DELETE_CHILD,auth.data())){
        LocalFree(sd);return {false,L"添加拒绝删除 ACE 失败。"};
    }
    for(DWORD i=0;i<old->AceCount;++i){
        void* ace=nullptr;
        if(!GetAce(old,i,&ace)){LocalFree(sd);return {false,L"读取现有 ACE 失败。"};}
        if(!enable && IsDeleteAce(ace,auth.data(),true))continue;
        const auto* head=static_cast<const ACE_HEADER*>(ace);
        if(!AddAce(replacement,ACL_REVISION,MAXDWORD,ace,head->AceSize)){
            LocalFree(sd);return {false,L"保留原有 ACE 失败。"};
        }
    }
    // Do not alter the inheritance protection bit or other account permissions.
    err=SetNamedSecurityInfoW(const_cast<LPWSTR>(p.c_str()),SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION,nullptr,nullptr,replacement,nullptr);
    LocalFree(sd);
    return err==ERROR_SUCCESS?Result{true,enable?L"已添加独立禁止删除 ACE。":L"已移除独立禁止删除 ACE。"}:
        Result{false,L"写入删除拒绝规则失败："+ErrorText(err)};
}

Result Attributes(const std::wstring& p, DWORD flag, bool enabled) {
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return {false, L"读取属性失败：" + ErrorText(GetLastError())};
    DWORD updated = enabled ? (attr | flag) : (attr & ~flag);
    if (!SetFileAttributesW(p.c_str(), updated))
        return {false, L"修改属性失败：" + ErrorText(GetLastError())};
    return {true, L"目录属性更新成功。"};
}
Result RunIcacls(const std::wstring& path, const std::wstring& args) {
    wchar_t sys[MAX_PATH] = {};
    if (!GetSystemDirectoryW(sys, MAX_PATH)) return {false, L"无法定位系统目录。"};
    const std::wstring exe = std::wstring(sys) + L"\\icacls.exe";
    std::wstring cmd = L"\"" + exe + L"\" \"" + path + L"\" " + args;
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return {false, L"启动 icacls 失败：" + ErrorText(GetLastError())};
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE); // Runs off the UI thread.
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code != 0) return {false, L"icacls 失败，退出码：" + std::to_wstring(code)};
    return {true, L"命令执行成功。"};
}
bool ReparseInTree(const std::wstring& root, std::wstring& bad) {
    // Explicitly avoid silently operating across directory reparse points.
    std::vector<std::wstring> dirs(1, root);
    while (!dirs.empty()) {
        auto dir = dirs.back(); dirs.pop_back();
        WIN32_FIND_DATAW data = {};
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &data);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            auto child = dir + L"\\" + data.cFileName;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                bad = child;
                FindClose(h);
                return true;
            }
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs.push_back(child);
        } while (FindNextFileW(h, &data));
        FindClose(h);
    }
    return false;
}
} // namespace

bool IsAdministrator() {
    std::vector<BYTE> sid;
    if (!MakeSid(WinBuiltinAdministratorsSid, sid)) return false;
    BOOL member = FALSE;
    return CheckTokenMembership(nullptr, sid.data(), &member) && member;
}
Result PrepareDirectory(const std::wstring& path, bool createIfMissing) { return Guard(path, createIfMissing); }

Status ReadStatus(const std::wstring& input) {
    Status s;
    const auto p = Canonical(input);
    if (p.empty()) { s.error = L"无效目录路径"; return s; }
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) { s.error = L"目录不存在或无法访问"; return s; }
    s.exists = true;
    s.ntfs = IsNtfs(p);
    s.system = (attr & FILE_ATTRIBUTE_SYSTEM) != 0;
    s.hidden = (attr & FILE_ATTRIBUTE_HIDDEN) != 0;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD err = GetNamedSecurityInfoW(const_cast<LPWSTR>(p.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor);
    if (err != ERROR_SUCCESS) { s.error = L"读取 ACL 失败：" + ErrorText(err); return s; }
    SECURITY_DESCRIPTOR_CONTROL control = {};
    DWORD revision = 0;
    if (GetSecurityDescriptorControl(descriptor, &control, &revision))
        s.aclProtected = (control & SE_DACL_PROTECTED) != 0;
    std::vector<BYTE> auth;
    if (MakeSid(WinAuthenticatedUserSid, auth) && acl) {
        for (DWORD i = 0; i < acl->AceCount; ++i) {
            void* ace = nullptr;
            if (!GetAce(acl, i, &ace)) continue;
            ACE_HEADER* head = static_cast<ACE_HEADER*>(ace);
            if (head->AceType != ACCESS_DENIED_ACE_TYPE) continue;
            auto denied = static_cast<ACCESS_DENIED_ACE*>(ace);
            if (IsDeleteAce(ace, auth.data(), false)) s.denyDelete = true;
        }
    }
    LocalFree(descriptor);
    return s;
}


std::wstring DescribeAcl(const std::wstring& input) {
    auto path = Canonical(input);
    if (path.empty()) return L"无效目录路径。\r\n";
    std::wstring reason;
    if (!SafePath(path, reason)) return reason + L"\r\n";
    PSECURITY_DESCRIPTOR sd = nullptr;
    PACL dacl = nullptr;
    PSID owner = nullptr;
    DWORD err = GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
       OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl, nullptr, &sd);
    if (err != ERROR_SUCCESS) return L"读取 ACL 失败：" + ErrorText(err);
    auto sidText = [](PSID sid) -> std::wstring {
        if (!sid || !IsValidSid(sid)) return L"(none)";
        DWORD n=0,m=0; SID_NAME_USE kind{};
        LookupAccountSidW(nullptr,sid,nullptr,&n,nullptr,&m,&kind);
        std::vector<wchar_t> name(n+2), domain(m+2);
        if (LookupAccountSidW(nullptr,sid,name.data(),&n,domain.data(),&m,&kind)) {
            std::wstring dn(domain.data());
            return (dn.empty() ? L"" : dn+L"\\") + name.data();
        }
        LPWSTR stringSid=nullptr;
        std::wstring result=L"(unknown)";
        if (ConvertSidToStringSidW(sid,&stringSid) && stringSid) {
            result=stringSid; LocalFree(stringSid);
        }
        return result;
    };
    wchar_t hex[64]={};
    std::wstring report=L"FolderGuard 详细 NTFS ACL 报告\r\n";
    report+=L"--------------------------------------------------------\r\n";
    report+=L"路径：" + path + L"\r\n";
    report+=L"所有者：" + sidText(owner) + L"\r\n";
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision=0;
    if (GetSecurityDescriptorControl(sd,&control,&revision)) {
        report+=std::wstring(L"继承状态：") + ((control & SE_DACL_PROTECTED)?L"已关闭 (Protected)":L"已启用 (Inherited)") + L"\r\n";
    }
    if (!dacl) {
        report+=L"\r\nDACL: NULL（危险：通常允许任何账户访问）\r\n";
    } else {
        report+=L"DACL ACE 数量：" + std::to_wstring(dacl->AceCount) + L"\r\n\r\n";
        for (DWORD i=0;i<dacl->AceCount;++i) {
            void* ace=nullptr;
            if (!GetAce(dacl,i,&ace)) continue;
            auto* hd=static_cast<ACE_HEADER*>(ace);
            report+=L"#"+std::to_wstring(i+1)+L"   ";
            const bool allowed=hd->AceType==ACCESS_ALLOWED_ACE_TYPE;
            const bool denied=hd->AceType==ACCESS_DENIED_ACE_TYPE;
            if (!allowed && !denied) {
                report+=L"其他 ACE 类型："+std::to_wstring(hd->AceType)+L"\r\n";
                continue;
            }
            auto* common=static_cast<ACCESS_ALLOWED_ACE*>(ace);
            report+=allowed?L"ALLOW  ":L"DENY   ";
            report+=sidText(&common->SidStart)+L"\r\n";
            swprintf_s(hex, L"0x%08lX", static_cast<unsigned long>(common->Mask));
            report+=std::wstring(L"      权限掩码：")+hex;
            if (common->Mask & DELETE) report+=L"  [DELETE]";
            if (common->Mask & FILE_DELETE_CHILD) report+=L"  [DELETE_CHILD]";
            if (common->Mask & GENERIC_ALL) report+=L"  [GENERIC_ALL]";
            if (common->Mask & FILE_READ_DATA) report+=L"  [READ/LIST]";
            if (common->Mask & FILE_WRITE_DATA) report+=L"  [WRITE/CREATE]";
            report+=L"\r\n";
            report+=std::wstring(L"      来源：")+
               ((hd->AceFlags & INHERITED_ACE)?L"父目录继承":L"显式设置")+
               ((hd->AceFlags & CONTAINER_INHERIT_ACE)?L"  [子目录继承]":L"")+
               ((hd->AceFlags & OBJECT_INHERIT_ACE)?L"  [文件继承]":L"")+
               ((hd->AceFlags & INHERIT_ONLY_ACE)?L"  [仅继承]":L"") + L"\r\n";
        }
    }
    report+=L"\r\n注意：父目录 DELETE_CHILD 仍可能允许删除此目录。\r\n";
    report+=L"当前报告只读取顶层目录，不会修改 ACL，也不会遍历子文件。\r\n";
    if (sd) LocalFree(sd);
    return report;
}

Result Apply(const std::wstring& input, Action action) {
    const auto p = Canonical(input);
    auto guarded = Guard(p, false);
    if (!guarded.ok) return guarded;
    if (!IsAdministrator()) return {false, L"请以管理员身份启动 FolderGuard。"};
    Result r;
    switch (action) {
    case Action::AclProtectOn: return ToggleAclInheritance(p,true);
    case Action::AclProtectOff: return ToggleAclInheritance(p,false);
    case Action::DenyDeleteOn: return ToggleDeleteAce(p,true);
    case Action::DenyDeleteOff: return ToggleDeleteAce(p,false);
    case Action::SystemOn: return Attributes(p, FILE_ATTRIBUTE_SYSTEM, true);
    case Action::SystemOff: return Attributes(p, FILE_ATTRIBUTE_SYSTEM, false);
    case Action::HiddenOn: return Attributes(p, FILE_ATTRIBUTE_HIDDEN, true);
    case Action::HiddenOff: return Attributes(p, FILE_ATTRIBUTE_HIDDEN, false);
    case Action::QuickLock:
        r = LockRoot(p, false);
        if (!r.ok) return r;
        return Attributes(p, FILE_ATTRIBUTE_SYSTEM, true);
    case Action::QuickUnlock:
        r = RunIcacls(p, L"/inheritance:e /q");
        if (!r.ok) return r;
        return RunIcacls(p, L"/reset /q");
    case Action::FullLock: {
        std::wstring bad;
        if (ReparseInTree(p, bad)) return {false, L"发现链接/联接点，已中止：" + bad};
        r = LockRoot(p, true);
        if (!r.ok) return r;
        r = Attributes(p, FILE_ATTRIBUTE_SYSTEM, true);
        if (!r.ok) return r;
        // Only descendants: resetting root itself would remove the just-installed locked DACL.
        return RunIcacls(p + L"\\*", L"/reset /t /c /q");
    }
    case Action::FullUnlock:
        r = RunIcacls(p, L"/inheritance:e /q");
        if (!r.ok) return r;
        return RunIcacls(p, L"/reset /t /c /q");
    }
    return {false, L"未知操作。"};
}
} // namespace security
