$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CaptureApi {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
}
'@
$dir = Join-Path $PWD 'artifacts'
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$exe = (Resolve-Path 'build/Release/FolderGuard.exe').Path
$p = $null
try {
 $p = Start-Process -FilePath $exe -PassThru
 $hwnd = [IntPtr]::Zero
 for($i=0;$i -lt 30;$i++){
  Start-Sleep -Milliseconds 500
  $p.Refresh()
  if($p.HasExited){throw "Process exited early: $($p.ExitCode)"}
  if($p.MainWindowHandle -ne [IntPtr]::Zero){$hwnd=$p.MainWindowHandle;break}
 }
 if($hwnd -eq [IntPtr]::Zero){throw 'Main window handle not found'}
 [CaptureApi]::ShowWindow($hwnd,5)|Out-Null
 [CaptureApi]::SetForegroundWindow($hwnd)|Out-Null
 Start-Sleep -Seconds 2
 $r=New-Object CaptureApi+RECT
 [CaptureApi]::GetWindowRect($hwnd,[ref]$r)|Out-Null
 $w=$r.Right-$r.Left;$h=$r.Bottom-$r.Top
 if($w -lt 400 -or $h -lt 300){throw "Invalid window size $w x $h"}
 $bmp=New-Object System.Drawing.Bitmap($w,$h)
 $g=[System.Drawing.Graphics]::FromImage($bmp)
 try{
  $dc=$g.GetHdc()
  try{$ok=[CaptureApi]::PrintWindow($hwnd,$dc,0)}
  finally{$g.ReleaseHdc($dc)}
  $bmp.Save((Join-Path $dir 'FolderGuard-PrintWindow.png'),[System.Drawing.Imaging.ImageFormat]::Png)
  Write-Host "PrintWindow=$ok Size=$w x $h"
  $g.CopyFromScreen($r.Left,$r.Top,0,0,[System.Drawing.Size]::new($w,$h))
  $bmp.Save((Join-Path $dir 'FolderGuard-Screen.png'),[System.Drawing.Imaging.ImageFormat]::Png)
  Write-Host 'Screenshot captured.'
 }finally{$g.Dispose();$bmp.Dispose()}
}catch{
 Write-Warning "Screenshot unavailable: $_"
 $_ | Out-File (Join-Path $dir 'capture-error.txt')
}finally{
 if($p -and !$p.HasExited){Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue}
}
