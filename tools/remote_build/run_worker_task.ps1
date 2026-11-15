# Runs the WSL NativeLink worker in the foreground for the logon task and
# keeps the PC from sleeping while it runs, as a media player does: the request
# lasts as long as this process and changes no power settings. The display may
# still turn off.
param(
  [Parameter(Mandatory = $true)][string]$SchedulerHost,
  [int]$Slots = 16,
  [string]$Distro = 'Ubuntu'
)
Add-Type -Namespace Zitter -Name Power -MemberDefinition @'
[DllImport("kernel32.dll")]
public static extern uint SetThreadExecutionState(uint flags);
'@
# ES_CONTINUOUS | ES_SYSTEM_REQUIRED
[Zitter.Power]::SetThreadExecutionState(0x80000001) | Out-Null
& wsl.exe -d $Distro -- bash -lc "~/nativelink/run_wsl_worker.sh --foreground $SchedulerHost $Slots"
