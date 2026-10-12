$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NativeUiTest {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] public static extern IntPtr GetText(IntPtr h,uint msg,IntPtr size,StringBuilder sb);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int cmd);
  [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]
  public static extern IntPtr SetText(IntPtr h,uint msg,IntPtr unused,string text);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string klass,string caption);
  public static void ClickChild(IntPtr parent,int id) {
    IntPtr child=GetDlgItem(parent,id);
    if(child==IntPtr.Zero)throw new Exception("Missing control ID "+id);
    SendMessageW(child,0xF5,IntPtr.Zero,IntPtr.Zero); // BM_CLICK on native checkbox/button
  }
  public static void ClickApply(IntPtr hwnd) {
    Rect r;
    if(!GetClientRect(hwnd,out r))throw new Exception("GetClientRect");
    int px=(r.Right-r.Left)/2;
    int py=(int)Math.Round((r.Bottom-r.Top)*408.0/470.0);
    int lp=((py&65535)<<16)|(px&65535);
    PostMessageW(hwnd,0x201,new IntPtr(1),new IntPtr(lp));
    PostMessageW(hwnd,0x202,IntPtr.Zero,new IntPtr(lp));
  }
}
'@
$exe=(Resolve-Path 'build/Release/FolderGuard.exe').Path
$path=Join-Path $env:TEMP ('FolderGuard-GUI-Test-'+$PID)
New-Item -ItemType Directory -Path $path|Out-Null
$process=$null
function Assert-Flag($flag,[bool]$expected) {
  for($i=0;$i -lt 60;$i++){
    Start-Sleep -Milliseconds 150
    $actual=([IO.File]::GetAttributes($path) -band $flag) -ne 0
    if($actual -eq $expected){return}
  }
  throw "Attribute $flag expected=$expected actual=$([IO.File]::GetAttributes($path))"
}
function Dismiss-Result {
  $dialog=[IntPtr]::Zero
  for($i=0;$i -lt 40;$i++){
    Start-Sleep -Milliseconds 150
    $dialog=[NativeUiTest]::FindWindowW('#32770','FolderGuard · 应用设置结果')
    if($dialog -ne [IntPtr]::Zero){break}
  }
  if($dialog -eq [IntPtr]::Zero){throw 'Result modal did not appear'}
  [NativeUiTest]::PostMessageW($dialog,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Start-Sleep -Milliseconds 200
}
# Verify fresh launches are empty and inoperative until a directory is selected.
$emptyProcess=$null
try {
  $emptyProcess=Start-Process -FilePath $exe -PassThru
  $emptyHwnd=[IntPtr]::Zero
  for($i=0;$i -lt 35;$i++){
    Start-Sleep -Milliseconds 200
    $emptyProcess.Refresh()
    if($emptyProcess.HasExited){throw 'Empty GUI startup unexpectedly exited'}
    if($emptyProcess.MainWindowHandle -ne [IntPtr]::Zero){$emptyHwnd=$emptyProcess.MainWindowHandle;break}
  }
  if($emptyHwnd -eq [IntPtr]::Zero){throw 'Empty GUI startup window missing'}
  $emptyEdit=[NativeUiTest]::GetDlgItem($emptyHwnd,103)
  $emptyBuffer=[System.Text.StringBuilder]::new(1024)
  [NativeUiTest]::GetText($emptyEdit,0xD,[IntPtr]1024,$emptyBuffer)|Out-Null
  if($emptyBuffer.Length -ne 0){throw "Default path not empty: $emptyBuffer"}
  foreach($id in @(105,106,107,108,109)){
    $child=[NativeUiTest]::GetDlgItem($emptyHwnd,$id)
    if($child -eq [IntPtr]::Zero){throw "Native child control $id missing"}
    if([NativeUiTest]::IsWindowEnabled($child)){throw "Control $id enabled before folder load"}
  }
  [NativeUiTest]::ClickApply($emptyHwnd) # disabled Apply
  Start-Sleep -Milliseconds 350
  if([NativeUiTest]::FindWindowW('#32770','FolderGuard · 应用设置结果') -ne [IntPtr]::Zero){
    throw 'Apply was executable without loading a directory'
  }
  Write-Host '[PASS] empty initial folder; native refresh, settings and Apply disabled'
  # SetWindowText cannot update a child edit of another process reliably.
  # WM_SETTEXT is marshalled by Windows for the native EDIT control.
  [NativeUiTest]::SetText($emptyEdit,0x000C,[IntPtr]::Zero,$path)|Out-Null
  $typed=[System.Text.StringBuilder]::new(1024)
  [NativeUiTest]::GetText($emptyEdit,0xD,[IntPtr]1024,$typed)|Out-Null
  if([string]$typed -ne $path){throw "Pasted path does not match: '$typed' vs '$path'"}
  # Deliver Enter directly to the native edit window; not via old self-drawn hit zones.
  [NativeUiTest]::SendMessageW($emptyEdit,0x100,[IntPtr]13,[IntPtr]0)|Out-Null
  $loaded=$false
  for($i=0;$i -lt 40;$i++){
    Start-Sleep -Milliseconds 100
    if([NativeUiTest]::IsWindowEnabled([NativeUiTest]::GetDlgItem($emptyHwnd,105))){$loaded=$true;break}
  }
  if(!$loaded){throw 'Enter in path edit did not load the folder'}
  Write-Host '[PASS] Enter key loads a manually entered folder without Load button'
}finally{
  if($emptyProcess -and !$emptyProcess.HasExited){Stop-Process -Id $emptyProcess.Id -Force -ErrorAction SilentlyContinue}
}
try {
  $process=Start-Process -FilePath $exe -ArgumentList ('--path "'+$path+'"') -PassThru
  $hwnd=[IntPtr]::Zero
  for($i=0;$i -lt 40;$i++){
    Start-Sleep -Milliseconds 200
    $process.Refresh()
    if($process.HasExited){throw 'GUI quit before showing window'}
    if($process.MainWindowHandle -ne [IntPtr]::Zero){$hwnd=$process.MainWindowHandle;break}
  }
  if($hwnd -eq [IntPtr]::Zero){throw 'Main window missing'}
  [NativeUiTest]::ShowWindow($hwnd,5)|Out-Null
  [NativeUiTest]::UpdateWindow($hwnd)|Out-Null
  Start-Sleep -Seconds 1
  $edit=[NativeUiTest]::GetDlgItem($hwnd,103)
  if($edit -eq [IntPtr]::Zero){throw 'Native directory edit is missing'}
  $buffer=[System.Text.StringBuilder]::new(1024)
  [NativeUiTest]::GetText($edit,0xD,[IntPtr]1024,$buffer)|Out-Null
  if([string]$buffer -ne $path){throw "Path mismatch: $buffer expected $path"}
  Write-Host '[PASS] --path initializes selected folder'
  function Accept-Confirm {
    $confirm=[IntPtr]::Zero
    for($i=0;$i -lt 40;$i++){
      Start-Sleep -Milliseconds 150
      $confirm=[NativeUiTest]::FindWindowW('#32770','权限变更确认')
      if($confirm -ne [IntPtr]::Zero){break}
    }
    if($confirm -eq [IntPtr]::Zero){throw 'Permission confirmation popup missing'}
    [NativeUiTest]::PostMessageW([NativeUiTest]::GetDlgItem($confirm,6),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  }
  function Assert-ACL([bool]$expected) {
    for($i=0;$i -lt 70;$i++){
      Start-Sleep -Milliseconds 150
      if((Get-Acl -LiteralPath $path).AreAccessRulesProtected -eq $expected){return}
    }
    throw "ACL protected expected=$expected"
  }
  function Has-Deny {
    $sid='S-1-5-11'
    $required=[int][System.Security.AccessControl.FileSystemRights]::Delete -bor
              [int][System.Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles
    foreach($rule in (Get-Acl -LiteralPath $path).Access) {
      $rid=$rule.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value
      if($rid -eq $sid -and
         $rule.AccessControlType -eq [System.Security.AccessControl.AccessControlType]::Deny -and
         !$rule.IsInherited -and
         (([int]$rule.FileSystemRights -band $required) -eq $required)) {return $true}
    }
    return $false
  }
  function Assert-Deny([bool]$expected) {
    for($i=0;$i -lt 70;$i++){
      Start-Sleep -Milliseconds 150
      if((Has-Deny) -eq $expected){return}
    }
    throw "Delete-deny ACE expected=$expected actual=$(Has-Deny)"
  }
  function Assert-Checked([int]$id,[bool]$expected) {
    $ctrl=[NativeUiTest]::GetDlgItem($hwnd,$id)
    $actual=[NativeUiTest]::SendMessageW($ctrl,0xF0,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -eq 1
    if($actual -ne $expected){throw "Checkbox $id state expected=$expected actual=$actual"}
  }
  foreach($id in @(106,107,108,109)){Assert-Checked $id $false}

  # 1: independently stage and apply deletion denial, no implicit ACL or System.
  [NativeUiTest]::ClickChild($hwnd,109)
  if(Has-Deny){throw 'Deny ACE changed before Apply'}
  [NativeUiTest]::ClickApply($hwnd)
  Accept-Confirm
  Assert-Deny $true; Assert-ACL $false
  Assert-Flag ([IO.FileAttributes]::System) $false
  Assert-Flag ([IO.FileAttributes]::Hidden) $false
  Dismiss-Result
  Assert-Checked 109 $true; Assert-Checked 106 $false
  Write-Host '[PASS] standalone delete-deny switch sets only explicit deny ACE'

  # 2: System and Hidden are independent in real NTFS (not GUI-only fake state).
  [NativeUiTest]::ClickChild($hwnd,108)
  [NativeUiTest]::ClickApply($hwnd)
  Assert-Flag ([IO.FileAttributes]::Hidden) $true
  Dismiss-Result
  Assert-Deny $true; Assert-Flag ([IO.FileAttributes]::System) $false
  [NativeUiTest]::ClickChild($hwnd,107)
  [NativeUiTest]::ClickApply($hwnd)
  Assert-Flag ([IO.FileAttributes]::System) $true
  Dismiss-Result
  Assert-Checked 107 $true; Assert-Checked 108 $true
  Write-Host '[PASS] System +S and Hidden +H independently persisted'

  # 3: ACL inheritance switch is independent and preserves denial + attributes.
  [NativeUiTest]::ClickChild($hwnd,106)
  [NativeUiTest]::ClickApply($hwnd)
  Accept-Confirm
  Assert-ACL $true
  Dismiss-Result
  Assert-Deny $true
  Assert-Flag ([IO.FileAttributes]::System) $true
  Assert-Flag ([IO.FileAttributes]::Hidden) $true
  [NativeUiTest]::ClickChild($hwnd,106)
  [NativeUiTest]::ClickApply($hwnd)
  Accept-Confirm
  Assert-ACL $false
  Dismiss-Result
  Assert-Deny $true
  Write-Host '[PASS] ACL toggle does not affect delete ACE or attributes'

  # 4: delete-deny can be switched off without resetting remaining settings.
  [NativeUiTest]::ClickChild($hwnd,109)
  [NativeUiTest]::ClickApply($hwnd)
  Accept-Confirm
  Assert-Deny $false
  Dismiss-Result
  Assert-Flag ([IO.FileAttributes]::System) $true
  Assert-Flag ([IO.FileAttributes]::Hidden) $true
  Assert-Checked 109 $false
  Write-Host '[PASS] delete-deny OFF preserves attributes and ACL inheritance'

  # 5: disable two attribute flags, refresh and verify controls match disk.
  [NativeUiTest]::ClickChild($hwnd,107)
  [NativeUiTest]::ClickChild($hwnd,108)
  [NativeUiTest]::ClickApply($hwnd)
  Assert-Flag ([IO.FileAttributes]::System) $false
  Assert-Flag ([IO.FileAttributes]::Hidden) $false
  Dismiss-Result
  [NativeUiTest]::ClickChild($hwnd,105)
  Start-Sleep -Milliseconds 250
  foreach($id in @(106,107,108,109)){Assert-Checked $id $false}
  if(!(Test-Path -LiteralPath $path)){throw 'Protected directory was removed unexpectedly'}
  Write-Host '[PASS] all four independent switches round-trip and refresh from real NTFS'
  Write-Host '[PASS] compact GUI interaction smoke'

}finally{
  if($process -and !$process.HasExited){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue}
  if(Test-Path -LiteralPath $path){
    $a=[IO.File]::GetAttributes($path)
    [IO.File]::SetAttributes($path,($a -band (-bnot ([IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System))))
    Remove-Item -LiteralPath $path -Force -Recurse -ErrorAction SilentlyContinue
  }
}
