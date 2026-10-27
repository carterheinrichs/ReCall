$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../tools/Version.ps1"
$script:checks = 0
function Check($Condition, [string]$Message) {
    if (!$Condition) { throw "FAIL: $Message" }
    $script:checks++
}
function Expect-Failure([scriptblock]$Action, [string]$Message) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    Check $failed $Message
}
Check ((Step-ReCallVersion '1.2.3') -eq '1.2.4') 'default patch increment'
Check ((Step-ReCallVersion '1.2.3' -Part minor) -eq '1.3.0') 'minor increment resets patch'
Check ((Step-ReCallVersion '1.2.3' -Part major) -eq '2.0.0') 'major increment resets minor and patch'
Check ((Step-ReCallVersion '0.0.0') -eq '0.0.1') 'zero version accepted'
Check ((Step-ReCallVersion '1.2.65535' -Part minor) -eq '1.3.0') 'minor increment can reset maximum patch'
Check ((@(Get-ReCallVersionParts '1.2.3.65535' -IncludeRevision) -join '.') -eq '1.2.3.65535') 'maximum revision accepted'
foreach ($invalid in @('1.2', '1.2.3.4', '01.2.3', '-1.2.3', '1.2.65536', '1.2.beta', '1.2.99999999999999999999')) {
    Expect-Failure { Step-ReCallVersion $invalid } "reject invalid base version: $invalid"
}
Expect-Failure { Step-ReCallVersion '1.2.65535' } 'patch overflow rejected'
Expect-Failure { Step-ReCallVersion '65535.2.3' -Part major } 'major overflow rejected'
Expect-Failure { Get-ReCallVersionParts '1.2.3.65536' -IncludeRevision } 'MSIX revision overflow rejected'
Expect-Failure { Get-ReCallVersionParts '1.2.3' -IncludeRevision } 'MSIX requires four components'
$temp = Join-Path ([IO.Path]::GetTempPath()) "ReCall-version-$([Guid]::NewGuid()).txt"
try {
    [IO.File]::WriteAllText($temp, "2.4.8`n")
    & "$PSScriptRoot/../tools/Bump-Version.ps1" -VersionFile $temp | Out-Null
    Check ([IO.File]::ReadAllText($temp) -eq "2.4.9`n") 'bump command writes patch version'
    & "$PSScriptRoot/../tools/Bump-Version.ps1" -VersionFile $temp -Part minor | Out-Null
    Check ([IO.File]::ReadAllText($temp) -eq "2.5.0`n") 'bump command writes minor version'
} finally { if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp } }
"Version checks passed ($script:checks checks)."
