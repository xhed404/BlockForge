param(
  [Parameter(Mandatory = $true)][string]$BuildDir,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [Parameter(Mandatory = $false)][string]$Version,
  [Parameter(Mandatory = $false)][string]$Jre8,
  [Parameter(Mandatory = $false)][string]$Jre17,
  [Parameter(Mandatory = $false)][string]$Jre21
)

$ErrorActionPreference = "Stop"

$cwd = Get-Location
$buildDirAbs = [System.IO.Path]::GetFullPath((Join-Path $cwd $BuildDir))
$outDirAbs = [System.IO.Path]::GetFullPath((Join-Path $cwd $OutDir))

New-Item -ItemType Directory -Force -Path $outDirAbs | Out-Null

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

$appDir = Join-Path $outDirAbs "app"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $appDir
New-Item -ItemType Directory -Force -Path $appDir | Out-Null

$exe = Join-Path $buildDirAbs "BlockForge.exe"
if (!(Test-Path $exe)) { throw "BlockForge.exe not found at $exe" }

Copy-Item $exe (Join-Path $appDir "BlockForge.exe")
if (!(Test-Path (Join-Path $appDir "BlockForge.exe"))) { throw "Failed to stage BlockForge.exe into $appDir" }

$windeployqt = $null
$cmd = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if ($cmd) { $windeployqt = $cmd.Source }
if ([string]::IsNullOrWhiteSpace($windeployqt) -and ![string]::IsNullOrWhiteSpace($env:Qt6_DIR)) {
  $qtBin = Join-Path $env:Qt6_DIR "..\\..\\..\\bin"
  $candidate = Join-Path $qtBin "windeployqt.exe"
  if (Test-Path $candidate) { $windeployqt = $candidate }
}
if ([string]::IsNullOrWhiteSpace($windeployqt)) { throw "windeployqt.exe not found (Qt6_DIR is not set and windeployqt is not on PATH)" }

& $windeployqt --release --no-translations (Join-Path $appDir "BlockForge.exe")

$zlibSource = $null
$zlibName = $null
$triplet = $env:VCPKG_TARGET_TRIPLET
if ([string]::IsNullOrWhiteSpace($triplet)) { $triplet = "x64-windows" }
$vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT

$candidates = @()
foreach ($n in @("zlib1.dll", "zlib.dll")) {
  $candidates += (Join-Path $buildDirAbs $n)
  if (![string]::IsNullOrWhiteSpace($vcpkgRoot)) {
    $candidates += (Join-Path $vcpkgRoot ("installed\\" + $triplet + "\\bin\\" + $n))
    $candidates += (Join-Path $vcpkgRoot ("installed\\" + $triplet + "\\debug\\bin\\" + $n))
  }
  $candidates += ("C:\\vcpkg\\installed\\" + $triplet + "\\bin\\" + $n)
  $candidates += ("C:\\ProgramData\\vcpkg\\installed\\" + $triplet + "\\bin\\" + $n)
}

foreach ($c in $candidates) {
  if (![string]::IsNullOrWhiteSpace($c) -and (Test-Path $c)) {
    $zlibSource = $c
    $zlibName = [System.IO.Path]::GetFileName($c)
    break
  }
}

