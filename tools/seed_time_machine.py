#!/usr/bin/env python3
"""Seeds a week of made-up Time Machine history (ZitterTimeline.jsonl) for UI checks.

Each workspace of the profile's ZitterWorkspaces.json gets its own rhythm: work
spaces fill weekdays with morning and afternoon sessions, the first space also
gets evenings and lazy weekends. Tabs open, close and navigate every few minutes,
a few snapshot marks land on the axis, and some tabs stay open overnight. The
history is deterministic for a given --seed.

Quit Zitter first: it keeps the timeline in memory and rewrites the file on
start. Existing lines are kept (with a .bak copy); seeded frames end before a
workspace's first real frame, so its own history continues where they stop.

  python3 tools/seed_time_machine.py                       # build/rust-metal-profile
  python3 tools/seed_time_machine.py --data-dir DIR --days 10 --replace
"""
import argparse
import datetime as dt
import json
import random
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Public pages with plausible titles, grouped by what a space is used for.
WORK = [
    ("https://github.com/rust-lang/rust/pulls", "Pull requests · rust-lang/rust"),
    ("https://github.com/chromium/chromium/blob/main/ui/views/view.h", "chromium/ui/views/view.h at main · chromium/chromium"),
    ("https://doc.rust-lang.org/std/collections/struct.HashMap.html", "HashMap in std::collections - Rust"),
    ("https://doc.rust-lang.org/book/ch16-00-concurrency.html", "Fearless Concurrency - The Rust Programming Language"),
    ("https://developer.apple.com/documentation/metal", "Metal | Apple Developer Documentation"),
    ("https://developer.apple.com/documentation/appkit/nswindow", "NSWindow | Apple Developer Documentation"),
    ("https://www.sqlite.org/wal.html", "Write-Ahead Logging"),
    ("https://cxx.rs/extern-rust.html", "extern \"Rust\" — Rust ♡ C++"),
    ("https://chromium.googlesource.com/chromium/src/+/main/docs/ui/views/overview.md", "Views Overview"),
    ("https://www.figma.com/files/recents-and-sharing", "Recents – Figma"),
    ("https://linear.app/zitter/team/ZIT/active", "Active issues · Zitter"),
    ("https://calendar.google.com/calendar/r/week", "Kalendarz Google – ten tydzień"),
    ("https://mail.google.com/mail/u/0/#inbox", "Odebrane"),
    ("https://stackoverflow.com/questions/tagged/rust", "Newest 'rust' Questions - Stack Overflow"),
    ("https://en.cppreference.com/w/cpp/container/vector", "std::vector - cppreference.com"),
    ("https://webkit.org/blog/", "Blog | WebKit"),
    ("https://v8.dev/blog", "V8 JavaScript engine blog"),
    ("https://web.dev/articles/vitals", "Web Vitals | Articles | web.dev"),
    ("https://react.dev/learn", "Quick Start – React"),
    ("https://tailwindcss.com/docs/installation", "Installation - Tailwind CSS"),
]
READING = [
    ("https://arstechnica.com/gadgets/2007/10/mac-os-x-10-5/14/", "Mac OS X 10.5 Leopard: the Ars Technica review"),
    ("https://en.wikipedia.org/wiki/Zitterbewegung", "Zitterbewegung – Wikipedia"),
    ("https://en.wikipedia.org/wiki/Time_Machine_(macOS)", "Time Machine (macOS) – Wikipedia"),
    ("https://news.ycombinator.com/", "Hacker News"),
    ("https://www.theverge.com/tech", "Tech - The Verge"),
    ("https://www.quantamagazine.org/physics/", "Physics | Quanta Magazine"),
    ("https://aosabook.org/en/v1/chrome.html", "The Architecture of Open Source Applications: Chromium"),
    ("https://vimeo.com/36579366", "Bret Victor - Inventing on Principle on Vimeo"),
    ("https://www.youtube.com/watch?v=8pTEmbeENF4", "The Future of Programming - Bret Victor - YouTube"),
    ("https://open.spotify.com/playlist/37i9dQZF1DWZeKCadgRdKQ", "Deep Focus | Spotify Playlist"),
    ("https://www.nytimes.com/section/technology", "Technology - The New York Times"),
    ("https://fonts.google.com/specimen/Instrument+Serif", "Instrument Serif - Google Fonts"),
    ("https://www.allegro.pl/kategoria/laptopy-491", "Laptopy - Allegro"),
    ("https://www.booking.com/city/pl/krakow.html", "Hotele w Krakowie | Booking.com"),
    ("https://www.kwestiasmaku.com/przepis/pierogi-ruskie", "Pierogi ruskie - przepis | Kwestia Smaku"),
    ("https://www.filmweb.pl/ranking/film", "Ranking filmów | Filmweb"),
    ("https://maps.google.com/?q=Kraków", "Kraków – Mapy Google"),
    ("https://www.strava.com/dashboard", "Panel | Strava"),
]
MARKS = ["Przed demo", "Research przed deployem", "Koniec sprintu", "Plan na weekend", "Przed refaktorem"]


def app_running(data_dir):
    result = subprocess.run(["pgrep", "-f", f"--user-data-dir={data_dir}"], capture_output=True)
    return result.returncode == 0


def ms(when):
    return int(when.timestamp() * 1000)


