# Certificate selection is kept separate from packaging so it can be checked without
# modifying the certificate store or invoking SignTool.
function Get-ReCallCertificateProblem {
    param($Certificate, [string]$Publisher)
    if ($Certificate.Subject -cne $Publisher) { return "Subject '$($Certificate.Subject)' does not match manifest Publisher '$Publisher'." }
    if (!$Certificate.HasPrivateKey) { return 'The certificate has no private key on this computer.' }
    $now = Get-Date
    if ($Certificate.NotBefore -gt $now -or $Certificate.NotAfter -le $now) { return 'The certificate is expired or not yet valid.' }
    $eku = $Certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' }
    if (!$eku -or !($eku.EnhancedKeyUsages | Where-Object { $_.Value -eq '1.3.6.1.5.5.7.3.3' })) {
        return 'The certificate must permit Code Signing (EKU 1.3.6.1.5.5.7.3.3).'
    }
    $usage = $Certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.15' }
    if ($usage -and !($usage.KeyUsages -band [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature)) {
        return 'The certificate does not permit digital signatures.'
    }
    return ''
}

function Select-ReCallSigningCertificate {
    param(
        [object[]]$Certificates,
        [Parameter(Mandatory)][string]$Publisher,
        [string]$Thumbprint
    )
    if ($Thumbprint) {
        $Thumbprint = ($Thumbprint -replace '[\s\p{Cf}]', '').ToUpperInvariant()
        if ($Thumbprint -notmatch '^[0-9A-F]{40}$') { throw 'A certificate thumbprint must contain 40 hexadecimal characters.' }
        $cert = $Certificates | Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1
        if (!$cert) {
            throw "Signing certificate $Thumbprint was not found in CurrentUser\My. A .cer contains only the public certificate; signing requires its matching private key on this computer. Use the original signing computer or import its private-key backup."
        }
        $problem = Get-ReCallCertificateProblem -Certificate $cert -Publisher $Publisher
        if ($problem) { throw "Cannot use signing certificate $Thumbprint : $problem" }
        return $cert
    }
    # Deterministic discovery when the exported .cer is not present (e.g. a cleaned out/).
    return $Certificates | Where-Object { !(Get-ReCallCertificateProblem -Certificate $_ -Publisher $Publisher) } |
        Sort-Object @{ Expression = 'NotBefore'; Descending = $true }, Thumbprint | Select-Object -First 1
}

function Get-ReCallCertificateFileThumbprint {
    param([Parameter(Mandatory)][string]$Path)
    $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new($resolved)
    try { return $cert.Thumbprint } finally { $cert.Dispose() }
}
