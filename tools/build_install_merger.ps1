param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOutput = Join-Path $taskRoot 'Portable/InitialInstallMerger'
$taskCompiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (-not (Test-Path -LiteralPath $taskCompiler)) { throw 'Windows .NET Framework 4.x C# compiler not found.' }
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
$taskExe = Join-Path $taskOutput 'InitialInstallMerger.exe'
& $taskCompiler /nologo /utf8output /codepage:65001 /langversion:5 /optimize+ /target:winexe /platform:x64 /reference:System.Windows.Forms.dll /reference:System.Drawing.dll "/out:$taskExe" (Join-Path $PSScriptRoot 'initial_install_merger/InitialInstallMerger.cs')
if ($LASTEXITCODE -ne 0) { throw 'C# compilation failed.' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'initial_install_merger/README.txt') -Destination (Join-Path $taskOutput 'README.txt')
$taskFirmware = Join-Path $taskOutput 'firmware'
New-Item -ItemType Directory -Force -Path $taskFirmware | Out-Null
foreach ($taskName in @('fsbl_universal.hex', 'agent.hex', 'appli.hex')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot "Binary/compat/$taskName") -Destination (Join-Path $taskFirmware $taskName)
}
$taskArchive = Join-Path $taskRoot 'Portable/InitialInstallMerger.zip'
Compress-Archive -LiteralPath @($taskExe, (Join-Path $taskOutput 'README.txt'), $taskFirmware) -DestinationPath $taskArchive -Force
Write-Output "Built portable Windows x64 tool: $taskExe"
Write-Output "Portable ZIP: $taskArchive"
