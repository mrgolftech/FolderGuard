#include <windows.h>
#include <iostream>
#include <aclapi.h>
#include <vector>
#include <string>
#include "Security.h"
namespace {
int fails=0,passes=0,skips=0;
void Check(bool ok, const wchar_t* name) {
 std::wcout << (ok?L"[PASS] ":L"[FAIL] ") << name << L"\n";
 if(ok) ++passes; else ++fails;
}
// AccessCheck evaluates the target folder security descriptor in isolation,
// unlike CreateFile(DELETE), which may also be granted via parent's DELETE_CHILD.
bool TargetAclDeniesDelete(const std::wstring& path) {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    PACL dacl=nullptr;
    PSID owner=nullptr,group=nullptr;
    DWORD err=GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
        &owner,&group,&dacl,nullptr,&descriptor);
    if(err!=ERROR_SUCCESS)return false;
    HANDLE primary=nullptr,impersonation=nullptr;
    bool blocked=false;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&primary) &&
       DuplicateToken(primary,SecurityImpersonation,&impersonation)){
        GENERIC_MAPPING mapping={FILE_GENERIC_READ,FILE_GENERIC_WRITE,FILE_GENERIC_EXECUTE,FILE_ALL_ACCESS};
        std::vector<BYTE> privileges(4096);
        DWORD size=static_cast<DWORD>(privileges.size());
        DWORD granted=0;BOOL allowed=FALSE;
        if(AccessCheck(descriptor,impersonation,DELETE,&mapping,
                       reinterpret_cast<PPRIVILEGE_SET>(privileges.data()),
                       &size,&granted,&allowed))
            blocked=!allowed;
    }
    if(impersonation)CloseHandle(impersonation);
    if(primary)CloseHandle(primary);
    LocalFree(descriptor);
    return blocked;
}
bool RootAclOnlyAdminSystem(const std::wstring& path) {
    PSECURITY_DESCRIPTOR sd=nullptr;PACL dacl=nullptr;
    if(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,
         DACL_SECURITY_INFORMATION,nullptr,nullptr,&dacl,nullptr,&sd)!=ERROR_SUCCESS)return false;
    BYTE admins[SECURITY_MAX_SID_SIZE]{},system[SECURITY_MAX_SID_SIZE]{};
    DWORD n1=sizeof(admins),n2=sizeof(system);
    bool valid=CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admins,&n1) &&
               CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&n2);
    bool hasAdmin=false,hasSystem=false;
    if(dacl && valid){
        for(DWORD i=0;i<dacl->AceCount;++i){
            void* ace=nullptr;
            if(!GetAce(dacl,i,&ace)){valid=false;break;}
            auto* hdr=static_cast<ACE_HEADER*>(ace);
            if(hdr->AceType!=ACCESS_ALLOWED_ACE_TYPE)continue;
            auto* rule=static_cast<ACCESS_ALLOWED_ACE*>(ace);
            PSID sid=&rule->SidStart;
            if(EqualSid(sid,admins))hasAdmin=true;
            else if(EqualSid(sid,system))hasSystem=true;
            else valid=false;
        }
    } else valid=false;
    if(sd)LocalFree(sd);
    return valid && hasAdmin && hasSystem;
}
void MakeFile(const std::wstring& p) {
 HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h!=INVALID_HANDLE_VALUE){DWORD n=0;WriteFile(h,"hello",5,&n,nullptr);CloseHandle(h);}
}
}
int wmain(){
 wchar_t temp[MAX_PATH]={}; GetTempPathW(MAX_PATH,temp);
 std::wstring root=std::wstring(temp)+L"FolderGuard-CI-"+std::to_wstring(GetCurrentProcessId());
 std::wstring child=root+L"\\child", file=child+L"\\sample.txt";
 Check(!security::PrepareDirectory(L"C:\\",false).ok,L"Reject volume root");
 Check(!security::PrepareDirectory(L"C:\\$RECYCLE.BIN",false).ok,L"Reject Windows recycle bin");
 auto prep=security::PrepareDirectory(root,true);
 Check(prep.ok,L"Create disposable NTFS folder");
 if(!prep.ok)return 2;
 CreateDirectoryW(child.c_str(),nullptr);MakeFile(file);
 auto s=security::ReadStatus(root);
 Check(s.exists && s.ntfs,L"Read NTFS status");
 bool admin=security::IsAdministrator();
 std::wcout << L"Elevated administrator token: " << (admin?L"yes":L"no") << L"\n";
 if(admin){
   auto a=security::Apply(root,security::Action::SystemOn);
   auto b=security::Apply(root,security::Action::HiddenOn);
   s=security::ReadStatus(root);
   Check(a.ok && b.ok && s.system && s.hidden,L"Set +S +H");
   a=security::Apply(root,security::Action::SystemOff);
   b=security::Apply(root,security::Action::HiddenOff);
   s=security::ReadStatus(root);
   Check(a.ok && b.ok && !s.system && !s.hidden,L"Clear -S -H");
   // CI uses real NTFS APIs on a disposable folder, never a mocked ACL.
   // Four GUI options must be independent (all off initially).
   auto init=security::ReadStatus(root);
   Check(!init.system && !init.hidden && !init.aclProtected && !init.denyDelete,
         L"Independent toggles: initial four flags off");
   a=security::Apply(root,security::Action::AclProtectOn);
   s=security::ReadStatus(root);
   Check(a.ok && s.aclProtected && !s.denyDelete && !s.system && !s.hidden,
         L"ACL only ON: protect inheritance without auto-deny or +S");
   Check(RootAclOnlyAdminSystem(root),
         L"ACL only ON: actual DACL allows exclusively Administrators/SYSTEM");
   a=security::Apply(root,security::Action::AclProtectOff);
   s=security::ReadStatus(root);
   Check(a.ok && !s.aclProtected && !s.denyDelete,
         L"ACL only OFF: restore inheritance without DACL reset");

   a=security::Apply(root,security::Action::DenyDeleteOn);
   s=security::ReadStatus(root);
   Check(a.ok && s.denyDelete && !s.aclProtected && !s.system && !s.hidden,
         L"Delete-deny only ON: add explicit rule while keeping inheritance");
   Check(TargetAclDeniesDelete(root),L"Real NTFS DACL AccessCheck denies target DELETE");
   // Diagnostic only: actual DELETE handles may succeed by parent's DELETE_CHILD.
   HANDLE removal=CreateFileW(root.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
       nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
   const DWORD removalError=removal==INVALID_HANDLE_VALUE?GetLastError():ERROR_SUCCESS;
   if(removal!=INVALID_HANDLE_VALUE)CloseHandle(removal);
   std::wcout<<L"[INFO] Actual DELETE handle allowed="
             <<(removalError==ERROR_SUCCESS?L"yes":L"no")
             <<L", error="<<removalError
             <<L" (parent DELETE_CHILD can grant deletion)\n";

   a=security::Apply(root,security::Action::DenyDeleteOff);
   s=security::ReadStatus(root);
   Check(a.ok && !s.denyDelete && !s.aclProtected,
         L"Delete-deny only OFF: inheritance and other ACEs retained");

   a=security::Apply(root,security::Action::DenyDeleteOn);
   b=security::Apply(root,security::Action::AclProtectOn);
   s=security::ReadStatus(root);
   Check(a.ok && b.ok && s.aclProtected && s.denyDelete,
         L"Combined ACL + deny controls work independently");
   a=security::Apply(root,security::Action::AclProtectOff);
   s=security::ReadStatus(root);
   Check(a.ok && !s.aclProtected && s.denyDelete,
         L"Disabling ACL protection preserves delete-deny ACE");
   a=security::Apply(root,security::Action::SystemOn);
   b=security::Apply(root,security::Action::HiddenOn);
   s=security::ReadStatus(root);
   Check(a.ok && b.ok && s.system && s.hidden && s.denyDelete && !s.aclProtected,
         L"+S and +H independently coexist with delete-deny");
   a=security::Apply(root,security::Action::DenyDeleteOff);
   s=security::ReadStatus(root);
   Check(a.ok && !s.denyDelete && s.system && s.hidden,
         L"Disabling delete-deny does not clear System or Hidden");
   a=security::Apply(root,security::Action::SystemOff);
   s=security::ReadStatus(root);
   Check(a.ok && !s.system && s.hidden,
         L"System OFF preserves Hidden");
   a=security::Apply(root,security::Action::HiddenOff);
   s=security::ReadStatus(root);
   Check(a.ok && !s.hidden && !s.system && !s.aclProtected && !s.denyDelete,
         L"Hidden OFF restores all four independent controls");
   Check(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES,
         L"Nested user data survived all four independent toggle cycles");

   // Unicode-path regression: every native API and icacls invocation must
   // preserve UTF-16 filenames (including CJK text and whitespace).
   const std::wstring unicodeDir=root+L"\\中文 文件夹-テスト";
   const std::wstring unicodeChild=unicodeDir+L"\\内部 子目录";
   const std::wstring unicodeFile=unicodeChild+L"\\资料样例.txt";
   Check(CreateDirectoryW(unicodeDir.c_str(),nullptr)!=0,L"Create Unicode directory with spaces");
   Check(CreateDirectoryW(unicodeChild.c_str(),nullptr)!=0,L"Create nested Unicode directory");
   MakeFile(unicodeFile);
   Check(GetFileAttributesW(unicodeFile.c_str())!=INVALID_FILE_ATTRIBUTES,
         L"Create Unicode-named test file");
   auto unicodeStatus=security::ReadStatus(unicodeDir);
   Check(unicodeStatus.exists && unicodeStatus.ntfs,L"Read Unicode NTFS folder state");
   // A pre-existing protected DACL is NOT proof of FolderGuard's restrictive policy.
   PSECURITY_DESCRIPTOR inheritedSd=nullptr;
   PACL inheritedAcl=nullptr;
   DWORD aclRead=GetNamedSecurityInfoW(const_cast<LPWSTR>(unicodeDir.c_str()),
       SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&inheritedAcl,nullptr,&inheritedSd);
   DWORD aclWrite=ERROR_INVALID_DATA;
   if(aclRead==ERROR_SUCCESS){
       auto mutablePath=unicodeDir;
       aclWrite=SetNamedSecurityInfoW(&mutablePath[0],SE_FILE_OBJECT,
           DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,
           nullptr,nullptr,inheritedAcl,nullptr);
   }
   if(inheritedSd)LocalFree(inheritedSd);
   Check(aclRead==ERROR_SUCCESS && aclWrite==ERROR_SUCCESS,
         L"Create pre-protected but not FolderGuard-locked ACL");
   unicodeStatus=security::ReadStatus(unicodeDir);
   Check(!unicodeStatus.aclProtected,
         L"Do not mistake inheritance-disabled Unicode ACL for actual lock");
   a=security::Apply(unicodeDir,security::Action::AclProtectOn);
   unicodeStatus=security::ReadStatus(unicodeDir);
   Check(a.ok && unicodeStatus.aclProtected && RootAclOnlyAdminSystem(unicodeDir),
         L"Unicode ACL ON sets verified Administrators/SYSTEM-only root DACL");
   b=security::Apply(unicodeDir,security::Action::AclProtectOff);
   unicodeStatus=security::ReadStatus(unicodeDir);
   Check(b.ok && !unicodeStatus.aclProtected,L"Unicode ACL OFF restores inheritance");
   a=security::Apply(unicodeDir,security::Action::DenyDeleteOn);
   unicodeStatus=security::ReadStatus(unicodeDir);
   Check(a.ok && unicodeStatus.denyDelete && TargetAclDeniesDelete(unicodeDir),
         L"Unicode delete-deny ACE actually denies DELETE");
   b=security::Apply(unicodeDir,security::Action::DenyDeleteOff);
   Check(b.ok && !security::ReadStatus(unicodeDir).denyDelete,
         L"Unicode delete-deny OFF preserves other ACL state");
   a=security::Apply(unicodeDir,security::Action::SystemOn);
   b=security::Apply(unicodeDir,security::Action::HiddenOn);
   unicodeStatus=security::ReadStatus(unicodeDir);
   Check(a.ok && b.ok && unicodeStatus.system && unicodeStatus.hidden,
         L"Unicode path supports both attribute switches");
   a=security::Apply(unicodeDir,security::Action::SystemOff);
   b=security::Apply(unicodeDir,security::Action::HiddenOff);
   Check(a.ok && b.ok && !security::ReadStatus(unicodeDir).system &&
         !security::ReadStatus(unicodeDir).hidden,
         L"Unicode path attribute switches can be reset");
   a=security::Apply(unicodeDir,security::Action::FullLock);
   unicodeStatus=security::ReadStatus(unicodeDir);
   Check(a.ok && unicodeStatus.aclProtected &&
         GetFileAttributesW(unicodeFile.c_str())!=INVALID_FILE_ATTRIBUTES,
         L"Unicode full lock preserves nested CJK file");
   b=security::Apply(unicodeDir,security::Action::FullUnlock);
   unicodeStatus=security::ReadStatus(unicodeDir);
   Check(b.ok && !unicodeStatus.aclProtected,
         L"Unicode full unlock resets directory via icacls.exe");
   Check(DeleteFileW(unicodeFile.c_str())!=0 &&
         RemoveDirectoryW(unicodeChild.c_str())!=0 &&
         RemoveDirectoryW(unicodeDir.c_str())!=0,
         L"Clean up Unicode NTFS test tree");
 
   a=security::Apply(root,security::Action::QuickLock);
   s=security::ReadStatus(root);
   Check(a.ok && s.aclProtected && s.denyDelete && s.system,L"Quick lock: ACL protected, deny delete, +S");
   auto aclReport=security::DescribeAcl(root);
   Check(aclReport.find(L"DACL ACE")!=std::wstring::npos &&
         aclReport.find(L"DENY")!=std::wstring::npos &&
         aclReport.find(L"DELETE")!=std::wstring::npos,
         L"ACL detail report includes ACE, DENY and DELETE rights");
   b=security::Apply(root,security::Action::QuickUnlock);
   s=security::ReadStatus(root);
   Check(b.ok && !s.aclProtected && !s.denyDelete && s.system,L"Quick unlock: root inheritance restored, +S retained");
   a=security::Apply(root,security::Action::FullLock);
   s=security::ReadStatus(root);
   Check(a.ok && s.aclProtected && s.denyDelete,L"Full lock: root ACL protected");
   Check(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES,L"Full lock preserves test file");
   b=security::Apply(root,security::Action::FullUnlock);
   s=security::ReadStatus(root);
   Check(b.ok && !s.aclProtected && !s.denyDelete,L"Full unlock restores inherited ACL");
 }else{
   ++skips;
   std::wcout << L"[SKIP] ACL mutation tests: runner not elevated\n";
 }
 DWORD attr=GetFileAttributesW(root.c_str());
 if(attr!=INVALID_FILE_ATTRIBUTES)SetFileAttributesW(root.c_str(),attr&~(FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_HIDDEN));
 DeleteFileW(file.c_str());RemoveDirectoryW(child.c_str());
 Check(RemoveDirectoryW(root.c_str())!=0,L"Delete disposable test directory");
 std::wcout<<L"RESULT pass="<<passes<<L" fail="<<fails<<L" skip="<<skips<<L"\n";
 return fails?1:0;
}