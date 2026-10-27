$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../tools/Signing.ps1"
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
function New-TestCertificate([string]$Subject, [int]$StartDays, [int]$EndDays, [switch]$NoCodeSigning) {
    $key = [Security.Cryptography.RSA]::Create(2048)
    try {
        $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new($Subject, $key,
            [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
        $oids = [Security.Cryptography.OidCollection]::new()
        $oid = if ($NoCodeSigning) { '1.3.6.1.5.5.7.3.1' } else { '1.3.6.1.5.5.7.3.3' }
        [void]$oids.Add([Security.Cryptography.Oid]::new($oid))
        $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($oids, $false))
        $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new(
            [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature, $true))
        return $request.CreateSelfSigned([DateTimeOffset]::Now.AddDays($StartDays), [DateTimeOffset]::Now.AddDays($EndDays))
    } finally { $key.Dispose() }
}

$publisher = 'CN=ReCall Development'
$certificates = @()
$publicOnly = $null
$temp = Join-Path ([IO.Path]::GetTempPath()) "ReCall-signing-$([Guid]::NewGuid()).cer"
try {
    $older = New-TestCertificate $publisher -2 30
    $newer = New-TestCertificate $publisher -1 30
    $expired = New-TestCertificate $publisher -30 -1
    $other = New-TestCertificate 'CN=Other App' -1 30
    $wrongEku = New-TestCertificate $publisher -1 30 -NoCodeSigning
    $certificates = @($older,$newer,$expired,$other,$wrongEku)
    $publicOnly = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
        $newer.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    Check ((Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher).Thumbprint -eq $newer.Thumbprint) 'discovery picks newest usable publisher certificate'
    Check ((Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher -Thumbprint $older.Thumbprint).Thumbprint -eq $older.Thumbprint) 'explicit identity wins over newer certificate'
    $spaced = ' ' + $older.Thumbprint.ToLowerInvariant() + "`n"
    Check ((Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher -Thumbprint $spaced).Thumbprint -eq $older.Thumbprint) 'pasted thumbprint normalized'
    Expect-Failure { Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher -Thumbprint ('0' * 40) } 'missing private-key identity rejected'
    Expect-Failure { Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher -Thumbprint 'not-a-thumbprint' } 'malformed thumbprint rejected'
    Expect-Failure { Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher -Thumbprint $expired.Thumbprint } 'expired certificate rejected'
    Expect-Failure { Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher -Thumbprint $other.Thumbprint } 'publisher mismatch rejected'
    Expect-Failure { Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher -Thumbprint $wrongEku.Thumbprint } 'wrong EKU rejected'
    Expect-Failure { Select-ReCallSigningCertificate -Certificates @($publicOnly) -Publisher $publisher -Thumbprint $publicOnly.Thumbprint } 'public-only certificate cannot sign'
    Check ($null -eq (Select-ReCallSigningCertificate -Certificates @($expired,$other,$wrongEku,$publicOnly) -Publisher $publisher)) 'no usable certificate returns none for first-run creation'
    [IO.File]::WriteAllBytes($temp, $older.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    $fromFile = Get-ReCallCertificateFileThumbprint -Path $temp
    Check ($fromFile -eq $older.Thumbprint) 'reads thumbprint from actual exported CER'
    Check ((Select-ReCallSigningCertificate -Certificates $certificates -Publisher $publisher -Thumbprint $fromFile).Thumbprint -eq $older.Thumbprint) 'CER pins original identity across rebuilds'
    "Signing checks passed ($script:checks checks; no certificate-store changes)."
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp }
    if ($publicOnly) { $publicOnly.Dispose() }
    foreach ($cert in $certificates) { $cert.Dispose() }
}
