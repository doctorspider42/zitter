# Zitter — notes for Claude

## UI tests

- Run the **full** browser UI suite (`zitter_prototype_ui_lab` without a narrow
  `--gtest_filter`) only after the user explicitly agrees, each time. It shows and
  focuses a browser window over and over for ~12 minutes, so the Mac is unusable
  meanwhile. Say how long it takes when you ask.
- Without asking: Rust tests (`out/ZitterOptimized/sidebar_scene_unittests`),
  Python tests (`python3 -m unittest discover -s tests -p 'test_*.py'`) and single
  browser tests named with `--gtest_filter` when a change needs them.

## Shared build directory

Several sessions stage into `.chromium/src/zitter_ui_lab` and build in
`.chromium/src/out/ZitterOptimized`. Before staging or running ninja there, make sure
no other session is building, and agree on the window with the sessions that use it.
