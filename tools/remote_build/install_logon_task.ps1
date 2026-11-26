# Registers the scheduled task that keeps the WSL NativeLink worker running,
# then starts it now.
#   powershell -ExecutionPolicy Bypass -File install_logon_task.ps1 -SchedulerHost 192.168.100.41 -Slots 16
#
# By default the task runs as the current user whether or not anyone is logged
# on (S4U, no stored password): it starts at boot, so a Windows Update restart
# in the night does not leave the worker down until someone logs in.
# Registering that needs an administrator token, which an SSH session of an
# administrator has; a non-elevated PowerShell needs `-LogonType Interactive`,
# which only runs while the user is logged on.
#
# -WakeAt sets a daily time at which the task wakes the PC from sleep, so a
# PC that went to sleep in the evening is up for an overnight build. The worker
# keeps the PC awake while it runs (run_worker_task.ps1).
param(
  [Parameter(Mandatory = $true)][string]$SchedulerHost,
  [int]$Slots = 16,
  [string]$Distro = 'Ubuntu',
  [ValidateSet('S4U', 'Interactive')][string]$LogonType = 'S4U',
  [string]$WakeAt = '03:45'
)
$ErrorActionPreference = 'Stop'
$name = 'Zitter NativeLink worker'
$user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
# run_worker_task.ps1 sits next to this script and keeps the PC awake while
# the worker runs.
$runner = Join-Path $PSScriptRoot 'run_worker_task.ps1'
$action = New-ScheduledTaskAction -Execute 'powershell.exe' `
  -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$runner`" -SchedulerHost $SchedulerHost -Slots $Slots -Distro $Distro"
# Every 5 minutes as a watchdog: WSL can stop the worker, and IgnoreNew skips
# the run while the worker is still up.
$watchdog = New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) `
  -RepetitionInterval (New-TimeSpan -Minutes 5)
if ($LogonType -eq 'S4U') {
  $trigger = @((New-ScheduledTaskTrigger -AtStartup), $watchdog)
} else {
  $trigger = @((New-ScheduledTaskTrigger -AtLogOn -User $user), $watchdog)
}
if ($WakeAt) {
  $trigger += New-ScheduledTaskTrigger -Daily -At $WakeAt
}
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType $LogonType -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
  -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) `
  -MultipleInstances IgnoreNew -WakeToRun -StartWhenAvailable
# Stop a running instance so the new definition starts the worker afresh.
Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue | Stop-ScheduledTask
Register-ScheduledTask -TaskName $name -Action $action -Trigger $trigger -Principal $principal `
  -Settings $settings -Description 'Zitter LAN remote builds: NativeLink worker in WSL (tools/remote_build).' -Force | Out-Null
Start-ScheduledTask -TaskName $name
Start-Sleep -Seconds 5
"{0} ({1})" -f (Get-ScheduledTask -TaskName $name).State, $LogonType
