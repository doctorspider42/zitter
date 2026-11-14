#!/usr/bin/env python3
"""Builds with Siso, sending clang-cl compiles to the LAN NativeLink cluster.

  python tools/remote_build/build.py -C out/ZitterWinDev zitter zitter_setup

The output directory needs `use_siso = true` and `use_remoteexec = true` (see
windows-remote.args.gn) and the checkout needs setup_siso.py.

--remote-jobs caps the compiles in flight. Siso defaults to ~1900, which queues
them on the scheduler past Siso's 2-minute step timeout and makes them fall
back to local; keep it near the workers' total slots plus some headroom.
"""

import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_SRC = os.path.join(REPO, ".chromium", "src")
DEPOT_TOOLS = os.path.join(REPO, ".chromium", "depot_tools")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-C", dest="out", required=True, help="output dir, relative to src")
    p.add_argument("targets", nargs="*")
    p.add_argument("--src", default=DEFAULT_SRC)
    p.add_argument("--scheduler", default="127.0.0.1",
                   help="host running the NativeLink server")
    p.add_argument("--remote-jobs", type=int, default=48)
    p.add_argument("--strict-remote", action="store_true",
                   help="fail instead of compiling locally when remote fails")
    args, extra = p.parse_known_args()

    src = os.path.abspath(args.src)
    siso = os.path.join(src, "third_party", "siso", "cipd",
                        "siso.exe" if os.name == "nt" else "siso")
    cmd = [siso, "ninja", "-C", args.out, "-project=",
           f"-reapi_address={args.scheduler}:50051", "-reapi_insecure",
           "-reapi_instance=default_instance", f"-remote_jobs={args.remote_jobs}"]
    if args.strict_remote:
        cmd.append("-strict_remote")
    cmd += extra + args.targets

    env = dict(os.environ, DEPOT_TOOLS_WIN_TOOLCHAIN="1",
               PATH=DEPOT_TOOLS + os.pathsep + os.environ.get("PATH", ""))
    start = time.monotonic()
    rc = subprocess.run(cmd, cwd=src, env=env).returncode
    print(f"remote build: exit {rc} after {time.monotonic() - start:.0f} s")
    sys.exit(rc)


if __name__ == "__main__":
    main()
