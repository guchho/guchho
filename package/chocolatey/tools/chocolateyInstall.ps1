$ErrorActionPreference = 'Stop'

$toolsDir   = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"
$packageName = $env:ChocolateyPackageName

$guchhoPath = Join-Path $toolsDir 'guchho.exe'

if (-not (Test-Path $guchhoPath)) {
    throw "guchho.exe not found in $toolsDir. Package is missing the embedded binary."
}

Write-Host "Guchho installed successfully. Run 'guchho' to get started."