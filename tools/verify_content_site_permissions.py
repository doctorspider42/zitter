#!/usr/bin/env python3
"""Verify Clipboard site settings and per-profile persistence in Content Lab."""

import argparse
from contextlib import ExitStack
from http.server import ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import threading

from verify_content_clipboard import ClipboardFixture
from verify_content_bundle import loader_environment
from verify_content_profiles import KEY_FILE


PAGE = b"""<!doctype html><meta charset=utf-8>
<h1>Clipboard permission fixture</h1>
<script>
Promise.all([
  navigator.permissions.query({name: 'clipboard-read'}),
  navigator.permissions.query({name: 'clipboard-write'})
]).then(([read, write]) => {
  const report = () => {
    document.title = 'Zitter site clipboard ' + read.state + '/' + write.state;
    if (parent !== window)
      parent.postMessage(document.title, location.origin);
  };
  read.onchange = write.onchange = report;
  report();
});
</script>"""
EMBEDDED = b"""<!doctype html><meta charset=utf-8>
<script>
  addEventListener('message', event => {
    const frame = document.querySelector('iframe');
    if (event.origin === location.origin && event.source === frame?.contentWindow)
      document.title = event.data;
  });
</script>
<iframe src="/site-permissions" allow="clipboard-read; clipboard-write"></iframe>"""
READ = b"""<!doctype html><meta charset=utf-8>
<style>body { margin: 0 } button { width: 230px; height: 55px }</style>
<button>Read the test clipboard</button>
<script>
  document.querySelector('button').addEventListener('click', async event => {
    if (!event.isTrusted) return;
    if (document.title !== 'Zitter clipboard read ready') return;
    document.title = 'Zitter clipboard read requesting';
    let passed = false;
    try {
      passed = (await navigator.clipboard.readText()) === 'zitter-private-pasteboard-fixture';
    } catch (_) {}
    document.title = passed ? 'Zitter clipboard read passed' : 'Zitter clipboard read failed';
  });
  document.title = 'Zitter clipboard read ready';
</script>"""
MEDIA = b"""<!doctype html><meta charset=utf-8>
<style>body { margin: 0 } button { position: absolute; inset: 0 auto auto 0; width: 230px; height: 55px }
video { display: block; margin-top: 60px }</style>
<button>Start test camera and microphone</button>
<video muted playsinline width="160"></video>
<script>
  document.querySelector('button').addEventListener('click', async event => {
    if (!event.isTrusted) return;
    if (document.title !== 'Zitter media ready') return;
    document.title = 'Zitter media requesting';
    fetch('/site-media-report?stage=request');
    try {
      const stream = await navigator.mediaDevices.getUserMedia({audio: true, video: true});
      fetch('/site-media-report?stage=granted');
      const tracks = stream.getTracks();
      if (tracks.length !== 2 || tracks.some(track => track.readyState !== 'live')) {
        document.title = 'Zitter media failed';
        return;
      }
      let ended = 0;
      for (const track of tracks) {
        track.addEventListener('ended', () => {
          if (++ended === tracks.length) document.title = 'Zitter media ended';
        });
      }
      const video = document.querySelector('video');
      video.srcObject = stream;
      await video.play();
      video.requestVideoFrameCallback(() => document.title = 'Zitter media started');
    } catch (error) {
      fetch('/site-media-report?' + new URLSearchParams({error: error.name}));
      document.title = error.name === 'NotAllowedError' ?
          'Zitter media denied' : 'Zitter media failed';
    }
  });
  document.title = 'Zitter media ready';
</script>"""


