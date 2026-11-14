# Remote builds on the local network

Zitter's Windows build can send its C++ compiles to other PCs on the LAN. Siso,
Chromium's build tool, sends each `clang-cl` compile through the Remote Execution
API to a self-hosted [NativeLink](https://github.com/TraceMachina/nativelink)
server, and NativeLink runs it on whichever worker has a free slot. Rust, code
generators and linking stay on the build PC.

Status: working, measured on one target only (see [Results](#results)).

## How it works

```
build PC (Windows)                                   other PC (Windows)
┌───────────────────────────────────────────┐        ┌──────────────────────────┐
│ Siso ── clang-cl compile ──► NativeLink    │◄─LAN──►│ WSL2: NativeLink worker  │
│   │       (REAPI :50051)    server (Docker)│ :50061 │   Linux clang            │
│   └─ local: Rust, codegen,  ├ CAS + cache  │        └──────────────────────────┘
│      links                  ├ scheduler    │
│                             └ worker       │
└───────────────────────────────────────────┘
```

- **Workers run Linux.** Each compile carries the Linux build of the same clang
  revision (`third_party/llvm-build/Release+Asserts_linux`) and
  `clang_remote_wrapper`, which runs it in `clang-cl` mode on the Windows
  command line. This is the setup Google and EngFlow use for Windows builds.
  The objects are byte-identical to local `clang-cl.exe` output: checked with
  SHA-256 on `obj/base/base/at_exit.obj`.
- **The toolchain must be hermetic.** Remote compiles need the VS and Windows
  SDK headers inside the checkout, so the build uses a packaged toolchain
  (`third_party/depot_tools/win_toolchain/vs_files/<hash>`) instead of the
  installed Visual Studio. `DEPOT_TOOLS_WIN_TOOLCHAIN=1` selects it. Switching
  an existing output directory to it rebuilds everything once.
- **Workers only connect out.** A remote worker needs no open ports; the build
  PC must accept ports 50051 and 50061 from the LAN.
- **Siso marks nothing executable when sending from Windows.** Workers start
  every action through `worker_action.sh` (bash), and the wrapper copies clang
  into a local cache with the executable bit.

## Files

All in [`tools/remote_build/`](../tools/remote_build/):

| File | Purpose |
|---|---|
| `package_toolchain.py` | Packages installed VS 2026 Build Tools + SDK as `<hash>.zip` (once per VS/SDK update). |
| `setup_siso.py` | Prepares a checkout: hermetic toolchain, Linux clang, wrapper and Siso configs. Run after each fresh checkout. |
| `build.py` | Runs Siso against the cluster with a sane `-remote_jobs`. |
| `windows-remote.args.gn` | The two GN args that turn remote compiles on. |
| `start_server.ps1`, `Dockerfile`, `nativelink_server.json5` | NativeLink server + local worker in Docker Desktop. |
| `nativelink_worker.json5`, `install_wsl_worker.sh`, `run_wsl_worker.sh`, `install_logon_task.ps1` | Worker on another PC in WSL2. |
| `clang_remote_wrapper`, `worker_action.sh`, `rewrapper_windows.cfg`, `backend.star` | What Siso sends and how workers run it. |

## Setup

### Build PC

1. Docker Desktop running. Start the server and a local worker:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\remote_build\start_server.ps1 -Slots 16
   ```
2. Package the toolchain (needs depot_tools in `.chromium/depot_tools`):
   ```powershell
   python tools/remote_build/package_toolchain.py
   ```
   It writes `.chromium/win_toolchain_pkg/<hash>.zip` (about 1.4 GB).
3. Prepare the checkout:
   ```powershell
   python tools/remote_build/setup_siso.py
   ```
4. Add `windows-remote.args.gn` to the output directory's `args.gn`, then
   `gn gen` with `DEPOT_TOOLS_WIN_TOOLCHAIN=1`. Use an existing output directory
   (CLAUDE.md, "Build directories and disk space").

### Another PC as a worker

Needs WSL2 with Ubuntu and SSH access from the build PC. From the build PC:

1. Copy the worker files (`nativelink` from the image, `nativelink_worker.json5`,
   `worker_action.sh`, `install_wsl_worker.sh`, `run_wsl_worker.sh`) to
   `%USERPROFILE%\zitter-nl` on the worker PC and run
   `wsl -d Ubuntu -- bash /mnt/c/Users/<user>/zitter-nl/install_wsl_worker.sh`.
   Extract the binary with `docker create zitter-remote-build` and `docker cp`.
2. Keep it running with a per-user logon task (no admin):
   ```powershell
   powershell -ExecutionPolicy Bypass -File install_logon_task.ps1 -SchedulerHost <build PC IP> -Slots 14
   ```
   WSL stops the worker when its last `wsl.exe` session ends, for example when
   an RDP session disconnects. The task starts it at logon and restarts it every
   5 minutes when it is down.

Current cluster: the build PC (Threadripper 1920X, 16 slots, 192.168.100.41)
and `PPAJAK-LAP` (Ryzen 7 PRO 5850U, 14 slots, Wi-Fi at 270–350 Mbit/s, SSH alias
`zitter-node`).

## Building

```powershell
python tools/remote_build/build.py -C out/ZitterWinDev zitter zitter_setup
```

Use `--strict-remote` to fail instead of compiling locally when a remote compile
fails, which is useful to check the setup. The server's worker list and errors:
`docker logs zitter-nativelink`; a WSL worker's log: `~/nativelink/worker.log`.

## Results

| Target | Where | Time | Notes |
|---|---|---|---|
| `base_unittests` (6,338 steps) | build PC + laptop | 15 min 51 s | first run, cold cache, default `-remote_jobs` (37 compiles timed out in the queue and ran locally), disk busy with a cleanup |

Not measured yet: the same target built locally, a run with `--remote-jobs 48`,
and a full `zitter` build.

## Known limits

- Remote compiles only. Rust, mojo/grit generators and links run locally;
  linking `zitter.exe` with ThinLTO stays the longest step of an official build.
- Siso spends noticeable time hashing inputs (`remoteexec-digest`) on the build
  PC's SATA SSD.
- The build PC's own worker shares its CPU with Siso's local steps.

## Next

- Measure local vs remote on the same target, then a full build.
- A Mac as a native worker (NativeLink and clang both ship for macOS ARM), and
  Mac builds sending their compiles to this cluster
  (`build/config/siso/mac_sdk.star`).
