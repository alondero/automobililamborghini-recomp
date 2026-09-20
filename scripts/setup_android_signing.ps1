# Creates a persistent release identity for the Android APK. Run only when
# configuring a NEW app key — re-running it would replace the release identity,
# which Android refuses (different signing key = users cannot update over the
# existing install). The keystore lives outside the repository and never enters
# git. Password backup uses Windows DPAPI and is readable only by this Windows
# user on this machine; keep a separate offline backup of the .jks file and the
# four secret values for disaster recovery.
#
# Idempotency: refuses to overwrite an existing backup so an accidental second
# run can't silently destroy a working release identity. If the keystore and
# password backup already exist but the GitHub secrets are out of sync (e.g.
# `gh` was unauthenticated when secrets were uploaded, or the repo was
# migrated), pass -ConfigureSecretsOnly to re-upload them without regenerating
# keys.
param(
    [string]$Repository = 'alondero/automobililamborghini-recomp',
    [string]$KeyDirectory = (Join-Path $env:LOCALAPPDATA 'LamborghiniRecomp\Signing'),
    [string]$Keytool = 'keytool',
    [switch]$ConfigureSecretsOnly
)
$ErrorActionPreference = 'Stop'
$keyPath = Join-Path $KeyDirectory 'lamborghini-release.jks'
$credentialPath = Join-Path $KeyDirectory 'credentials.clixml'

# Preflight: validate the tools this script depends on *before* touching any
# state, so a missing tool doesn't leave a half-configured backup that the
# guard below would then refuse to overwrite.
if (-not (Get-Command $Keytool -ErrorAction SilentlyContinue)) {
    throw "$Keytool not on PATH. Install a JDK (keytool ships with it) or pass -Keytool 'C:\path\to\keytool.exe'."
}
if ($ConfigureSecretsOnly) {
    if (-not (Test-Path -LiteralPath $keyPath)) { throw "ConfigureSecretsOnly requires an existing keystore at $keyPath." }
    if (-not (Test-Path -LiteralPath $credentialPath)) { throw "ConfigureSecretsOnly requires an existing password backup at $credentialPath." }
    & gh auth status | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "gh is not authenticated. Run 'gh auth login' before retrying." }
} else {
    if ((Test-Path -LiteralPath $keyPath) -or (Test-Path -LiteralPath $credentialPath)) {
        throw 'A signing backup already exists. Do not replace a release identity; use the existing key (or pass -ConfigureSecretsOnly to re-upload GitHub secrets from it).'
    }
    & gh auth status | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "gh is not authenticated. Run 'gh auth login' first." }
    New-Item -ItemType Directory -Force -Path $KeyDirectory | Out-Null
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    & icacls $KeyDirectory /inheritance:r /grant:r "${identity}:(OI)(CI)F" 'SYSTEM:(OI)(CI)F' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Cannot secure the signing directory.' }
}

# -ConfigureSecretsOnly branch: read the DPAPI-encrypted password backup and
# re-upload all four secrets. No key regeneration, no keystore overwrite.
if ($ConfigureSecretsOnly) {
    $cred = Import-Clixml -LiteralPath $credentialPath
    $password = $cred.GetNetworkCredential().Password
    $env:LAMBO_SIGNING_PASSWORD = $password
    try {
        $keystoreBase64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($keyPath))
        $values = @{
            ANDROID_KEYSTORE_BASE64 = $keystoreBase64
            ANDROID_KEYSTORE_PASSWORD = $password
            ANDROID_KEY_ALIAS = 'lamborghini'
            ANDROID_KEY_PASSWORD = $password
        }
        foreach ($name in $values.Keys) {
            $values[$name] | & gh secret set $name --repo $Repository
            if ($LASTEXITCODE -ne 0) { throw "Cannot configure $name. Re-run with the same arguments to resume from the next secret." }
        }
        Write-Host "Secrets re-uploaded for $Repository. Local key untouched at $keyPath."
    } finally {
        Remove-Item Env:LAMBO_SIGNING_PASSWORD -ErrorAction SilentlyContinue
        $password = $null
        $values = $null
    }
    return
}

# Fresh-identity branch: generate the key FIRST (no on-disk backup yet), then
# write the DPAPI password backup only if the keystore landed cleanly. This
# avoids the dead-end lockout where a keytool failure leaves credentials.clixml
# behind and the guard refuses any re-run.
$random = New-Object byte[] 48
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
$rng.GetBytes($random)
$rng.Dispose()
$password = [Convert]::ToBase64String($random)
$env:LAMBO_SIGNING_PASSWORD = $password
try {
    & $Keytool -genkeypair -keystore $keyPath -storetype PKCS12 -alias lamborghini `
        -keyalg RSA -keysize 4096 -validity 10000 -dname 'CN=Lamborghini Recompiled' `
        -storepass:env LAMBO_SIGNING_PASSWORD -keypass:env LAMBO_SIGNING_PASSWORD
    if ($LASTEXITCODE -ne 0) {
        # Clean up partial .jks the JDK may have left behind so the guard on a
        # re-run sees a clean directory.
        Remove-Item -LiteralPath $keyPath -ErrorAction SilentlyContinue
        throw 'Key generation failed. The local directory is clean; re-run after fixing keytool.'
    }
    # Keystore landed. Now persist the DPAPI password backup.
    $secure = ConvertTo-SecureString $password -AsPlainText -Force
    [pscredential]::new('lamborghini', $secure) | Export-Clixml -LiteralPath $credentialPath
    $values = @{
        ANDROID_KEYSTORE_BASE64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($keyPath))
        ANDROID_KEYSTORE_PASSWORD = $password
        ANDROID_KEY_ALIAS = 'lamborghini'
        ANDROID_KEY_PASSWORD = $password
    }
    foreach ($name in $values.Keys) {
        $values[$name] | & gh secret set $name --repo $Repository
        if ($LASTEXITCODE -ne 0) {
            throw "Cannot configure $name. The local key is intact at $keyPath; re-run with -ConfigureSecretsOnly to retry the upload from where it stopped."
        }
    }
    Write-Host "Signing key backed up at $keyPath; repository signing secrets configured."
    Write-Host 'Keep the key and password in a secure backup. credentials.clixml is tied to this Windows account and machine.'
} finally {
    Remove-Item Env:LAMBO_SIGNING_PASSWORD -ErrorAction SilentlyContinue
    $password = $null
    $values = $null
    [Array]::Clear($random, 0, $random.Length)
}