TAB_CAPTURE = b"""<!doctype html><meta charset=utf-8>
<style>
  body { margin: 0; background: rgb(219, 33, 61) }
  button { position: absolute; top: 0; left: 0; width: 230px; height: 55px }
  video { position: absolute; top: 60px; left: 0; width: 160px }
</style>
<button>Share the test tab</button>
<video muted playsinline></video>
<script>
  document.querySelector('button').addEventListener('click', async event => {
    if (!event.isTrusted || document.title !== 'Zitter media ready') return;
    document.title = 'Zitter media requesting';
    try {
      const other = location.pathname.includes('-other-');
      const expectAudio = location.pathname.includes('-audio-');
      const desktop = location.pathname.includes('-desktop-');
      const surface = desktop ? (location.pathname.includes('-screen-') ? 'monitor' : 'window')
                              : 'browser';
      const exclude = /-(exclude|close|navigation)$/.test(location.pathname);
      const audio = expectAudio ? {suppressLocalAudioPlayback: true}
                                : !location.pathname.endsWith('-close');
      const stream = await navigator.mediaDevices.getDisplayMedia({
        video: true, audio, selfBrowserSurface: exclude ? 'exclude' : 'include',
        monitorTypeSurfaces: location.pathname.includes('-desktop-exclude-') ? 'exclude'
                                                                             : 'include'
      });
      const [track] = stream.getVideoTracks();
      const [audioTrack] = stream.getAudioTracks();
      const tracks = stream.getTracks();
      if (!track || tracks.length !== (expectAudio ? 2 : 1) ||
          tracks.some(track => track.readyState !== 'live') ||
          Boolean(audioTrack) !== expectAudio ||
          track.getSettings().displaySurface !== surface) {
        document.title = 'Zitter media failed';
        return;
      }
      let ended = 0;
      let videoMatched = false;
      let audioMatched = !expectAudio;
      const maybeStarted = () => {
        if (videoMatched && audioMatched && !ended)
          document.title = 'Zitter media started';
      };
      for (const track of tracks) {
        track.addEventListener('ended', () => {
          if (++ended === tracks.length) document.title = 'Zitter media ended';
        });
      }
      if (audioTrack) {
        const audioContext = new AudioContext();
        const analyser = audioContext.createAnalyser();
        analyser.fftSize = 4096;
        const silent = audioContext.createGain();
        silent.gain.value = 0;
        audioContext.createMediaStreamSource(new MediaStream([audioTrack]))
                    .connect(analyser).connect(silent).connect(audioContext.destination);
        audioTrack.addEventListener('ended', () => audioContext.close(), {once: true});
        await audioContext.resume();
        const channel = new BroadcastChannel('zitter-test-tab-audio');
        channel.postMessage('play fixture tone');
        channel.close();
        const levels = new Float32Array(analyser.frequencyBinCount);
        const checkAudio = () => {
          if (ended) return;
          analyser.getFloatFrequencyData(levels);
          let peak = 0;
          for (let i = 1; i < levels.length; ++i) {
            if (levels[i] > levels[peak]) peak = i;
          }
          const frequency = peak * audioContext.sampleRate / analyser.fftSize;
          audioMatched = levels[peak] > -65 && Math.abs(frequency - 997) < 28;
          if (audioMatched) maybeStarted();
          else requestAnimationFrame(checkAudio);
        };
        requestAnimationFrame(checkAudio);
      }
      const video = document.querySelector('video');
      video.srcObject = stream;
      const canvas = document.createElement('canvas');
      canvas.width = canvas.height = 1;
      const context = canvas.getContext('2d', {willReadFrequently: true});
      const checkFrame = () => {
        if (desktop) {
          videoMatched = video.videoWidth > 0 && video.videoHeight > 0;
          maybeStarted();
          return;
        }
        context.drawImage(video, video.videoWidth * 0.75, video.videoHeight * 0.75,
                          1, 1, 0, 0, 1, 1);
        const [r, g, b] = context.getImageData(0, 0, 1, 1).data;
        const matched = other ? r < 65 && g > 55 && g < 130 && b > 165
                              : r > 185 && g < 60 && b > 30 && b < 95;
        if (matched) {
          videoMatched = true;
          maybeStarted();
        } else if (!ended) {
          video.requestVideoFrameCallback(checkFrame);
        }
      };
      video.requestVideoFrameCallback(checkFrame);
      await video.play();
    } catch (error) {
      fetch('/site-media-report?' + new URLSearchParams({error: error.name}));
      document.title = error.name === 'NotAllowedError' ?
          'Zitter media denied' : 'Zitter media failed';
    }
  });
  document.title = 'Zitter media ready';
</script>"""

