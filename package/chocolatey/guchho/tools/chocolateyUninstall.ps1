$toolsDir = "$(Split-Path -parent $MyInvocation.MyCommand.Definition)"

$guchhoPath = Join-Path $toolsDir 'guchho.exe'
if (Test-Path $guchhoPath) {
    $pathToRemove = $toolsDir
    $currentPath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    if ($currentPath -like "*$pathToRemove*") {
        $newPath = ($currentPath.Split(';') | Where-Object { $_ -ne $pathToRemove }) -join ';'
        [Environment]::SetEnvironmentVariable('Path', $newPath, 'Machine')
        Write-Host "guchho has been removed from your PATH."
    }
}
