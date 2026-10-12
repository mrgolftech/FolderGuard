#include <windows.h>
#include <iostream>
#include <string>
#include "Security.h"
namespace {
int fails=0,passes=0,skips=0;
void Check(bool ok, const wchar_t* name) {
 std::wcout << (ok?L"[PASS] ":L"[FAIL] ") << name << L"\n";
 if(ok) ++passes; else ++fails;
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
   a=security::Apply(root,security::Action::QuickLock);
   s=security::ReadStatus(root);
   Check(a.ok && s.aclProtected && s.denyDelete && s.system,L"Quick lock: ACL protected, deny delete, +S");
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