[CmdletBinding()]
param(
    [string]$Executable = "$PSScriptRoot/../out/build/Release/ReCall.exe",
    [switch]$Sign,
    [string]$CertificateThumbprint,
    [string]$CertificateFile
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
. "$PSScriptRoot/Signing.ps1"
if (($CertificateThumbprint -or $CertificateFile) -and !$Sign) { throw 'Use -Sign when specifying a signing certificate.' }
if ($CertificateThumbprint -and $CertificateFile) { throw 'Specify either -CertificateFile or -CertificateThumbprint, not both.' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Build ReCall first: $Executable" }
[xml]$manifest = [IO.File]::ReadAllText((Join-Path $root 'packaging/AppxManifest.xml'))
$publisher = [string]$manifest.Package.Identity.Publisher
$exportedCertificate = Join-Path $root 'out/ReCall.Development.cer'
$cert = $null
if ($Sign) {
    # Prefer the public certificate already distributed to users, preserving trust
    # across rebuilds. Explicit parameters allow intentionally selecting a new identity.
    if (!$CertificateThumbprint) {
        if (!$CertificateFile -and (Test-Path -LiteralPath $exportedCertificate)) { $CertificateFile = $exportedCertificate }
        if ($CertificateFile) { $CertificateThumbprint = Get-ReCallCertificateFileThumbprint -Path $CertificateFile }
    }
    $cert = Select-ReCallSigningCertificate -Certificates @(Get-ChildItem -Path 'Cert:\CurrentUser\My') `
        -Publisher $publisher -Thumbprint $CertificateThumbprint
    if (!$cert) {
        if ($publisher -cne 'CN=ReCall Development') { throw "No usable signing certificate matches '$publisher' in CurrentUser\My." }
        $cert = New-SelfSignedCertificate -Type Custom -Subject $publisher -FriendlyName 'ReCall Development' `
            -CertStoreLocation 'Cert:\CurrentUser\My' -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 3072 `
            -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(2) `
            -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}')
        "Created development signing certificate: $($cert.Thumbprint)"
    } else { "Using signing certificate: $($cert.Subject) [$($cert.Thumbprint)]" }
}
$sdkRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$sdk = Get-ChildItem (Join-Path $sdkRoot 'bin') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'x64/MakeAppx.exe') } | Sort-Object Name -Descending | Select-Object -First 1
if (!$sdk) { throw 'Windows SDK packaging tools not found' }
$stage = Join-Path $root 'out/msix'
New-Item -ItemType Directory -Force "$stage/Assets" | Out-Null
Copy-Item -LiteralPath $Executable -Destination "$stage/ReCall.exe"
Copy-Item -LiteralPath "$root/packaging/AppxManifest.xml" -Destination "$stage/AppxManifest.xml"
Copy-Item -LiteralPath "$root/THIRD_PARTY_NOTICES.md" -Destination "$stage/THIRD_PARTY_NOTICES.md"
# Generate deterministic development artwork without adding a graphics dependency to the app.
Add-Type -AssemblyName System.Drawing
foreach ($asset in @(@('StoreLogo',50), @('Square44x44Logo',44), @('Square150x150Logo',150))) {
    $size = [int]$asset[1]; $bitmap = [Drawing.Bitmap]::new($size,$size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([Drawing.Color]::FromArgb(27,29,35))
    $font = [Drawing.Font]::new('Segoe UI', [single]($size * 0.56), [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $format = [Drawing.StringFormat]::new(); $format.Alignment = 'Center'; $format.LineAlignment = 'Center'
    $graphics.DrawString('R',$font,[Drawing.Brushes]::White,[Drawing.RectangleF]::new(0,0,$size,$size),$format)
    $bitmap.Save("$stage/Assets/$($asset[0]).png",[Drawing.Imaging.ImageFormat]::Png)
    $format.Dispose(); $font.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}
$package = Join-Path $root 'out/ReCall.msix'
& (Join-Path $sdk.FullName 'x64/MakeAppx.exe') pack /d $stage /p $package /o
if ($LASTEXITCODE) { throw 'MakeAppx failed' }
if ($Sign) {
    & (Join-Path $sdk.FullName 'x64/SignTool.exe') sign /fd SHA256 /s My /sha1 $cert.Thumbprint $package
    if ($LASTEXITCODE) { throw 'SignTool failed' }
    Export-Certificate -Cert $cert -FilePath $exportedCertificate -Force | Out-Null
    "Certificate: $exportedCertificate"
}
"Package: $package"
