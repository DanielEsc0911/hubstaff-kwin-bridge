# Contributing

Thanks for helping! This is a small project, so the process is light.

## Reporting bugs

Use the [bug report template](https://github.com/DanielEsc0911/hubstaff-kwin-bridge/issues/new?template=bug_report.yml)
and include `make doctor` output. [docs/troubleshooting.md](docs/troubleshooting.md)
covers the common cases first.

Reports that it **works** on a distro, Plasma version or Hubstaff version not
listed in the README's compatibility table are also very welcome. Open an
issue with the "works on" template.

## Pull requests

1. Read [docs/development.md](docs/development.md) (layout, running by hand,
   testing checklist, style).
2. Keep changes focused. One topic per PR.
3. Make sure `make` builds without warnings and shell scripts pass
   `shellcheck -s sh`.
4. If you change install paths, units or behaviour, update `README.md`,
   `AGENTS.md`, `docs/` and `tools/doctor.sh` in the same PR.
5. Add a line under `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md).

## Scope

In scope: making Hubstaff's activity tracking work on KDE Plasma Wayland,
transparently and without modifying Hubstaff. Other Wayland compositors are
welcome if the change stays small.

Out of scope: anything that fabricates or inflates activity. The bridge
reports real input only.
