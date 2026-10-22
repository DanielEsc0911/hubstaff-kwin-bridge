# Development

## Layout

```
src/hubstaff-kwin-bridge.c         the daemon (Wayland ext-idle-notify-v1 → D-Bus IdleMonitor)
data/hubstaff-kwin-launch          Exec= wrapper: GNOME env on KDE Wayland, passthrough elsewhere
data/hubstaff-kwin-patch           idempotent .desktop Exec= patcher
data/hubstaff-kwin-patch.{path,service}   re-run the patcher when Hubstaff rewrites its entries
data/hubstaff-kwin-bridge.service  systemd user unit (Type=dbus)
data/org.gnome.Mutter.IdleMonitor.service  D-Bus activation file → the unit above
tools/doctor.sh                    read-only health check (make doctor)
tools/probe_idlemonitor.py         logging stub of the IdleMonitor interface
build/                             generated protocol code + binary (git-ignored)
```

## Building

```sh
make          # generates protocol glue with wayland-scanner, then compiles
make clean
```

The protocol XML comes from your system's wayland-protocols (`pkg-config
--variable=pkgdatadir wayland-protocols`). The build stops early with a clear
error if that copy lacks `ext_idle_notifier_v1` version 2.

The code must compile with **zero warnings** under `-Wall -Wextra`.

## Running by hand

The installed unit owns the bus name, so stop it first:

```sh
systemctl --user stop hubstaff-kwin-bridge
./build/hubstaff-kwin-bridge -v
```

`-v` logs each `idle`/`active` transition and every D-Bus call:

```
ext_idle_notifier_v1 version 2
GetIdletime -> 0
idle
GetIdletime -> 1003
GetIdletime -> 2004
active
```

Poke it from another terminal:

```sh
busctl --user call org.gnome.Mutter.IdleMonitor /org/gnome/Mutter/IdleMonitor/Core \
    org.gnome.Mutter.IdleMonitor GetIdletime
busctl --user call org.gnome.Mutter.IdleMonitor /org/gnome/Mutter/IdleMonitor/Core \
    org.gnome.Mutter.IdleMonitor AddIdleWatch t 5000
busctl --user monitor org.gnome.Mutter.IdleMonitor     # watch WatchFired signals
```

When you're done, Ctrl+C. The next call D-Bus-activates the installed unit again.

## Seeing what Hubstaff calls

[`tools/probe_idlemonitor.py`](../tools/probe_idlemonitor.py) owns the same bus
name, returns a fixed idle time and logs every call with its sender:

```sh
systemctl --user stop hubstaff-kwin-bridge
python3 tools/probe_idlemonitor.py 0        # or e.g. 600000 to pretend 10 min idle
# in another terminal, start Hubstaff with the GNOME env:
XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_DESKTOP=gnome ~/Hubstaff/HubstaffCLI.bin.x86_64 launch
```

Needs PyGObject (`python3-gobject` on Fedora).

## Testing checklist

There's no automated test suite, since the interesting part depends on a live
compositor and a closed-source client. Before sending a change:

- [ ] `make` builds without warnings
- [ ] `shellcheck -s sh data/hubstaff-kwin-launch data/hubstaff-kwin-patch tools/doctor.sh` is clean
- [ ] `make install && make doctor` passes (after restarting Hubstaff from the menu)
- [ ] `GetIdletime` returns `0` while moving the mouse and grows when idle
- [ ] Hubstaff shows a non-zero activity block after a few minutes of use
- [ ] If the launcher or env spoofing changed: screenshots still captured, no
      GNOME Shell extension prompt
- [ ] On an X11 session the launcher is a passthrough (`XDG_CURRENT_DESKTOP`
      unchanged in `/proc/<HubstaffClient pid>/environ`)
- [ ] `make uninstall` leaves no files behind and restores the original `Exec=`

CI runs the build on Fedora, shellcheck, and a Python syntax check.

## Style

- **C:** GLib types (`gboolean`, `guint`, …), 4-space indent, ~100 columns,
  everything `static`, one global state struct `st`, `LOG()` for verbose-only
  output, `g_printerr` for real errors. Keep it a single file.
- **Shell:** POSIX `sh` only, shellcheck-clean.
- **Install paths:** per-user only (`~/.local`, `~/.config`). If you change one,
  update the Makefile, the units, `tools/doctor.sh`, `README.md` and
  `AGENTS.md` together.
