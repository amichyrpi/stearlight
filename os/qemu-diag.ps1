$serial = 'F:\StearlightBuild\vm-image-armada-contract\qemu-diag.serial'
$stdout = 'F:\StearlightBuild\vm-image-armada-contract\qemu-diag.out'
$stderr = 'F:\StearlightBuild\vm-image-armada-contract\qemu-diag.err'
Remove-Item -LiteralPath $serial,$stdout,$stderr -Force -ErrorAction SilentlyContinue
$args = @(
  '-machine','q35','-m','1024','-smp','2','-accel','tcg,thread=multi','-cpu','max',
  '-drive','if=pflash,format=raw,readonly=on,file=F:\StearlightBuild\vm-image-armada-contract\edk2-code.fd',
  '-drive','if=pflash,format=raw,file=F:\StearlightBuild\vm-image-armada-contract\edk2-vars.fd',
  '-drive','file=F:\StearlightBuild\vm-image-armada-contract\clean-test-overlay16.qcow2,format=qcow2,if=ide',
  '-nic','user,model=e1000','-vga','none','-device','VGA,xres=2880,yres=1600,vgamem_mb=32,edid=on',
  '-display','none','-serial',"file:$serial",'-no-reboot'
)
$p = Start-Process -FilePath 'C:\Program Files\qemu\qemu-system-x86_64.exe' -ArgumentList $args `
  -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
Start-Sleep -Seconds 20
$exited = $p.HasExited
if (-not $exited) { $p.Kill(); $p.WaitForExit(5000) }
"pid=$($p.Id) exited=$exited code=$($p.ExitCode)"
if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Tail 80 }
if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Tail 120 }
if (Test-Path -LiteralPath $serial) { Get-Content -LiteralPath $serial -Tail 120 }
