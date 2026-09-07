$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Iso = Join-Path $Root 'build\RixuriOS.iso'
$RixfsImage = Join-Path $Root 'build\rixfs.img'
$OvmfCode = Join-Path $Root 'build\uefi\ovmf\OVMF_CODE_4M.fd'
$OvmfVars = Join-Path $Root 'build\uefi\ovmf\OVMF_VARS_4M.fd'

if (-not (Test-Path $OvmfCode)) { throw "OVMF not found: $OvmfCode" }
if (-not (Test-Path $Iso)) { throw "ISO not found: $Iso. Run make iso first." }

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
  -no-reboot `
  -no-shutdown
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
