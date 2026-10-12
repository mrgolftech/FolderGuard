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
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string klass,string caption);
  public static void Click(IntPtr hwnd,int x,int y) {
    Rect r;
    if(!GetClientRect(hwnd,out r))throw new Exception("GetClientRect");
    int px=(int)Math.Round(x*(r.Right-r.Left)/1000.0);
    int py=(int)Math.Round(y*(r.Bottom-r.Top)/680.0);
    int lp=((py&65535)<<16)|(px&65535);
    SendMessageW(hwnd,0x201,new IntPtr(1),new IntPtr(lp));
    SendMessageW(hwnd,0x202,IntPtr.Zero,new IntPtr(lp));
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
  [NativeUiTest]::Click($hwnd,463,483) # select System +S; no FS modification yet
  [NativeUiTest]::Click($hwnd,748,483) # select Hidden +H
  Start-Sleep -Milliseconds 250
  $attrs=[IO.File]::GetAttributes($path)
  if(($attrs -band ([IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System)) -ne 0){
    throw 'Options modified NTFS before Apply was clicked'
  }
  Write-Host '[PASS] options are staged until Apply'
  [NativeUiTest]::Click($hwnd,500,587)
  Assert-Flag ([IO.FileAttributes]::System) $true
  Assert-Flag ([IO.FileAttributes]::Hidden) $true
  Dismiss-Result
  Write-Host '[PASS] consolidated Apply sets +S and +H and reports result'
  [NativeUiTest]::Click($hwnd,890,65) # refresh status: must re-adopt real flags
  Start-Sleep -Milliseconds 250
  [NativeUiTest]::Click($hwnd,463,483)
  [NativeUiTest]::Click($hwnd,748,483)
  [NativeUiTest]::Click($hwnd,500,587)
  Assert-Flag ([IO.FileAttributes]::System) $false
  Assert-Flag ([IO.FileAttributes]::Hidden) $false
  Dismiss-Result
  Write-Host '[PASS] refresh synchronizes staged toggles, Apply removes +S and +H'
  Write-Host '[PASS] compact GUI interaction smoke'
}finally{
  if($process -and !$process.HasExited){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue}
  if(Test-Path -LiteralPath $path){
    $a=[IO.File]::GetAttributes($path)
    [IO.File]::SetAttributes($path,($a -band (-bnot ([IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System))))
    Remove-Item -LiteralPath $path -Force -Recurse -ErrorAction SilentlyContinue
  }
}
