#!/usr/bin/env python3
"""Inclusive main-thread wall-sample envelopes; overlapping groups are not additive."""
import argparse
import hashlib
import json
from pathlib import Path
import re

PATTERNS = {
    "idle_drain": r"base::RunLoop::RunUntilIdle",
    "occlusion_updates": r"WebContentsOcclusionCheckerMac.*performOcclusionStateUpdates",
    "windowserver_sync": r"SLSConnectionSynchronizeSLSCATransaction|_SLSTransactionWaitSource",
    "kernel_wait": r"mach_msg2_trap|semaphore_wait_trap|__psynch_cvwait|__ulock_wait",
    "update_shell": r"WorkspaceWindow::UpdateShell",
    "rebuild_tabs": r"WorkspaceShell::RebuildTabs",
    "native_graph_adapter": r"WorkspaceShell::ReconcileSidebarNodes",
    "rust_graph": r"ActionGraph.*reconcile",
    "layout": r"views::View::Layout|LayoutRootViewIfNecessary|Rust.*Layout::",
    "view_bounds": r"::SetBounds|::SetBoundsRect",
    "view_hierarchy": r"AddChildView|RemoveChildView|ReorderChildView",
    "paint": r"::Paint|::Raster|::Draw",
    "text_layout": r"RenderText|CTLine|EditorTextLayout",
    "configuration_serialization": r"WorkspaceWindow::SaveConfiguration",
    "commands": r"WorkspaceWindow::CommandResults",
    "pin_tree": r"PinTree::",
    "drawable_acquisition": r"nextDrawable",
}


def analyze(text):
    headers = list(re.finditer(r"^    (\d+) Thread_[^\n]+", text, re.M))
    if not headers or not re.search(r"CrBrowserMain|com.apple.main-thread", headers[0][0]):
        raise ValueError("First sample thread is not the browser main thread")
    total = int(headers[0][1])
    main = text[headers[0].start():headers[1].start() if len(headers) > 1 else len(text)]
    counts = {}
    for name, pattern in PATTERNS.items():
        count, ancestors = 0, []
        for line in main.splitlines()[1:]:
            match = re.match(r"^([ +!:|]+)(\d+) (.*)", line)
            if not match:
                continue
            depth = len(match[1])
            while ancestors and depth <= ancestors[-1]:
                ancestors.pop()
            if re.search(pattern, match[3]) and not ancestors:
                count += int(match[2])
                ancestors.append(depth)
        counts[name] = {"samples": count, "percent": count / total * 100}
    return {"main_thread_samples": total, "groups": counts}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profiles", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = []
    for folder in args.profiles:
        meta = json.loads((folder / "metadata.json").read_text())
        data = (folder / "sample.txt").read_bytes()
        result.append({"test": meta["test"], "backend": meta["backend"], "folder": str(folder),
                       "sample_sha256": hashlib.sha256(data).hexdigest(), **analyze(data.decode())})
    args.output.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
