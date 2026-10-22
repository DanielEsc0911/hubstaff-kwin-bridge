# AGENTS.md: hubstaff-kwin-bridge

Instructions for AI coding agents (Claude Code, Codex, Cursor, Aider, …).

- **Asked to install, set up, fix or uninstall this?** Follow
  [Part 1: Install playbook](#part-1-install-playbook).
- **Asked to change the code?** Read
  [Part 2: Development context](#part-2-development-context).

What this project is: a per-user daemon plus launcher that makes the Hubstaff
Linux desktop client record keyboard/mouse activity on KDE Plasma (Wayland),
without modifying Hubstaff. Human docs: [README.md](README.md),
[docs/](docs/).

---

## Part 1: Install playbook

The whole install is about 4 commands. Run the steps in order. Every check
below is read-only unless it says otherwise.

### Ground rules

- **Never use `sudo` with `make install` or `make uninstall`.** The install is
  per-user (`~/.local`, `~/.config`). Under sudo, `$HOME` points at root and
  the install lands in the wrong place.
- **Do not modify anything in `~/Hubstaff/`** (Hubstaff's binaries). Only the
  `.desktop` entries get patched, and only by `hubstaff-kwin-patch`.
- **Do not set `XDG_CURRENT_DESKTOP` globally** (profile, environment.d,
  Plasma env scripts). That breaks Plasma. Only Hubstaff's process gets it,
  through the launcher.
- **Do not quit or kill Hubstaff yourself.** That stops the user's running
  timer. Ask the user to quit it from the tray icon and reopen it from the app menu.
- Package installs need root. Show the user the exact command and let them
  run it, or ask before running it.

### Step 1: Preconditions (read-only)

```sh
echo "$XDG_SESSION_TYPE / $XDG_CURRENT_DESKTOP"     # expect: wayland / KDE
ls ~/Hubstaff/HubstaffCLI.bin.x86_64                 # Hubstaff installed per-user
ls ~/.local/share/applications/*hubstaff*.desktop    # its menu entry
pkg-config --modversion wayland-protocols            # must be >= 1.40
```

Decide:

| Result | Action |
|---|---|
| Session is not `wayland` + `KDE` | Stop. Tell the user the bridge is only needed on KDE Plasma Wayland (X11 and GNOME work natively). Installing is harmless but does nothing there. |
| No `~/Hubstaff/` or no desktop entry in `~/.local/share/applications` | Hubstaff is missing or installed system-wide. See [docs/troubleshooting.md#system-wide-hubstaff-install](docs/troubleshooting.md#system-wide-hubstaff-install), ask the user before copying desktop files. |
| `pkg-config` fails or version < 1.40 | Go to Step 2 (dependencies). If the distro has no wayland-protocols ≥ 1.40, tell the user. The build can't work without it. |

### Step 2: Build dependencies (needs root, involve the user)

Detect the distro with `. /etc/os-release; echo $ID $ID_LIKE` and propose
the matching command:

```sh
# fedora
sudo dnf install gcc make pkgconf-pkg-config glib2-devel wayland-devel wayland-protocols-devel
# arch / manjaro / endeavouros
sudo pacman -S --needed base-devel glib2 wayland wayland-protocols
# opensuse-tumbleweed
sudo zypper install gcc make pkgconf glib2-devel wayland-devel wayland-protocols-devel
# debian / ubuntu (needs wayland-protocols >= 1.40)
sudo apt install build-essential pkg-config libglib2.0-dev libwayland-dev wayland-protocols
```

In Claude Code, the user can run it themselves with `! sudo dnf install …`.

### Step 3: Build and install (no sudo)

```sh
make            # -> build/hubstaff-kwin-bridge; fails early if wayland-protocols is too old
make install    # copies files, enables the path unit, patches Hubstaff's Exec= line
```

`make install` is idempotent. Re-running it upgrades in place and restarts the
bridge if it's running.

### Step 4: Verify

```sh
make doctor     # read-only checklist, exits non-zero on any FAIL
```

Before Hubstaff is restarted, all checks except the last section should pass.
The "Running Hubstaff clients" section will show `FAIL ... started without the
launcher` if Hubstaff was already running. That's expected at this point.

### Step 5: Restart Hubstaff (user action)

Tell the user, roughly:

> Quit Hubstaff from its tray icon (this pauses your timer), then open it again
> from the Plasma app menu and resume tracking.

Then run `make doctor` again. Every Hubstaff client process should show
`XDG_CURRENT_DESKTOP=GNOME (uses the bridge)` and the run should end with
`All checks passed.`

### Step 6: Optional live check

```sh
for i in 1 2 3 4 5; do busctl --user call org.gnome.Mutter.IdleMonitor \
  /org/gnome/Mutter/IdleMonitor/Core org.gnome.Mutter.IdleMonitor GetIdletime; sleep 1; done
```

While the user is hands-off, the value grows by ~1000 each second. If they move
the mouse during the loop, it drops to `t 0`. Hubstaff's 10-minute activity
block (UI or `~/.local/share/Hubstaff/logs/hubstaff.log`) should then be
non-zero.

### Fixing common failures

| `make doctor` says | Fix |
|---|---|
| `missing …` | `make install` |
| `… does not go through hubstaff-kwin-launch` | `~/.local/bin/hubstaff-kwin-patch` |
| `hubstaff-kwin-patch.path is not active` | `systemctl --user enable --now hubstaff-kwin-patch.path` |
| `GetIdletime failed: … not activatable / ServiceUnknown` | `busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig`, retry; else re-login |
| `GetIdletime failed` (other) | `journalctl --user -u hubstaff-kwin-bridge -n 50` and see [docs/troubleshooting.md](docs/troubleshooting.md) |
| `owned by '<x>', not the bridge` | Another IdleMonitor provider is running (real GNOME session, or a leftover `tools/probe_idlemonitor.py`). Stop it. |
| `pid N runs with XDG_CURRENT_DESKTOP=KDE` | Hubstaff wasn't started from the patched menu entry. Restart it from the menu (Step 5). |

### Uninstall

```sh
make uninstall    # no sudo; reverts files, units, D-Bus service and the Exec= patch
```

Then ask the user to restart Hubstaff.

---

## Part 2: Development context

### Goals

1. **Activity reaches Hubstaff.** Hubstaff's activity % must reflect input on
   KDE Wayland, by whatever technical route works.
2. **Transparent.** No extra steps for the user: opening Hubstaff from the
   Plasma app menu (or its autostart) is all it takes. Survives reboots and
   Hubstaff updates.
3. **Inert elsewhere.** On X11 sessions (e.g. XFCE, where Hubstaff works
   natively) and on GNOME, nothing changes.

### Reference environment

- Developed and verified on Fedora 44, KDE Plasma 6.7, KWin Wayland,
  wayland-protocols 1.49.
- Hubstaff 1.9.8, installed per-user at `~/Hubstaff/` (closed source, no
  public repo). Menu entry: `~/.local/share/applications/netsoft-com.netsoft.hubstaff.desktop`,
  `Exec="~/Hubstaff/HubstaffCLI.bin.x86_64" launch --url %u`. The CLI spawns
  `HubstaffClient.bin.x86_64`, which inherits the CLI's environment.
- Hubstaff log: `~/.local/share/Hubstaff/logs/hubstaff.log`.
- CLI control: `~/Hubstaff/HubstaffCLI.bin.x86_64 status` (tracking state, JSON),
  `... send resume|stop|start-project --project_id N`.

### How Hubstaff reads activity on Linux (from binary strings + logs)

`core::providers::linux::InputImpl<T>::GetActivity(bool& kb, bool& mouse)`
with two providers:

| Provider | Mechanism | KDE Wayland |
|---|---|---|
| `XScreenSaver` | `XScreenSaverQueryInfo` via XWayland | Fails: log says `XScreenSaver extension not available` |
| `Mutter` | D-Bus `org.gnome.Mutter.IdleMonitor` `/org/gnome/Mutter/IdleMonitor/Core` `GetIdletime() -> t` (ms) | Selected only when the desktop is GNOME |

Desktop detection reads `XDG_CURRENT_DESKTOP` / `XDG_SESSION_DESKTOP`
(confirmed: log `Client OS:` line shows `wayland;gnome;GNOME;...` and
`GetIdletime` gets called when both are set to GNOME).

Screenshots on Wayland go through `Wayland.cpp` / xdg-desktop-portal and
already work on KDE. With `XDG_CURRENT_DESKTOP=GNOME`, Hubstaff may try the
GNOME Shell screenshot D-Bus path or offer to install its GNOME Shell
extension (`~/Hubstaff/gnome-shell-extension/`, used for app/window tracking).
**Check both whenever the spoofing changes.**

### Architecture

```
KWin ──ext_idle_notifier_v1 (v2, get_input_idle_notification)──► hubstaff-kwin-bridge
                                                                    │ owns org.gnome.Mutter.IdleMonitor
Hubstaff (XDG_CURRENT_DESKTOP=GNOME) ──GetIdletime over session D-Bus──┘
```

- `src/hubstaff-kwin-bridge.c`: C, wayland-client + GDBus. One core
  notification at 1000 ms timeout tracks last input. `GetIdletime` returns 0
  while active, else `now - (idled_time - 1000ms)`. Also implements
  `AddIdleWatch`, `AddUserActiveWatch`, `RemoveWatch`, `WatchFired` for full
  Mutter compatibility. `-v` logs every call to stderr.
  v2 `get_input_idle_notification` ignores idle inhibitors (video playback
  doesn't count as input). It falls back to v1 `get_idle_notification` at
  runtime if the compositor only offers v1.
- `data/hubstaff-kwin-launch`: wrapper prefixed to Hubstaff's `Exec=`. Sets
  the GNOME env vars only when `XDG_SESSION_TYPE=wayland` and
  `XDG_CURRENT_DESKTOP` contains KDE. Passthrough otherwise.
- `data/hubstaff-kwin-patch` + `.path`/`.service` units: idempotently prefix
  `Exec=` in Hubstaff's menu entry and any Hubstaff autostart entry. The path
  unit re-runs it when Hubstaff updates rewrite those files.
- `data/org.gnome.Mutter.IdleMonitor.service` (D-Bus activation) →
  `hubstaff-kwin-bridge.service` (systemd user, `Type=dbus`,
  `ConditionEnvironment=WAYLAND_DISPLAY`): the bridge starts on first call,
  no autostart entry needed. On a real GNOME session Mutter owns the name first.
- `tools/doctor.sh` (`make doctor`): read-only install/health check. Keep it
  in sync when installed paths or units change.
- `tools/probe_idlemonitor.py`: Python stub of the interface returning a fixed
  idle time and logging calls, for reverse-engineering what Hubstaff calls.

### Build / run / test

```sh
make                 # build/hubstaff-kwin-bridge
make install         # ~/.local/{libexec,bin}, systemd user units, D-Bus service, patches menu entry
make uninstall       # reverts all of the above including the Exec= patch
make doctor          # health check

systemctl --user stop hubstaff-kwin-bridge   # free the bus name, then:
./build/hubstaff-kwin-bridge -v              # run by hand with call logging
busctl --user call org.gnome.Mutter.IdleMonitor /org/gnome/Mutter/IdleMonitor/Core \
    org.gnome.Mutter.IdleMonitor GetIdletime
journalctl --user -u hubstaff-kwin-bridge -f
```

There are no unit tests. Checks are:

- `make` with zero warnings (`-Wall -Wextra`)
- `shellcheck -s sh data/hubstaff-kwin-launch data/hubstaff-kwin-patch tools/doctor.sh`
- the end-to-end check: start Hubstaff from the menu, start the timer, use
  keyboard/mouse. `GetIdletime` calls should show up in the bridge's `-v`
  output, and the 10-minute activity block in the Hubstaff UI/log should be
  non-zero.

CI (`.github/workflows/ci.yml`) runs the build on Fedora plus shellcheck and
a Python syntax check.

### Conventions

- C: GLib style types, 4-space indent, `static` everything, single global `st`
  state struct, `LOG()` for verbose-only output. Keep it one file.
- Shell: POSIX `sh`, no bashisms, must pass shellcheck.
- Everything installs per-user. No root, no system paths.
- Keep `README.md`, `docs/`, `tools/doctor.sh` and Part 1 above consistent
  with any change to install paths, units or behaviour.

### Verified behaviour (2026-10-21, Hubstaff 1.9.8)

- With the GNOME env vars, Hubstaff polls `GetIdletime` ~1/s while the timer
  runs (once at startup otherwise). No `AddIdleWatch`/`AddUserActiveWatch`
  calls observed.
- A 2-minute test produced a non-zero activity block (37%) in the Hubstaff UI.
- No GNOME Shell extension prompt appeared, and portal screenshots were still captured.
- Goal 2 verified: quit Hubstaff, reopen from the Plasma menu → both
  `HubstaffClient` processes carry `XDG_CURRENT_DESKTOP=GNOME`, activity recorded.
- D-Bus activation starts `hubstaff-kwin-bridge.service` on first call.
  dbus-broker only sees new service files after `ReloadConfig` (done by
  `make install`) or a re-login.

### Known limitations (accepted, not being worked on)

Out of scope unless the goals change.

- The Mutter provider only exposes idle time, so the keyboard vs mouse split
  may not be reported separately.
- App/URL tracking on Wayland needs the GNOME Shell extension. KDE has no
  equivalent yet (would require a KWin script exposing the active window over
  D-Bus in whatever shape Hubstaff's extension uses). Already the case before
  the bridge: the 10-min block logs `Unkown application record`.
- A Hubstaff client already running with the KDE env (e.g. started some other
  way) won't pick up the Mutter provider until restarted via the wrapper.
- Only per-user Hubstaff installs (desktop entry in `~/.local/share/applications`)
  are patched automatically.
