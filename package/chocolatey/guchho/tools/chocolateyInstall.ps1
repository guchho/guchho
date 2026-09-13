$ErrorActionPreference = 'Stop'

$toolsDir   = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$packageName = $env:ChocolateyPackageName

$guchhoPath = Join-Path $toolsDir 'guchho.exe'

if (-not (Test-Path $guchhoPath)) {
    throw "guchho.exe not found in $toolsDir. Package is missing the embedded binary."
}

Install-ChocolateyPath -PathToPersist $toolsDir -PathType 'Machine'

Write-Host "guchho has been added to your PATH."
Write-Host "guchho installed successfully. Try running: guchho"