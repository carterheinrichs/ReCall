[CmdletBinding()]
param(
    [string]$Executable = "$PSScriptRoot/../out/build/Release/ReCall.exe",
    [switch]$Sign,
    [string]$CertificateThumbprint
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path
if (!(Test-Path -LiteralPath $Executable)) { throw "Build ReCall first: $Executable" }
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
    if (!$CertificateThumbprint) {
        $cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=ReCall Development' -FriendlyName 'ReCall Development' `
            -CertStoreLocation 'Cert:\CurrentUser\My' -KeyUsage DigitalSignature `
            -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}')
        $CertificateThumbprint = $cert.Thumbprint
    }
    $cert = Get-Item "Cert:\CurrentUser\My\$CertificateThumbprint"
    Export-Certificate -Cert $cert -FilePath "$root/out/ReCall.Development.cer" | Out-Null
    & (Join-Path $sdk.FullName 'x64/SignTool.exe') sign /fd SHA256 /sha1 $CertificateThumbprint $package
    if ($LASTEXITCODE) { throw 'SignTool failed; certificate subject must match the manifest Publisher' }
}
"Package: $package"