TAB_CAPTURE_PHASES = ("deny", "policy", "exclude", "allow", "switch",
                      "other-allow", "other-close", "other-navigation",
                      "other-audio-allow", "other-audio-navigation",
                      "desktop-window-allow", "desktop-screen-allow",
                      "desktop-exclude-allow")
TAB_CAPTURE_SOURCE = b"""<!doctype html><meta charset=utf-8>
<title>Blue capture fixture</title><style>body {background: rgb(25, 89, 211)}</style>
<script>
  const channel = new BroadcastChannel('zitter-test-tab-audio');
  channel.addEventListener('message', async event => {
    if (event.data !== 'play fixture tone') return;
    const context = new AudioContext();
    const oscillator = context.createOscillator();
    oscillator.frequency.value = 997;
    const gain = context.createGain();
    gain.gain.value = 0.025;
    oscillator.connect(gain).connect(context.destination);
    await context.resume();
    oscillator.start();
    oscillator.stop(context.currentTime + 3);
    oscillator.addEventListener('ended', () => context.close(), {once: true});
  }, {once: true});
</script>"""
TAB_CAPTURE_DECOY = b"""<!doctype html><meta charset=utf-8>
<title>Yellow capture decoy</title><style>body {background: rgb(241, 207, 13)}</style>"""


class Fixture(ClipboardFixture):
    def do_GET(self):
        if self.path.startswith("/site-media-report?"):
            self.send_response(204)
            self.end_headers()
            return
        tab_capture = (
            self.path in ("/site-media-tab-desktop-native-cancel",
                          "/site-media-tab-desktop-native-window-allow",
                          "/site-media-tab-desktop-native-screen-allow") or
            self.path in {f"/site-media-tab-{phase}" for phase in TAB_CAPTURE_PHASES}
        )
        if not tab_capture and self.path not in ("/site-permissions", "/site-permissions?replacement",
                             "/site-permissions-policy", "/site-permissions-embedded",
                             "/site-permissions-read", "/site-media", "/site-media-policy",
                             "/site-media-source", "/site-media-decoy"):
            return super().do_GET()
        body = TAB_CAPTURE if tab_capture else {"/site-permissions-embedded": EMBEDDED,
                "/site-permissions-read": READ, "/site-media": MEDIA,
                "/site-media-policy": MEDIA, "/site-media-source": TAB_CAPTURE_SOURCE,
                "/site-media-decoy": TAB_CAPTURE_DECOY}.get(self.path, PAGE)
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        if self.path == "/site-permissions-policy":
            self.send_header("Permissions-Policy", "clipboard-read=(), clipboard-write=()")
        if self.path == "/site-media-policy":
            self.send_header("Permissions-Policy", "camera=(), microphone=()")
        if self.path == "/site-media-tab-policy":
            self.send_header("Permissions-Policy", "display-capture=()")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def start_server(stack):
    server = stack.enter_context(ThreadingHTTPServer(("127.0.0.1", 0), Fixture))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    stack.callback(thread.join)
    stack.callback(server.shutdown)
    return f"http://127.0.0.1:{server.server_port}"


def clipboard_rules(profile):
    path = profile / "Content Site Permissions"
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8")).get("zitter", {}).get("clipboard_permissions", {})


