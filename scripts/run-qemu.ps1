$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Esp = Join-Path $Root 'build/uefi/esp'
$candidates = @(
  'C:\Program Files\qemu\share\edk2-x86_64-code.fd',
  'C:\Program Files\qemu\share\edk2\x86_64\code.fd',
  'C:\Program Files\QEMU\share\edk2-x86_64-code.fd'
)
$ovmf = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $ovmf) { $ovmf = (Get-ChildItem -Path 'C:\Program Files\qemu*' -Filter '*code*.fd' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1).FullName }
if (-not $ovmf) { throw 'QEMU/OVMF firmware not found. Install QEMU with UEFI/EDK2 firmware.' }
if (-not (Test-Path $Esp)) { throw "ESP not found: $Esp. Run .\scripts\build-uefi.sh first (or make image)." }
& qemu-system-x86_64 `
  -machine q35 `
  -accel tcg `
  -cpu max `
  -m 512M `
  -drive "if=pflash,format=raw,readonly=on,file=$ovmf" `
  -drive "format=raw,file=fat:rw:$Esp" `
  -serial stdio `
  -display none `
  -no-reboot `
  -no-shutdown
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
