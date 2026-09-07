$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Iso = Join-Path $Root 'build\RixuriOS.iso'
$RixfsImage = Join-Path $Root 'build\rixfs.img'
$OvmfDir = Join-Path $Root 'build\uefi\ovmf'
$OvmfCode = Join-Path $OvmfDir 'OVMF_CODE_4M.fd'
$OvmfVars = Join-Path $OvmfDir 'OVMF_VARS_4M.fd'

if (-not (Test-Path $OvmfCode) -or -not (Test-Path $OvmfVars)) {
    Write-Host "OVMF not found locally, copying from WSL..."
    New-Item -ItemType Directory -Force -Path $OvmfDir | Out-Null
    $wslDir = "/mnt/c/" + ($Root -replace "^[A-Z]:","").Replace("\","/")
    wsl -u root -- bash -c "cp /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_VARS_4M.fd '$wslDir/build/uefi/ovmf/'"
}
if (-not (Test-Path $OvmfCode)) { throw "OVMF not found: $OvmfCode" }
if (-not (Test-Path $Iso)) { throw "ISO not found: $Iso. Run make iso first." }

Stop-Process -Name qemu-system-x86_64 -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

& qemu-system-x86_64 `
  -machine q35,accel=tcg `
  -cpu max `
  -m 512M `
  -drive "if=pflash,format=raw,readonly=on,file=$OvmfCode" `
  -drive "if=pflash,format=raw,file=$OvmfVars" `
  -cdrom $Iso `
  -drive "if=none,format=raw,file=$RixfsImage,id=rixfs-test" `
  -device "nvme,drive=rixfs-test,serial=RIXURI-TEST" `
  -serial stdio `
  -display sdl `
  -no-reboot `
  -no-shutdown
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
