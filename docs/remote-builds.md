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
| `build.py` | Runs Siso against the cluster with a sane `-remote_jobs`, after `workers.py`. |
| `workers.py` | Checks the remote workers over SSH and starts the ones that are down. |
| `fresh_build.py` | New Chromium revision: fresh checkout, Content staging, setup and a full remote build, timed per step. |
| `windows-remote.args.gn` | The two GN args that turn remote compiles on. |
| `start_server.ps1`, `Dockerfile`, `nativelink_server.json5` | NativeLink server + local worker in Docker Desktop. |
| `nativelink_worker.json5`, `install_wsl_worker.sh`, `run_wsl_worker.sh`, `install_logon_task.ps1`, `run_worker_task.ps1` | Worker on another PC in WSL2. |
| `install_mac_worker.sh`, `mac_worker_action.sh` | Native worker on an Apple Silicon Mac. |
| `clang_remote_wrapper`, `worker_action.sh`, `rewrapper_windows.cfg`, `backend.star` | What Siso sends and how workers run it. |

## Setup

### Build PC

1. Docker Desktop running. Start the server and a local worker:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\remote_build\start_server.ps1 -Slots 12
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
2. Keep it running with a scheduled task, installed over SSH from the build PC
   (an administrator's SSH session has the rights it needs):
   ```powershell
   ssh <alias> 'powershell -ExecutionPolicy Bypass -File $env:USERPROFILE\zitter-nl\install_logon_task.ps1 -SchedulerHost <build PC IP> -Slots 16'
   ```
   The task runs as the user whether or not anyone is logged on (S4U, no stored
   password): it starts the worker at boot and every 5 minutes when it is down.
   WSL stops the worker when its last `wsl.exe` session ends, and an
   interactive-only task cannot start it again until someone logs in. On
   2026-11-25 a Windows Update restart at 00:15 left the laptop's worker down
   for the whole overnight build that way. The task also wakes the PC from sleep
   daily at 03:45 (`-WakeAt`, empty to turn it off), and while it runs, the PC
   stays awake: `run_worker_task.ps1` (copy it next to the installer) asks for
   that without changing power settings. From a non-elevated PowerShell on the
   PC itself, `-LogonType Interactive` installs the older logon-only task.

### A Mac as a worker

An Apple Silicon Mac runs the macOS build of NativeLink natively, without
Docker or a Linux VM. It cannot run the Linux clang that every compile carries,
so its entrypoint, `mac_worker_action.sh`, does what `clang_remote_wrapper`
does with Chromium's Mac clang of the same revision:

- It finds that clang by the SHA-256 of the Linux clang in the action
  (`~/nativelink/clang/<package>/linux_clang.sha256`) and fails the action
  when none matches, so a clang roll never runs a mismatched compiler. The
  scheduler then retries it on another worker.
- It adds `--target=x86_64-pc-windows-msvc`: clang-cl takes its default target
  from the host, which is arm64 on a Mac.
- The entrypoint is not an action input, so none of this changes
  `clang_remote_wrapper` or the remote cache. Keep its clang command line in
  sync with the wrapper.
- The hermetic Windows SDK has directories that differ only in case
  (`um/gl`, `um/GL`). The CAS and action directories live on a case-sensitive
  APFS sparse image (`~/nativelink/data.sparsebundle`, mounted at
  `~/nativelink/vol`).

Setup needs SSH (System Settings → General → Sharing → Remote Login) with the
build PC's key in `~/.ssh/authorized_keys`. From the build PC:

1. Copy `install_mac_worker.sh`, `mac_worker_action.sh` and
   `nativelink_worker.json5` to `~/zitter-nl` on the Mac and run
   ```sh
   bash ~/zitter-nl/install_mac_worker.sh <build PC IP> <clang package> <Linux clang SHA-256> [slots]
   ```
   The clang package is `CLANG_REVISION-CLANG_SUB_REVISION` from
   `tools/clang/scripts/update.py`; the SHA-256 is that of
   `third_party/llvm-build/Release+Asserts_linux/bin/clang`. It downloads
   NativeLink and the Mac clang, creates the image and installs a per-user
   LaunchAgent (`com.zitter.nativelink-worker`, no admin) that starts the worker
   at login, restarts it, and keeps the Mac awake with `caffeinate` while it runs.
2. Allow `nativelink` under System Settings → Privacy & Security → Local
   Network on the Mac itself. Until then the worker logs "No route to host";
   processes started over SSH are exempt, so a manual test there passes.
3. Rerun the installer after a clang roll; it adds the new clang next to the old.

The log is `~/nativelink/worker.log`; restart with
`launchctl kickstart -k gui/$(id -u)/com.zitter.nativelink-worker`.

The Mac is on Wi-Fi (802.11n). In the Chromium 159 build its worker spent its
first minutes on "still fetching inputs, possible stall" and was evicted twice
for not acknowledging a kill within 60 s, each time with 12 compiles that the
build PC then redid. Use Ethernet for the Mac in long builds, or fewer slots.

Checked on 2026-11-24: 12 of 48 `base` compiles ran on the Mac with
`-re_cache_enable_read=false`, and all 48 objects were byte-identical to the
earlier local and Linux-worker builds.

Current cluster: the build PC (Threadripper 1920X, 12 slots, 192.168.100.41),
`PPAJAK-LAP` (Ryzen 7 PRO 5850U, 16 slots, Wi-Fi at 270–350 Mbit/s,
192.168.100.6, SSH alias `zitter-node`), `PPAJAK-MAC` (M3 Pro, 12 slots,
192.168.100.190, SSH alias `zitter-mac`) and `DESKTOP-72UKT0Q` (Core i5-3570K,
4 slots, 192.168.100.180, SSH alias `zitter-node2`, user `spider`), a
proof of concept for an old PC: 14 of 96 uncached `base` compiles ran there,
all byte-identical.

WSL workers name themselves `wsl-<computer name>`; the laptop's older install
still reports `laptop`. WSL2 needs CPU virtualization: on `DESKTOP-72UKT0Q` it
had to be enabled in the firmware (ASUS: Advanced > CPU Configuration > Intel
Virtualization Technology) before `wsl --install --no-distribution`. Changing
that setting made Windows Hello's PIN unavailable; sign in with the password
and set the PIN again.

## Building

```powershell
python tools/remote_build/build.py -C out/ZitterWinContent zitter_content
```

`build.py` first runs `workers.py`: it checks each remote worker over SSH,
starts the ones that are down (WSL through the scheduled task, the Mac through
its LaunchAgent) and prints which ones the build will have. A worker that does
not answer over SSH is off or asleep. `--no-worker-check` skips it;
`python tools/remote_build/workers.py --no-start` only reports.

For a new Chromium revision, `fresh_build.py` does everything from a clean
checkout to a full build, timing each step in `build/fresh-build/fresh_build.log`;
`--from <step>` resumes. It renames an existing `.chromium/src` aside instead of
deleting it. It builds Zitter's only product, the Content embedder
(`//zitter_content:zitter_content` in `out/ZitterWinContent`, staged with
`tools/stage_content_embedder.py`). `--keep-going` compiles past failures, for
a first build on a new revision or platform.

```powershell
python tools/remote_build/fresh_build.py --keep-going
```

Slots: the build PC's worker gets 12 of its 24 threads by default, so Siso
keeps CPU for hashing inputs, scanning dependencies, Rust and links; the laptop
gets all 16. Once the generators and Rust are done, Siso needs little local CPU
and `start_server.ps1 -Slots 20` finishes the C++ faster. Siso keeps 44
compiles in flight (`--remote-jobs`).

Use `--strict-remote` to fail instead of compiling locally when a remote compile
fails, which is useful to check the setup. The server's worker list and errors:
`docker logs zitter-nativelink`; a WSL worker's log: `~/nativelink/worker.log`.

## Results

| Target | Where | Time | Notes |
|---|---|---|---|
| `base_unittests` (6,338 steps) | build PC + laptop | 15 min 51 s | first run, cold cache, default `-remote_jobs` (37 compiles timed out in the queue and ran locally), disk busy with a cleanup |

| `zitter_content`, fresh checkout (34,423 steps) | build PC + laptop | 3 h 13 min compile, 3 h 52 min with checkout and setup | `--remote-jobs 44`, 31,000 remote compiles and 30 local fallbacks; the same build on the build PC alone took about 8 hours |
| `zitter_content`, full rebuild after a toolchain re-download (26,764 steps) | build PC (20 slots) + laptop (asleep for part of the run) | 3 h 51 min | the laptop slept at 02:42, which led to `run_worker_task.ps1` |
| `zitter_content`, Chromium 159 fresh checkout (45,315 steps), 2026-11-25 | build PC (12 slots) + Mac, laptop and old PC down | 4 h 56 min | 37,518 remote compiles: the PC's worker ran 37,303, the Mac 1,240 (some of them killed and rerun); 362 local fallbacks. The laptop's worker was down after a Windows Update restart and the old PC was asleep, which led to the S4U task and `workers.py` |

The laptop's 16 slots roughly halve a fresh build, as long as both workers stay
busy: the queue of ready compiles never emptied in either run.

Re-downloading clang (`tools/clang/scripts/update.py`) gives its files new
timestamps and rebuilds everything. Do not fetch extra LLVM packages into a
built checkout; `--package=objdump` replaces the whole `Release+Asserts`
directory.

## Known limits

- Remote compiles only. Rust, mojo/grit generators and links run locally;
  linking `zitter.exe` with ThinLTO stays the longest step of an official build.
- Siso spends noticeable time hashing inputs (`remoteexec-digest`) on the build
  PC's SATA SSD.
- The build PC's own worker shares its CPU with Siso's local steps.

## Overnight builds

`tools/nightly/fire.py` builds Windows on this cluster and macOS on the Mac
from one command; see [nightly-builds.md](nightly-builds.md).

## Next

- Measure local vs remote on the same target, then a full build.
- Mac builds sending their compiles to this cluster
  (`build/config/siso/mac_sdk.star`). The Mac already works as a worker.
- Measure a full build with the Mac in the cluster.
