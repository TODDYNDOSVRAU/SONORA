param(
  [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$adbCommand = Get-Command adb -ErrorAction SilentlyContinue
if (-not $adbCommand) {
  throw "ADB não encontrado. Instale Android Platform-Tools e confirme que adb.exe está no PATH."
}
$adbPath = $adbCommand.Source
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = "sonora-s23-target-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$state = (& $adbPath get-state 2>$null | Out-String).Trim()
$stateExitCode = $LASTEXITCODE
if ($stateExitCode -ne 0 -or $state -ne "device") {
  throw "Nenhum dispositivo ADB autorizado/conectado. Ative Depuração USB e autorize este computador."
}

function Save-AdbShellText {
  param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Command
  )
  $destination = Join-Path $OutputDirectory $Name
  $output = & $adbPath shell $Command 2>&1
  $exitCode = $LASTEXITCODE
  @($output) | Set-Content -Encoding utf8 -Path $destination
  if ($exitCode -ne 0) {
    Add-Content -Encoding utf8 -Path (Join-Path $OutputDirectory "collection-errors.txt") -Value "${Name}: adb shell retornou código $exitCode"
  }
}

$properties = @(
  "ro.build.fingerprint",
  "ro.vendor.build.fingerprint",
  "ro.product.model",
  "ro.product.device",
  "ro.product.cpu.abi",
  "ro.product.cpu.abilist",
  "ro.build.version.release",
  "ro.build.version.sdk",
  "ro.build.version.incremental",
  "ro.build.version.security_patch"
)
$propertyLines = foreach ($property in $properties) {
  $value = (& $adbPath shell getprop $property 2>&1 | Out-String).Trim()
  "$property=$value"
}
$kernel = (& $adbPath shell uname -a 2>&1 | Out-String).Trim()
$propertyLines += "kernel=$kernel"
$propertyLines | Set-Content -Encoding utf8 -Path (Join-Path $OutputDirectory "build-properties.txt")

# ADB shell commands are read-only; system XMLs are pulled directly without su.
$findCommand = 'find /vendor/etc /odm/etc /system/etc /system/vendor/etc /product/etc /system/product/etc /system_ext/etc -maxdepth 3 -type f -name "audio_effects*.xml" -print 2>/dev/null'
$findOutput = & $adbPath shell $findCommand 2>&1
$findExitCode = $LASTEXITCODE
$devicePaths = @($findOutput | ForEach-Object { ("$_").Trim() } | Where-Object { $_ -match '^/(vendor|odm|system|product|system_ext)/' } | Sort-Object -Unique)
$devicePaths | Set-Content -Encoding utf8 -Path (Join-Path $OutputDirectory "audio-effect-config-paths.txt")
if ($findExitCode -ne 0) {
  Add-Content -Encoding utf8 -Path (Join-Path $OutputDirectory "collection-errors.txt") -Value "Busca de XML retornou código $findExitCode."
}

$index = 0
foreach ($devicePath in $devicePaths) {
  if ($devicePath -notmatch '^/[A-Za-z0-9._/-]+$' -or $devicePath -match '(^|/)\.\.(/|$)') {
    Add-Content -Encoding utf8 -Path (Join-Path $OutputDirectory "collection-errors.txt") -Value "Caminho ignorado por formato inesperado: $devicePath"
    continue
  }
  $index++
  $safeName = $devicePath.TrimStart('/').Replace('/', '_')
  $destination = Join-Path $OutputDirectory ("{0:D2}-{1}" -f $index, $safeName)
  $pullOutput = & $adbPath pull $devicePath $destination 2>&1
  $pullExitCode = $LASTEXITCODE
  if ($pullExitCode -ne 0) {
    Add-Content -Encoding utf8 -Path (Join-Path $OutputDirectory "collection-errors.txt") -Value "Não foi possível ler $devicePath (código $pullExitCode): $($pullOutput -join ' ')"
  }
}

if ($index -eq 0) {
  "Nenhum arquivo audio_effects*.xml foi listado nos diretórios padrão." | Set-Content -Encoding utf8 -Path (Join-Path $OutputDirectory "xml-status.txt")
}

Save-AdbShellText -Name "selinux-mode.txt" -Command "getenforce"
Save-AdbShellText -Name "soundfx-libraries.txt" -Command "ls -laZ /vendor/lib64/soundfx /odm/lib64/soundfx /system/lib64/soundfx /product/lib64/soundfx /system_ext/lib64/soundfx /vendor/lib/soundfx /odm/lib/soundfx /system/lib/soundfx 2>/dev/null"
Save-AdbShellText -Name "audio-data-dir-labels.txt" -Command "ls -ldZ /data/vendor/audio /data/vendor /data/misc/audio 2>/dev/null"
Save-AdbShellText -Name "audio-effect-runtime-lines.txt" -Command "dumpsys media.audio_flinger 2>/dev/null | grep -iE 'effect|spatial|postproc' | head -100"

@"
Coleta somente de leitura do Galaxy S23 para investigar compatibilidade do módulo Sonora.
Não usa `su`, não altera arquivos do sistema e não grava áudio.
Revise os arquivos antes de compartilhar. Informe separadamente a versão do KernelSU Manager e o nome/versão do metamodule ativo.
"@ | Set-Content -Encoding utf8 -Path (Join-Path $OutputDirectory "README.txt")

$archivePath = "$OutputDirectory.zip"
Compress-Archive -Path (Join-Path $OutputDirectory "*") -DestinationPath $archivePath -Force
Write-Host "Coleta concluída: $archivePath"
Write-Host "XMLs encontrados: $index. Revise o conteúdo antes de compartilhar."