def sessions(day, rhythm, rng):
    """(start, end) local datetimes of browsing on `day` for a rhythm."""
    weekend = day.weekday() >= 5
    out = []
    def at(h, m):
        return dt.datetime.combine(day, dt.time(h, m))
    if rhythm == "work" and not weekend:
        out.append((at(8, rng.randint(15, 59)), at(12, rng.randint(0, 45))))
        out.append((at(13, rng.randint(10, 50)), at(17, rng.randint(15, 59))))
        if rng.random() < 0.25:
            out.append((at(20, rng.randint(0, 30)), at(21, rng.randint(0, 50))))
    elif rhythm == "life":
        if weekend:
            out.append((at(rng.randint(9, 11), rng.randint(0, 59)), at(13, rng.randint(0, 50))))
            if rng.random() < 0.7:
                out.append((at(16, rng.randint(0, 59)), at(rng.randint(18, 22), rng.randint(0, 59))))
        else:
            out.append((at(7, rng.randint(20, 59)), at(8, rng.randint(10, 40))))
            out.append((at(19, rng.randint(30, 59)), at(rng.randint(22, 23), rng.randint(0, 50))))
    elif rhythm == "side" and (not weekend or rng.random() < 0.3):
        start = at(rng.choice([10, 11, 14, 15]), rng.randint(0, 59))
        out.append((start, start + dt.timedelta(minutes=rng.randint(40, 110))))
    return out


def simulate(workspace, profile, rhythm, pool, start_day, days, end, rng):
    lines, tabs, active = [], [], -1
    last_t = 0

    def frame(when):
        nonlocal last_t
        t = max(ms(when), last_t + 45_000)
        if t >= ms(end):
            return
        last_t = t
        lines.append({"t": float(t), "w": workspace, "p": profile, "a": active, "tabs": [list(tab) for tab in tabs]})

    for offset in range(days):
        day = start_day + dt.timedelta(days=offset)
        for begin, finish in sessions(day, rhythm, rng):
            when = begin
            # A session starts by reopening a few things.
            for _ in range(rng.randint(1, 3)):
                tab = rng.choice(pool)
                if tab not in tabs:
                    tabs.append(tab)
            active = len(tabs) - 1
            frame(when)
            while when < finish:
                when += dt.timedelta(minutes=rng.choice([2, 3, 5, 7, 9, 12, 18, 25]), seconds=rng.randint(0, 59))
                roll = rng.random()
                if roll < 0.38 and len(tabs) < 14:
                    tabs.append(rng.choice([t for t in pool if t not in tabs] or pool))
                    active = len(tabs) - 1
                elif roll < 0.66 and len(tabs) > 1:
                    tabs.pop(rng.randrange(len(tabs)))
                    active = min(active, len(tabs) - 1)
                elif roll < 0.85 and tabs:
                    active = rng.randrange(len(tabs))
                    choices = [t for t in pool if t not in tabs]
                    if choices:
                        tabs[active] = rng.choice(choices)
                elif tabs:
                    active = rng.randrange(len(tabs))
                frame(when)
            if rng.random() < 0.18:
                lines.append({"t": float(last_t + 1000), "w": workspace, "m": rng.choice(MARKS)})
            # Most tabs close before the night; a couple stay open.
            keep = rng.randint(0, 2)
            while len(tabs) > keep:
                tabs.pop(0)
            active = len(tabs) - 1
            frame(finish)
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data-dir", type=Path, default=ROOT / "build/rust-metal-profile")
    parser.add_argument("--days", type=int, default=7)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--replace", action="store_true", help="drop the existing timeline instead of keeping it")
    parser.add_argument("--force", action="store_true", help="write even while Zitter uses this profile")
    args = parser.parse_args()
    data_dir = args.data_dir.resolve()
    config = json.loads((data_dir / "ZitterWorkspaces.json").read_text(encoding="utf-8"))
    if app_running(data_dir) and not args.force:
        sys.exit("Zitter is running with this profile; quit it first (or pass --force).")
    path = data_dir / "ZitterTimeline.jsonl"
    existing = path.read_text(encoding="utf-8").splitlines() if path.exists() and not args.replace else []
    first_real = {}
    for line in existing:
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        if "tabs" in item:
            first_real[item["w"]] = min(first_real.get(item["w"], item["t"]), item["t"])
    now = dt.datetime.now().replace(second=0, microsecond=0)
    start_day = (now - dt.timedelta(days=args.days)).date()
    rhythms = ["life", "work", "side", "work"]
    seeded = []
    for index, space in enumerate(config.get("workspaces", [])):
        rng = random.Random(f"{args.seed}:{space['id']}")
        rhythm = rhythms[index % len(rhythms)]
        pool = READING + WORK[:6] if rhythm == "life" else WORK + READING[:5]
        end = now - dt.timedelta(minutes=10)
        if space["id"] in first_real:
            end = min(end, dt.datetime.fromtimestamp(first_real[space["id"]] / 1000) - dt.timedelta(minutes=1))
        lines = simulate(space["id"], space["profile_id"], rhythm, pool, start_day, args.days + 1, end, rng)
        seeded += lines
        print(f"{space['name']}: {sum('tabs' in l for l in lines)} changes, {sum('m' in l for l in lines)} snapshots ({rhythm})")
    seeded.sort(key=lambda item: item["t"])
    if path.exists():
        shutil.copy2(path, path.with_suffix(".jsonl.bak"))
    text = "".join(json.dumps(item, ensure_ascii=False, separators=(",", ":")) + "\n" for item in seeded)
    text += "".join(line + "\n" for line in existing if line.strip())
    tmp = path.with_suffix(".jsonl.tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)
    print(f"Wrote {path} ({len(text) // 1024} KB)")


if __name__ == "__main__":
    main()
