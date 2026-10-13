#pragma once
#include <string>

namespace security {

enum class Action {
    QuickLock, QuickUnlock, FullLock, FullUnlock,
    SystemOn, SystemOff, HiddenOn, HiddenOff,
    AclProtectOn, AclProtectOff, DenyDeleteOn, DenyDeleteOff
};
struct Status {
    bool exists = false;
    bool ntfs = false;
    bool system = false;
    bool hidden = false;
    // Raw Windows inheritance flag, distinct from verified FolderGuard policy.
    bool aclInheritanceDisabled = false;
    bool aclProtected = false;
    bool denyDelete = false;
    std::wstring error;
};
struct Result {
    bool ok = false;
    std::wstring message;
};

bool IsAdministrator();
Result PrepareDirectory(const std::wstring& path, bool createIfMissing);
Status ReadStatus(const std::wstring& path);
// Human-readable root ACL report; read-only, no changes to file permissions.
std::wstring DescribeAcl(const std::wstring& path);
Result Apply(const std::wstring& path, Action action);
}
