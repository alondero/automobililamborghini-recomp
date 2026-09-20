# Creates a persistent release identity for the Android APK. Run only when
# configuring a NEW app key — re-running it would replace the release identity,
# which Android refuses (different signing key = users cannot update over the
# existing install). The keystore lives outside the repository and never enters
# git. Password backup uses Windows DPAPI and is readable only by this Windows
# user on this machine; keep a separate offline backup of the .jks file and the
# four secret values for disaster recovery.
#
# Idempotency: refuses to overwrite an existing backup so an accidental second
# run can't silently destroy a working release identity.
param(
    [string]$Repository = 'alondero/automobililamborghini-recomp',
    [string]$KeyDirectory = "$env:LOCALAPPDATA/LamborghiniRecomp/Signing",
    [string]$Keytool = 'keytool'
)
$ErrorActionPreference = 'Stop'
$keyPath = Join-Path $KeyDirectory 'lamborghini-release.jks'
$credentialPath = Join-Path $KeyDirectory 'credentials.clixml'
if ((Test-Path -LiteralPath $keyPath) -or (Test-Path -LiteralPath $credentialPath)) {
    throw 'A signing backup already exists. Do not replace a release identity; use the existing key.'
}
New-Item -ItemType Directory -Force -Path $KeyDirectory | Out-Null
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
& icacls $KeyDirectory /inheritance:r /grant:r "${identity}:(OI)(CI)F" 'SYSTEM:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot secure the signing directory.' }
$random = New-Object byte[] 48
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
$rng.GetBytes($random)
$rng.Dispose()
$password = [Convert]::ToBase64String($random)
$env:LAMBO_SIGNING_PASSWORD = $password
try {
    $secure = ConvertTo-SecureString $password -AsPlainText -Force
    [pscredential]::new('lamborghini', $secure) | Export-Clixml -LiteralPath $credentialPath
    & $Keytool -genkeypair -keystore $keyPath -storetype PKCS12 -alias lamborghini `
        -keyalg RSA -keysize 4096 -validity 10000 -dname 'CN=Lamborghini Recompiled' `
        -storepass:env LAMBO_SIGNING_PASSWORD -keypass:env LAMBO_SIGNING_PASSWORD
    if ($LASTEXITCODE -ne 0) { throw 'Key generation failed. Preserve the backup files and inspect the error.' }
    $values = @{
        ANDROID_KEYSTORE_BASE64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($keyPath))
        ANDROID_KEYSTORE_PASSWORD = $password
        ANDROID_KEY_ALIAS = 'lamborghini'
        ANDROID_KEY_PASSWORD = $password
    }
    foreach ($name in $values.Keys) {
        $values[$name] | & gh secret set $name --repo $Repository
        if ($LASTEXITCODE -ne 0) { throw "Cannot configure $name. The local key remains backed up; do not generate another." }
    }
    Write-Host "Signing key backed up at $keyPath; repository signing secrets configured."
    Write-Host 'Keep the key and password in a secure backup. credentials.clixml is tied to this Windows account and machine.'
} finally {
    Remove-Item Env:LAMBO_SIGNING_PASSWORD -ErrorAction SilentlyContinue
    $password = $null
    $values = $null
    [Array]::Clear($random, 0, $random.Length)
}
