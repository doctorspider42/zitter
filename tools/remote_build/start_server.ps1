# Builds the NativeLink image and (re)starts the server + local worker on this
# PC in Docker Desktop. Siso connects to port 50051; remote workers to 50061.
#   powershell -ExecutionPolicy Bypass -File tools\remote_build\start_server.ps1 [-Slots 16] [-CasGB 25]
param(
  [int]$Slots = 16,
  [int]$CasGB = 25
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
docker build -q -t zitter-remote-build . | Out-Null
docker volume create zitter-nativelink | Out-Null
docker rm -f zitter-nativelink 2>$null | Out-Null
docker run -d --name zitter-nativelink --restart unless-stopped `
  -p 50051:50051 -p 50061:50061 -v zitter-nativelink:/data `
  -e ZITTER_WORKER_SLOTS=$Slots -e ZITTER_CAS_BYTES=$($CasGB * 1000000000) `
  zitter-remote-build | Out-Null
Start-Sleep -Seconds 4
docker ps --filter name=zitter-nativelink --format '{{.Names}}: {{.Status}}'
