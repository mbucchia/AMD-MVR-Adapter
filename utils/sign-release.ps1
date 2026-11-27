# Signs the DLLs from AMD-MVR-Adapter.zip and repackages them as iRacing-AMD-MVR-X.Y.Z.zip.

$ErrorActionPreference = 'Stop'

$inputZip = Join-Path $PSScriptRoot 'AMD-MVR-Adapter.zip'
if (-not (Test-Path $inputZip)) {
    throw "Could not find $inputZip"
}

# Build the version string from version.info (at the repository root).
$versionFile = Join-Path $PSScriptRoot '..\version.info'
if (-not (Test-Path $versionFile)) {
    throw "Could not find $versionFile"
}
$version = @{}
foreach ($line in Get-Content $versionFile) {
    if ($line -match '^\s*(\w+)\s*=\s*(\S+)\s*$') {
        $version[$Matches[1]] = $Matches[2]
    }
}
foreach ($key in 'major', 'minor', 'patch') {
    if (-not $version.ContainsKey($key)) {
        throw "version.info is missing '$key'"
    }
}
$versionString = "$($version.major).$($version.minor).$($version.patch)"

# Locate signtool.exe: PATH first, then the newest Windows SDK.
$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
if (-not $signtool) {
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.Directory.Parent.Name } -ErrorAction SilentlyContinue |
        Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $signtool) {
    throw 'Could not find signtool.exe'
}

$workDir = Join-Path ([System.IO.Path]::GetTempPath()) ("AMD-MVR-Adapter-sign-" + [guid]::NewGuid())
try {
    Write-Host "Extracting $inputZip"
    Expand-Archive -Path $inputZip -DestinationPath $workDir

    foreach ($dll in 'dxgi.dll', 'nvapi64.dll') {
        $path = Join-Path $workDir $dll
        if (-not (Test-Path $path)) {
            throw "$dll not found in $inputZip"
        }
        Write-Host "Signing $dll"
        & $signtool sign /fd sha256 /n "Matthieu Bucchianeri" /du "https://github.com/mbucchia/AMD-MVR-Adapter" /tr http://timestamp.digicert.com /td sha256 /a $path
        if ($LASTEXITCODE -ne 0) {
            throw "signtool failed for $dll (exit code $LASTEXITCODE)"
        }
    }

    $outputZip = Join-Path $PSScriptRoot "iRacing-AMD-MVR-$versionString.zip"
    Write-Host "Creating $outputZip"
    Compress-Archive -Path (Join-Path $workDir '*') -DestinationPath $outputZip -Force
    Write-Host 'Done.'
}
finally {
    if (Test-Path $workDir) {
        Remove-Item -Recurse -Force $workDir
    }
}