def run_site_permission_probe(executable, root, phase, origin, *,
                              theme="light", renderer="compositor", rejected=False,
                              env=None, missing_clipboard_fixture=False):
    command = [
        executable, "--zitter-content-workspace", "--no-proxy-server",
        f"--zitter-content-workspace-root={root}",
        f"--zitter-content-workspace-probe={origin}/",
        f"--zitter-content-workspace-site-permission-probe={phase}",
        f"--zitter-content-workspace-theme={theme}",
        f"--zitter-rust-raster={renderer}",
    ]
    if phase.startswith("media-"):
        command.append("--vmodule=workspace_media_access=1")
        if not phase.startswith("media-tab-"):
            command.append("--use-fake-device-for-media-stream")
        if phase.startswith("media-tab-other-audio-"):
            command.append("--autoplay-policy=no-user-gesture-required")
    try:
        timeout = 120 if phase.startswith("media-tab-desktop-native-") and phase.endswith("-allow") else 40
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired as error:
        raise SystemExit(f"{phase} timed out.\n{error.stderr!r}") from error
    if phase == "media-tab-desktop-native-cancel" and (
        "Zitter Content native desktop picker created" not in result.stdout
    ):
        raise SystemExit(f"The system picker did not create a session.\n{result.stderr}")
    if missing_clipboard_fixture:
        if ("Clipboard read fixture is unavailable" not in result.stderr or
                "Zitter Content site permissions probe passed" in result.stdout):
            raise SystemExit(f"Clipboard read started without isolation.\n{result.stderr}")
        return
    if rejected:
        if result.returncode != 1 or "Could not open Content site permissions" not in result.stderr:
            raise SystemExit(f"Malformed permissions were accepted.\n{result.stderr}")
    elif (result.returncode or
          "Zitter Content site permissions probe passed" not in result.stdout or
          "Zitter Content workspace session probe passed" not in result.stdout):
        raise SystemExit(f"{phase} failed.\n{result.stdout}\n{result.stderr}")
    if phase == "prompt-read":
        if "Zitter Content read from the isolated macOS pasteboard" not in result.stdout:
            raise SystemExit("The page did not read from the isolated pasteboard")
        print("Content read the isolated macOS pasteboard after consent")


def build_pasteboard_fixture(directory):
    source = Path(__file__).resolve().parents[1] / "tests/fixtures/content_clipboard_pasteboard_mac.mm"
    library = directory / "clipboard_fixture.dylib"
    subprocess.run([
        "xcrun", "--sdk", "macosx", "clang++", "-fobjc-arc", "-dynamiclib",
        "-framework", "AppKit", source, "-o", library,
    ], check=True)
    return dict(loader_environment(), DYLD_INSERT_LIBRARIES=str(library))