if ([string]::IsNullOrWhiteSpace($zlibSource)) {
  if (![string]::IsNullOrWhiteSpace($vcpkgRoot)) {
    $installedTriplet = Join-Path $vcpkgRoot ("installed\\" + $triplet)
    if (Test-Path $installedTriplet) {
      $z = Get-ChildItem -Path $installedTriplet -Filter "zlib1.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
      if (!$z) { $z = Get-ChildItem -Path $installedTriplet -Filter "zlib.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 }
      if ($z) {
        $zlibSource = $z.FullName
        $zlibName = $z.Name
      }
    }
  }
  $z = Get-ChildItem -Path $buildDirAbs -Filter "zlib1.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
  if (!$z) { $z = Get-ChildItem -Path $buildDirAbs -Filter "zlib.dll" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 }
  if ($z) {
    $zlibSource = $z.FullName
    $zlibName = $z.Name
  }
}

if ([string]::IsNullOrWhiteSpace($zlibSource)) {
  $needsZlib = $true
  $dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
  if ($dumpbin) {
    $deps = & $dumpbin.Source /nologo /dependents $exe 2>$null
    if ($LASTEXITCODE -eq 0) {
      $needsZlib = ($deps -match "(?i)\bzlib1\.dll\b") -or ($deps -match "(?i)\bzlib\.dll\b")
    }
  }
  if ($needsZlib) {
    throw "zlib dll not found (zlib1.dll/zlib.dll). VCPKG_INSTALLATION_ROOT=$vcpkgRoot triplet=$triplet"
  }
}

if (![string]::IsNullOrWhiteSpace($zlibSource)) {
  Copy-Item $zlibSource (Join-Path $appDir $zlibName) -Force
  if ($zlibName -ne "zlib1.dll") {
    Copy-Item $zlibSource (Join-Path $appDir "zlib1.dll") -Force
  }
}

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

$staging = Join-Path $outDirAbs "staging"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $staging
New-Item -ItemType Directory -Force -Path $staging | Out-Null

$rootFolder = Join-Path $staging ("BlockForge-" + $vSafe)
New-Item -ItemType Directory -Force -Path $rootFolder | Out-Null
Copy-Item (Join-Path $appDir "*") $rootFolder -Recurse -Force

$portableZip = Join-Path $outDirAbs ("BlockForge-Portable-" + $vSafe + ".zip")
if (Test-Path $portableZip) { Remove-Item -Force $portableZip }
Compress-Archive -Path $rootFolder -DestinationPath $portableZip

choco install nsis -y --no-progress

$nsi = Join-Path $PSScriptRoot "..\\packaging\\nsis\\BlockForge.nsi"
$setupExe = Join-Path $outDirAbs ("BlockForge-Setup-" + $vSafe + ".exe")
$makensis = $null
$mcmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
if ($mcmd) { $makensis = $mcmd.Source }
if ([string]::IsNullOrWhiteSpace($makensis)) {
  $candidate = Join-Path $env:ChocolateyInstall "bin\\makensis.exe"
  if (Test-Path $candidate) { $makensis = $candidate }
}
if ([string]::IsNullOrWhiteSpace($makensis)) {
  $candidate = Join-Path $env:ChocolateyInstall "lib\\nsis\\tools\\makensis.exe"
  if (Test-Path $candidate) { $makensis = $candidate }
}
if ([string]::IsNullOrWhiteSpace($makensis)) {
  $chocoRoot = $env:ChocolateyInstall
  if ([string]::IsNullOrWhiteSpace($chocoRoot)) { $chocoRoot = "C:\\ProgramData\\chocolatey" }
  $nsisRoot = Join-Path $chocoRoot "lib\\nsis"
  if (Test-Path $nsisRoot) {
    $found = Get-ChildItem -Path $nsisRoot -Filter "makensis.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $makensis = $found.FullName }
  }
}
if ([string]::IsNullOrWhiteSpace($makensis)) {
  $candidate = "C:\\Program Files (x86)\\NSIS\\makensis.exe"
  if (Test-Path $candidate) { $makensis = $candidate }
}
if ([string]::IsNullOrWhiteSpace($makensis)) {
  $candidate = "C:\\Program Files\\NSIS\\makensis.exe"
  if (Test-Path $candidate) { $makensis = $candidate }
}
if ([string]::IsNullOrWhiteSpace($makensis)) {
  $pf86 = ${env:ProgramFiles(x86)}
  if ([string]::IsNullOrWhiteSpace($pf86)) { $pf86 = "C:\\Program Files (x86)" }
  $nsis = Join-Path $pf86 "NSIS"
  if (Test-Path $nsis) {
    $found = Get-ChildItem -Path $nsis -Filter "makensis.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $makensis = $found.FullName }
  }
}
if ([string]::IsNullOrWhiteSpace($makensis)) { throw "makensis.exe not found after installing nsis" }
& $makensis /DAPPDIR="$appDir" /DOUTFILE="$setupExe" /DVERSION="$vSafe" /DVIPRODUCTVERSION="$vi" $nsi

$shaPath = Join-Path $outDirAbs ("SHA256SUMS-" + $vSafe + ".txt")
if (Test-Path $shaPath) { Remove-Item -Force $shaPath }
Get-FileHash -Algorithm SHA256 $portableZip, $setupExe | ForEach-Object { "$($_.Hash)  $([System.IO.Path]::GetFileName($_.Path))" } | Out-File -FilePath $shaPath -Encoding ascii
