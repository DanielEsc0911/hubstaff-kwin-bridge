# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0] - 2026-10-22

First public release.

### Added

- `hubstaff-kwin-bridge` daemon: `org.gnome.Mutter.IdleMonitor`
  (`GetIdletime`, `AddIdleWatch`, `AddUserActiveWatch`, `RemoveWatch`,
  `WatchFired`) backed by KWin's `ext-idle-notify-v1`, using the
  input-only v2 notification when available.
- `hubstaff-kwin-launch` wrapper that selects Hubstaff's Mutter provider on
  KDE Wayland only.
- `hubstaff-kwin-patch` and a systemd path unit that keep Hubstaff's menu and
  autostart entries patched across Hubstaff updates.
- D-Bus activation and a systemd user unit, so the bridge starts on demand.
- `make install` / `make uninstall` (per-user, no root) and `make doctor`
  health check.
- `tools/probe_idlemonitor.py` for reverse-engineering IdleMonitor clients.
- Documentation: README, agent install playbook (`AGENTS.md`), `docs/`.

[Unreleased]: https://github.com/DanielEsc0911/hubstaff-kwin-bridge/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/DanielEsc0911/hubstaff-kwin-bridge/releases/tag/v0.1.0
