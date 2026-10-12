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
$dir=Join-Path $PWD 'artifacts'
New-Item -ItemType Directory -Path $dir -Force|Out-Null
$exe=(Resolve-Path 'build/Release/FolderGuard.exe').Path
function Capture-Snapshot([string]$fileName,[string]$arguments) {
  $proc=$null
  try {
    if($arguments){$proc=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru}
    else{$proc=Start-Process -FilePath $exe -PassThru}
    $hwnd=[IntPtr]::Zero
    for($i=0;$i -lt 35;$i++){
      Start-Sleep -Milliseconds 300
      $proc.Refresh()
      if($proc.HasExited){throw "GUI process quit unexpectedly"}
      if($proc.MainWindowHandle -ne [IntPtr]::Zero){$hwnd=$proc.MainWindowHandle;break}
    }
    if($hwnd -eq [IntPtr]::Zero){throw 'GUI main HWND missing'}
    [CaptureApi]::ShowWindow($hwnd,5)|Out-Null
    [CaptureApi]::SetForegroundWindow($hwnd)|Out-Null
    Start-Sleep -Seconds 2
    $r=New-Object CaptureApi+RECT
    [CaptureApi]::GetWindowRect($hwnd,[ref]$r)|Out-Null
    $w=$r.Right-$r.Left;$h=$r.Bottom-$r.Top
    if($w -lt 500 -or $h -lt 400){throw "Window dimensions invalid: $($w)x$($h)"}
    $bmp=New-Object System.Drawing.Bitmap($w,$h)
    $graphics=[System.Drawing.Graphics]::FromImage($bmp)
    try {
      $dc=$graphics.GetHdc()
      try{$ok=[CaptureApi]::PrintWindow($hwnd,$dc,0)}finally{$graphics.ReleaseHdc($dc)}
      if(-not $ok){throw "PrintWindow returned false"}
      $bmp.Save((Join-Path $dir $fileName),[System.Drawing.Imaging.ImageFormat]::Png)
      Write-Host "[PASS] Captured $fileName ($($w)x$($h))"
    }finally{$graphics.Dispose();$bmp.Dispose()}
  }finally{
    if($proc -and !$proc.HasExited){Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue}
  }
}
try{
  Capture-Snapshot 'FolderGuard-Empty.png' ''
  $folder=Join-Path $env:TEMP ('FolderGuard-Capture-'+$PID)
  New-Item -ItemType Directory -Path $folder | Out-Null
  try {
    Capture-Snapshot 'FolderGuard-Selected.png' ('--path "'+$folder+'"')
  }finally {
    Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
  }
}catch{
  Write-Warning "GUI screenshot could not be completed: $_"
  $_ | Out-File (Join-Path $dir 'capture-error.txt')
}