def create_probe_root(stack, windows):
    root = Path(stack.enter_context(
        tempfile.TemporaryDirectory(prefix="zitter-content-probe-")
    )).resolve()
    if not windows:
        with os.fdopen(os.open(root / KEY_FILE, os.O_WRONLY | os.O_CREAT |
                                os.O_EXCL, 0o600), "wb") as out:
            out.write(secrets.token_bytes(32))
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="Zitter Content Lab.app or zitter_content.exe")
    parser.add_argument("--theme", choices=("light", "dark"), default="light")
    parser.add_argument("--renderer", choices=("compositor", "metal"), default="compositor")
    parser.add_argument("--workspace-only", action="store_true",
                        help="Use the Workspace entry point and omit the standalone private probe")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--read-clipboard", action="store_true",
                        help="Verify readText against a private AppKit pasteboard on macOS")
    mode.add_argument("--media-capture", action="store_true",
                      help="Verify consent, denial and stop using Chromium's fake camera and microphone")
    mode.add_argument("--tab-capture", action="store_true",
                      help="Verify scratch-tab capture and synthetic desktop video")
    mode.add_argument("--native-desktop-cancel", action="store_true",
                      help="Open and close the macOS desktop picker without capturing a source")
    mode.add_argument("--native-desktop", choices=("window", "screen"),
                      help="Verify capture from a test surface selected in the macOS system picker")
    args = parser.parse_args()
    windows = os.name == "nt"
    if (args.native_desktop or args.native_desktop_cancel) and sys.platform != "darwin":
        parser.error("Native desktop picker trials require macOS 14 or newer")
    if args.read_clipboard and sys.platform != "darwin":
        parser.error("--read-clipboard requires macOS")
    if windows and args.renderer == "metal":
        parser.error("Metal is only available on macOS")
    executable = args.app if windows else args.app / "Contents/MacOS/Zitter Content Lab"
    executable = executable.resolve()
    if not executable.is_file():
        parser.error(f"Content Lab executable not found: {executable}")

    with ExitStack() as stack:
        root = create_probe_root(stack, windows)

        primary = start_server(stack)
        other = start_server(stack)
        work = root / "workspace-work"
        personal = root / "workspace-personal"

        def run(phase, origin, *, rejected=False, env=None):
            run_site_permission_probe(executable, root, phase, origin,
                                      theme=args.theme, renderer=args.renderer, rejected=rejected,
                                      env=env)

        def require_rules(work_rules, personal_rules):
            if clipboard_rules(work) != work_rules or clipboard_rules(personal) != personal_rules:
                raise SystemExit("Unexpected permissions in the profile stores")

        if args.native_desktop_cancel:
            run("media-tab-desktop-native-cancel", primary)
            require_rules({}, {})
            print("Content native desktop picker cancellation passed")
            return

        if args.native_desktop:
            print(f"Select the disposable test {args.native_desktop} in the macOS picker.",
                  flush=True)
            run(f"media-tab-desktop-native-{args.native_desktop}-allow", primary)
            require_rules({}, {})
            print(f"Content native {args.native_desktop} delivered a video frame and stopped")
            return

        if args.tab_capture:
            for phase in TAB_CAPTURE_PHASES:
                case_root = create_probe_root(stack, windows)
                run_site_permission_probe(executable, case_root, f"media-tab-{phase}", primary,
                                          theme=args.theme, renderer=args.renderer)
                if any(clipboard_rules(case_root / name) for name in
                       ("workspace-work", "workspace-personal")):
                    raise SystemExit("Tab capture changed the clipboard permission stores")
                print(f"Content tab capture probe passed: {phase}")
            return

        if args.media_capture:
            for phase in ("media-deny", "media-policy", "media-allow", "media-switch"):
                run(phase, primary)
                require_rules({}, {})
                print(f"Content camera and microphone probe passed: {phase}")
            return

        # This is an independent process and profile. Give its native window
        # the initial activation before the sequence of Workspace restarts.
        if not windows and not args.workspace_only:
            with tempfile.TemporaryDirectory(prefix="zitter-content-probe-") as private:
                private_root = Path(private).resolve()
                result = subprocess.run(
                    [executable, "--zitter-content-clipboard-probe", "--no-proxy-server",
                     f"--zitter-content-profile-probe={primary}/clipboard/no-gesture",
                     f"--zitter-content-probe-root={private_root}",
                     "--zitter-content-probe-profile=private"],
                    capture_output=True, text=True, timeout=40,
                )
                if (result.returncode or "Zitter Content clipboard probe passed" not in result.stdout
                        or any(private_root.iterdir())):
                    raise SystemExit(f"Standalone private Clipboard probe failed.\n"
                                     f"{result.stdout}\n{result.stderr}")
        run("change", primary)
        require_rules({primary: 1}, {primary: 2})
        run("other", other)
        require_rules({primary: 1}, {primary: 2})
        run("reopen", primary)
        require_rules({primary: 1}, {primary: 2})
        for phase in ("policy", "embedded"):
            run(phase, primary)
            require_rules({primary: 1}, {primary: 2})
        run("reset", primary)
        require_rules({}, {primary: 2})
        for phase in ("prompt-cancel", "prompt-navigation", "prompt-switch"):
            run(phase, primary)
            require_rules({}, {primary: 2})
        run("prompt-allow", primary)
        require_rules({primary: 1}, {primary: 2})
        run("reopen", primary)
        require_rules({primary: 1}, {primary: 2})

        if args.read_clipboard:
            with tempfile.TemporaryDirectory(prefix="zitter-clipboard-library-") as scratch:
                fixture_env = build_pasteboard_fixture(Path(scratch))
                run("reset", primary)
                require_rules({}, {primary: 2})
                run_site_permission_probe(
                    executable, root, "prompt-read", primary,
                    theme=args.theme, renderer=args.renderer, env=loader_environment(),
                    missing_clipboard_fixture=True,
                )
                require_rules({}, {primary: 2})
                run("prompt-read", primary, env=fixture_env)
                require_rules({primary: 1}, {primary: 2})

        damaged = work / "Content Site Permissions"
        invalid = b'{"zitter":{"clipboard_permissions":{"https://invalid.test/path":1}}}'
        damaged.write_bytes(invalid)
        run("reopen", primary, rejected=True)
        if damaged.read_bytes() != invalid or clipboard_rules(personal) != {primary: 2}:
            raise SystemExit("Rejecting malformed permissions changed a profile")
        print(f"Content Clipboard site permissions passed in {args.theme} with {args.renderer}")


if __name__ == "__main__":
    main()
