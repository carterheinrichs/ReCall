[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable
)
$ErrorActionPreference = 'Stop'
if (!$env:RECALL_SIGNING_PFX_BASE64 -or !$env:RECALL_SIGNING_PFX_PASSWORD) {
    throw 'Set both RECALL_SIGNING_PFX_BASE64 and RECALL_SIGNING_PFX_PASSWORD to sign in CI.'
}
if (!(Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Executable not found: $Executable" }
$root = (Resolve-Path "$PSScriptRoot/..").Path
. "$PSScriptRoot/Signing.ps1"
[xml]$manifest = [IO.File]::ReadAllText((Join-Path $root 'packaging/AppxManifest.xml'))
$publisher = [string]$manifest.Package.Identity.Publisher
$tempRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$pfxPath = Join-Path $tempRoot "ReCall-signing-$([Guid]::NewGuid()).pfx"
$existingThumbprints = @(Get-ChildItem -Path 'Cert:\CurrentUser\My' | ForEach-Object { $_.Thumbprint })
$imported = @()
try {
    [IO.File]::WriteAllBytes($pfxPath, [Convert]::FromBase64String($env:RECALL_SIGNING_PFX_BASE64))
    $password = ConvertTo-SecureString -String $env:RECALL_SIGNING_PFX_PASSWORD -AsPlainText -Force
    $imported = @(Import-PfxCertificate -FilePath $pfxPath -CertStoreLocation 'Cert:\CurrentUser\My' -Password $password)
    $cert = Select-ReCallSigningCertificate -Certificates $imported -Publisher $publisher
    if (!$cert) { throw "The imported PFX does not contain a valid code-signing private key matching '$publisher'." }
    # Explicit identity prevents the development-certificate fallback from ever
    # generating an unrelated certificate on a fresh GitHub-hosted runner.
    & "$PSScriptRoot/Package.ps1" -Executable $Executable -Sign -CertificateThumbprint $cert.Thumbprint
} finally {
    if (Test-Path -LiteralPath $pfxPath) { Remove-Item -LiteralPath $pfxPath -Force }
    foreach ($cert in $imported) {
        $path = "Cert:\CurrentUser\My\$($cert.Thumbprint)"
        if ($cert.Thumbprint -notin $existingThumbprints -and (Test-Path -LiteralPath $path)) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    $env:RECALL_SIGNING_PFX_BASE64 = $null
    $env:RECALL_SIGNING_PFX_PASSWORD = $null
}
