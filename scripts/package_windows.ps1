param(
  [Parameter(Mandatory = $true)][string]$BuildDir,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [Parameter(Mandatory = $false)][string]$Version,
  [Parameter(Mandatory = $false)][string]$Jre8,
  [Parameter(Mandatory = $false)][string]$Jre17,
  [Parameter(Mandatory = $false)][string]$Jre21
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$v = $Version
if ([string]::IsNullOrWhiteSpace($v)) {
  $v = $env:GITHUB_REF_NAME
}
if ([string]::IsNullOrWhiteSpace($v)) {
  $v = "dev"
}
if ($v.StartsWith("v")) {
  $v = $v.Substring(1)
}
$vSafe = $v -replace "[^0-9A-Za-z\.\-_]", "_"
$vi = $vSafe
if ($vi -match "^\d+(\.\d+){0,3}$") {
  $parts = $vi.Split(".")
  while ($parts.Length -lt 4) { $parts += "0" }
  if ($parts.Length -gt 4) { $parts = $parts[0..3] }
  $vi = ($parts -join ".")
} else {
  $vi = "0.0.0.0"
}

$appDir = Join-Path $OutDir "app"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $appDir
New-Item -ItemType Directory -Force -Path $appDir | Out-Null

$exe = Join-Path $BuildDir "BlockForge.exe"
if (!(Test-Path $exe)) { throw "BlockForge.exe not found at $exe" }

Copy-Item $exe (Join-Path $appDir "BlockForge.exe")

$windeployqt = $null
$cmd = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if ($cmd) { $windeployqt = $cmd.Source }

if ([string]::IsNullOrWhiteSpace($windeployqt) -and ![string]::IsNullOrWhiteSpace($env:Qt6_DIR)) {
  $qtBin = Join-Path $env:Qt6_DIR "..\\..\\..\\bin"
  $candidate = Join-Path $qtBin "windeployqt.exe"
  if (Test-Path $candidate) { $windeployqt = $candidate }
}

if ([string]::IsNullOrWhiteSpace($windeployqt)) {
  throw "windeployqt.exe not found (Qt6_DIR is not set and windeployqt is not on PATH)"
}

& $windeployqt --release --no-translations (Join-Path $appDir "BlockForge.exe")

$jreRoot = Join-Path $appDir "jre"
New-Item -ItemType Directory -Force -Path $jreRoot | Out-Null
if (![string]::IsNullOrWhiteSpace($Jre8) -and (Test-Path $Jre8)) {
  Copy-Item $Jre8 (Join-Path $jreRoot "8") -Recurse -Force
}
if (![string]::IsNullOrWhiteSpace($Jre17) -and (Test-Path $Jre17)) {
  Copy-Item $Jre17 (Join-Path $jreRoot "17") -Recurse -Force
}
if (![string]::IsNullOrWhiteSpace($Jre21) -and (Test-Path $Jre21)) {
  Copy-Item $Jre21 (Join-Path $jreRoot "21") -Recurse -Force
}

$staging = Join-Path $OutDir "staging"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $staging
New-Item -ItemType Directory -Force -Path $staging | Out-Null

$rootFolder = Join-Path $staging ("BlockForge-" + $vSafe)
New-Item -ItemType Directory -Force -Path $rootFolder | Out-Null
Copy-Item (Join-Path $appDir "*") $rootFolder -Recurse -Force

$portableZip = Join-Path $OutDir ("BlockForge-Portable-" + $vSafe + ".zip")
if (Test-Path $portableZip) { Remove-Item -Force $portableZip }
Compress-Archive -Path $rootFolder -DestinationPath $portableZip

choco install nsis -y --no-progress

$nsi = Join-Path $PSScriptRoot "..\\packaging\\nsis\\BlockForge.nsi"
$setupExe = Join-Path $OutDir ("BlockForge-Setup-" + $vSafe + ".exe")
& makensis.exe /DAPPDIR="$appDir" /DOUTFILE="$setupExe" /DVERSION="$vSafe" /DVIPRODUCTVERSION="$vi" $nsi

$shaPath = Join-Path $OutDir ("SHA256SUMS-" + $vSafe + ".txt")
if (Test-Path $shaPath) { Remove-Item -Force $shaPath }
Get-FileHash -Algorithm SHA256 $portableZip, $setupExe | ForEach-Object { "$($_.Hash)  $([System.IO.Path]::GetFileName($_.Path))" } | Out-File -FilePath $shaPath -Encoding ascii
