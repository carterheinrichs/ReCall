function Get-ReCallVersionParts {
    param([Parameter(Mandatory)][string]$Version, [switch]$IncludeRevision)
    $pattern = if ($IncludeRevision) { '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' } `
        else { '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' }
    if ($Version -notmatch $pattern) { throw "Invalid version '$Version': expected $(if ($IncludeRevision) { 'major.minor.patch.revision' } else { 'major.minor.patch' }) without leading zeroes." }
    foreach ($part in $Version.Split('.')) {
        [uint16]$number = 0
        if (![uint16]::TryParse($part, [ref]$number)) { throw 'Each version component must be between 0 and 65535.' }
        [int]$number
    }
}

function Step-ReCallVersion {
    param([Parameter(Mandatory)][string]$Version, [ValidateSet('major','minor','patch')][string]$Part = 'patch')
    $parts = @(Get-ReCallVersionParts -Version $Version)
    $index = switch ($Part) { 'major' { 0 } 'minor' { 1 } 'patch' { 2 } }
    if ($parts[$index] -eq 65535) { throw "Cannot increment $Part beyond the MSIX limit of 65535." }
    $parts[$index]++
    for ($i = $index + 1; $i -lt 3; $i++) { $parts[$i] = 0 }
    return $parts -join '.'
}
