[CmdletBinding()]
param(
    [ValidateSet('major','minor','patch')][string]$Part = 'patch',
    [string]$VersionFile = "$PSScriptRoot/../VERSION"
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Version.ps1"
$path = (Resolve-Path -LiteralPath $VersionFile).Path
$before = [IO.File]::ReadAllText($path).Trim()
$after = Step-ReCallVersion -Version $before -Part $Part
[IO.File]::WriteAllText($path, "$after`n", [Text.UTF8Encoding]::new($false))
"ReCall version: $before -> $after"
'Reconfigure/build before packaging. Commit VERSION with the release changes.'
