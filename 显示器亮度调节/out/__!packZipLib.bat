@echo off
cd /d "%~dp0"
set FILES='index.html','css','js'
set "TARGET_ZIP=res.zip"

echo 正在打包并混淆 %TARGET_ZIP% ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Add-Type -AssemblyName 'System.IO.Compression.FileSystem'; $zipPath = '%TARGET_ZIP%'; if (Test-Path $zipPath) { Remove-Item $zipPath -Force }; $zip = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create'); foreach ($it in @('index.html','css','js')) { if (Test-Path $it) { if ((Get-Item $it) -is [System.IO.DirectoryInfo]) { Get-ChildItem -Path $it -Recurse | ? { -not $_.PSIsContainer } | %% { $rel = (Resolve-Path $_.FullName -Relative).TrimStart('.\'); [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $rel) } } else { [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Resolve-Path $it), (Split-Path $it -Leaf)) } } }; $zip.Dispose(); $b = [System.IO.File]::ReadAllBytes($zipPath); $cd = [BitConverter]::ToUInt32($b, $b.Length - 6); $b[0] = $b[0] -bxor 0xA5; for ($i = $cd; $i -lt ($b.Length - 6); $i++) { $b[$i] = $b[$i] -bxor 0xA5 }; [System.IO.File]::WriteAllBytes($zipPath, $b);"

echo 开始编译到lib库 ...
bin2Lib.exe -cl "%~dp0..\BEMod\R.stb"
