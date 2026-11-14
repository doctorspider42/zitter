# Registers a per-user logon task that keeps the WSL NativeLink worker running
# in a hidden window, then starts it now. No admin rights needed.
#   powershell -ExecutionPolicy Bypass -File install_logon_task.ps1 -SchedulerHost 192.168.100.41 -Slots 14
param(
  [Parameter(Mandatory = $true)][string]$SchedulerHost,
  [int]$Slots = 14,
  [string]$Distro = 'Ubuntu'
)
$ErrorActionPreference = 'Stop'
$name = 'Zitter NativeLink worker'
$user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
$wsl = "wsl.exe -d $Distro -- bash -lc '~/nativelink/run_wsl_worker.sh --foreground $SchedulerHost $Slots'"
$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
  -Argument "-NoProfile -WindowStyle Hidden -Command `"$wsl`""
# At logon, and every 5 minutes as a watchdog: WSL can stop the worker (for
# example when an RDP session disconnects), and IgnoreNew skips the run while
# the worker is still up.
$trigger = @(
  (New-ScheduledTaskTrigger -AtLogOn -User $user),
  (New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) `
     -RepetitionInterval (New-TimeSpan -Minutes 5))
)
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
  -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) `
  -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $name -Action $action -Trigger $trigger -Principal $principal `
  -Settings $settings -Description 'Zitter LAN remote builds: NativeLink worker in WSL (tools/remote_build).' -Force | Out-Null
Start-ScheduledTask -TaskName $name
Start-Sleep -Seconds 5
(Get-ScheduledTask -TaskName $name).State
