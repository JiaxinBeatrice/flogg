# Named windows: TDD tracking

Branch: `feature/named-windows`. Not committed yet.

Goal: open a log in a chosen window from the command line, across flogg processes, and give
each window a name that replaces the hard to read file name as the main label.

## Decisions

- The display name and the `--window` name are the same thing.
- `flogg --window A new.log`: A exists, the file opens there, in any process. A does not
  exist, a new window named A opens.
- Names compare case-insensitively after trimming, and are unique across processes.
- Title: `A - new.log - flogg (build ...)`; the `#n` suffix is only shown without a name.
- Every named window owns a `QLocalServer` whose name is derived from the window name, so no
  registry or polling is needed. A `QLockFile` serializes claiming a name.
- Sending to a window that accepts the connection but does not answer is an error: its name
  is not taken over.
- Names are saved in the session.

## Baseline

- [x] Configure with Qt 6.7.3, Hyperscan, Vectorscan and Sentry off
- [x] Build `ci_build`
- [x] Run `ctest`, record the results: 3 of 3 passed on macOS arm64 (2026-11-01). Needs the
      `AGL` link removed from the local Qt 6.7.3 `FindWrapOpenGL.cmake`, the SDK dropped it.

## TDD items

Each item: write the test, watch it fail for the expected reason, implement, watch it pass.

### WindowEndpoint (`tests/unit`)

- [x] Claiming a free name succeeds
- [x] Claiming a name held by another endpoint is refused
- [x] Names differing only in case or surrounding spaces are the same name
- [x] Releasing a name makes it free again
- [x] A stale Unix socket file does not block claiming
- [x] Sending files delivers them, with the follow flag, and is acknowledged
- [x] Sending to a free name claims it instead of reporting not found
- [x] An endpoint that never answers reports a failure, within the timeout
- [x] Invalid messages are ignored and do not crash the receiver

### Session (`tests/unit`)

- [x] The window name survives saving and restoring `SessionInfo`
- [x] A session without names still loads

### MainWindow (`tests/ui`)

- [x] A name appears first in the title and hides `#n`
- [x] Clearing the name brings the original title back
- [x] Renaming to a name in use is refused and keeps the old name
- [x] A renamed window answers to the new name only
- [x] Files received by the endpoint open in that window
- [x] Closing a window frees its name
- [ ] A close cancelled by the save filters prompt keeps the name (not testable offscreen, only
      the order in `closeEvent` covers it)

### Command line (`tests/named_windows_e2e.sh`)

- [x] `--window` is parsed and trimmed; an empty name is rejected
- [x] `--window A file` with a running A forwards the file and exits with 0
- [x] `--window A file` without A opens a named window
- [x] Two simultaneous `--window A` starts end up in one window
- [x] A forward failure exits with a non-zero code, without a dialog

## After the tests

- [ ] `File > Rename window...` menu entry, with zh_TW and zh_CN translations
- [ ] `README.md`, `README.zh-TW.md` and `DOCUMENTATION.md`
- [ ] Performance evaluation as asked by `CLAUDE.md`
- [x] Full `ctest` run: 4 of 4 passed on macOS arm64 (2026-11-01)
- [ ] Windows build and test through GitHub Actions on the fork

## Open containing folder experiment

Behind `Options > Advanced > Debug`, off by default, the old code runs unchanged when off.

- [x] Missing file opens its folder, missing folder shows an error (`tests/ui/revealfile_test.cpp`)
- [x] `/select,"path"` is built with the quotes, also for a path without spaces
- [x] The setting is off by default, saved, and changed by the options dialog
- [ ] Try it on Windows; the `setNativeArguments` branch is not compiled on macOS
- [ ] Decide whether it becomes the default, then remove the switch

## Separate follow-ups

- [ ] Replace KDSingleApplication with `QLockFile`
- [ ] Move oneTBB back to upstream, with a benchmark
- [ ] Pin the tag based dependencies and third party Actions to commit SHAs
- [ ] Review the KArchive and exprtk differences
- [ ] Check whether flogg can load external SVG files with Qt 6.7.3 (CVE-2026-6210)
