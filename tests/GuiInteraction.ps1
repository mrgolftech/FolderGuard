$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class NativeUiTest {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetDlgItem(IntPtr h,int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowTextW(IntPtr h,string text);
  [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]
  public static extern IntPtr GetText(IntPtr hwnd,uint msg,IntPtr wParam,StringBuilder buffer);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int cmd);
  [DllImport("user32.dll")] public static extern bool InvalidateRect(IntPtr h,IntPtr rect,bool erase);
  [DllImport("user32.dll")] public static extern bool UpdateWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string klass,string title);
  public static void Click(IntPtr hwnd,int x,int y){
    Rect r; if(!GetClientRect(hwnd,out r)) throw new Exception("GetClientRect failed");
    int px=(int)Math.Round(x*(r.Right-r.Left)/1180.0);
    int py=(int)Math.Round(y*(r.Bottom-r.Top)/820.0);
    int packed=((py&0xffff)<<16)|(px&0xffff);
    SendMessageW(hwnd,0x201,new IntPtr(1),new IntPtr(packed));
    SendMessageW(hwnd,0x202,IntPtr.Zero,new IntPtr(packed));
  }
}
'@
$path=Join-Path $env:TEMP ('FolderGuard-GUI-Test-'+$PID)
New-Item -ItemType Directory -Path $path | Out-Null
$exe=(Resolve-Path 'build/Release/FolderGuard.exe').Path
$process=$null
try {
  $process=Start-Process -FilePath $exe -ArgumentList ('--path "'+$path+'"') -PassThru
  $hwnd=[IntPtr]::Zero
  for($i=0;$i -lt 30;$i++){
    Start-Sleep -Milliseconds 300
    $process.Refresh()
    if($process.HasExited){throw 'GUI terminated before opening window'}
    if($process.MainWindowHandle -ne [IntPtr]::Zero){$hwnd=$process.MainWindowHandle;break}
  }
  if($hwnd -eq [IntPtr]::Zero){throw 'GUI main window was not found'}
  $edit=[NativeUiTest]::GetDlgItem($hwnd,103)
  if($edit -eq [IntPtr]::Zero){throw 'Path edit control ID 103 missing'}
  [NativeUiTest]::ShowWindow($hwnd,5)|Out-Null
  [NativeUiTest]::InvalidateRect($hwnd,[IntPtr]::Zero,$false)|Out-Null
  [NativeUiTest]::UpdateWindow($hwnd)|Out-Null
  Start-Sleep -Milliseconds 1200
  Start-Sleep -Milliseconds 300
  $buffer=[System.Text.StringBuilder]::new(1024)
  [NativeUiTest]::GetText($edit,0x000D,[IntPtr]1024,$buffer)|Out-Null
  Write-Host "[TRACE] GUI text='$buffer' expected='$path'"
  if([string]$buffer -ne $path){throw 'GUI edit field was not updated to requested path'}
  Write-Host "[TRACE] target=$path clientWindow=$hwnd; initialAttr=$([IO.File]::GetAttributes($path))"
  function Save-GuiSnapshot {
    try {
      [NativeUiTest+Rect]$bounds=New-Object NativeUiTest+Rect
      [NativeUiTest]::GetWindowRect($hwnd,[ref]$bounds)|Out-Null
      $w=$bounds.Right-$bounds.Left;$h=$bounds.Bottom-$bounds.Top
      $bmp=New-Object System.Drawing.Bitmap($w,$h)
      $g=[System.Drawing.Graphics]::FromImage($bmp)
      $dc=$g.GetHdc()
      try { [NativeUiTest]::PrintWindow($hwnd,$dc,0)|Out-Null } finally { $g.ReleaseHdc($dc) }
      New-Item -ItemType Directory -Path 'artifacts' -Force|Out-Null
      $bmp.Save((Join-Path (Resolve-Path 'artifacts').Path 'GUI-attribute-debug.png'),[System.Drawing.Imaging.ImageFormat]::Png)
      $g.Dispose();$bmp.Dispose()
      Write-Host '[TRACE] GUI debug screenshot saved'
    }catch {Write-Warning "Debug screenshot failed: $_"}
  }
  function Wait-Flag($flag,$enabled) {
    for($i=0;$i -lt 50;$i++){
      Start-Sleep -Milliseconds 150
      $got=([IO.File]::GetAttributes($path) -band $flag) -ne 0
      if($got -eq $enabled){return}
    }
    Save-GuiSnapshot
    throw "Attribute $flag did not change to $enabled; actual=$([IO.File]::GetAttributes($path))"
  }
  [NativeUiTest]::Click($hwnd,450,708)
  Wait-Flag ([IO.FileAttributes]::System) $true
  Write-Host '[PASS] GUI System +S'
  [NativeUiTest]::Click($hwnd,450,708)
  Wait-Flag ([IO.FileAttributes]::System) $false
  Write-Host '[PASS] GUI System -S'
  [NativeUiTest]::Click($hwnd,650,708)
  Wait-Flag ([IO.FileAttributes]::Hidden) $true
  Write-Host '[PASS] GUI Hidden +H'
  [NativeUiTest]::Click($hwnd,650,708)
  Wait-Flag ([IO.FileAttributes]::Hidden) $false
  Write-Host '[PASS] GUI Hidden -H'
  [NativeUiTest]::Click($hwnd,927,70)
  $detail=[IntPtr]::Zero
  for($i=0;$i -lt 20;$i++){
    Start-Sleep -Milliseconds 150
    $detail=[NativeUiTest]::FindWindowW('FolderGuard.AclViewer',$null)
    if($detail -ne [IntPtr]::Zero){break}
  }
  if($detail -eq [IntPtr]::Zero){throw 'ACL details popup missing after GUI click'}
  if([NativeUiTest]::GetDlgItem($detail,901) -eq [IntPtr]::Zero){throw 'ACL report text control missing'}
  [NativeUiTest]::PostMessageW($detail,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Write-Host '[PASS] GUI ACL detail popup'
  Write-Host '[PASS] GUI interactive smoke completed'
}finally{
  if($process -and !$process.HasExited){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue}
  if(Test-Path -LiteralPath $path){
    $attr=[IO.File]::GetAttributes($path)
    [IO.File]::SetAttributes($path,($attr -band (-bnot ([IO.FileAttributes]::System -bor [IO.FileAttributes]::Hidden))))
    Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
  }
}
