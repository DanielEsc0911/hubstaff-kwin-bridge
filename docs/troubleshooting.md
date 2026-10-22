# Troubleshooting

Start with:

```sh
make doctor
```

It's read-only, and each failing check comes with a suggested fix. The rest
of this page covers what it can't fix for you.

## Activity is still 0%

Work through these in order:

1. **Was Hubstaff restarted from the menu after installing?** A Hubstaff
   instance that was already running keeps its old environment. Quit it from
   the tray icon and open it from the Plasma app menu. `make doctor` checks
   this under "Running Hubstaff clients".
2. **Is the timer running?** Hubstaff only polls `GetIdletime` while tracking.
3. **Does the bridge see your input?** Run this while moving the mouse, then stop:

   ```sh
   watch -n1 busctl --user call org.gnome.Mutter.IdleMonitor \
       /org/gnome/Mutter/IdleMonitor/Core org.gnome.Mutter.IdleMonitor GetIdletime
   ```

   Expect `t 0` while moving and a growing number when you stop.
4. **Is Hubstaff calling the bridge?** Run it by hand with call logging:

   ```sh
   systemctl --user stop hubstaff-kwin-bridge
   ~/.local/libexec/hubstaff-kwin-bridge -v
   ```

   You should see `GetIdletime -> …` lines about once a second while the
   timer runs. Press Ctrl+C when done. The next call D-Bus-activates the
   service again.
5. **Wait for the block.** Hubstaff reports activity per 10-minute block. A
   block that started before the restart may still show 0%.

## `GetIdletime failed: … ServiceUnknown` / name not activatable

The session bus hasn't loaded the new D-Bus service file. `make install`
asks it to reload, but if that failed:

```sh
busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
```

If that doesn't help, log out and back in.

## The bridge exits immediately

```sh
journalctl --user -u hubstaff-kwin-bridge -n 50
```

| Message | Meaning |
|---|---|
| `Cannot connect to Wayland display` | Not started inside a Wayland session (`WAYLAND_DISPLAY` unset). |
| `Compositor lacks wl_seat or ext_idle_notifier_v1` | The compositor doesn't implement the idle protocol. Plasma 6 does. Very old KWin versions don't. |
| `Could not own org.gnome.Mutter.IdleMonitor` | Something else already owns the name: a GNOME session, another bridge you started by hand, or `tools/probe_idlemonitor.py`. |
| `Wayland connection lost` | The compositor restarted or the session ended. The unit restarts on failure. |

## The menu entry keeps losing the patch

The path unit should re-apply it automatically:

```sh
systemctl --user status hubstaff-kwin-patch.path
journalctl --user -u hubstaff-kwin-patch -n 20
```

If Hubstaff's updater renamed its desktop file, add the new path to
`data/hubstaff-kwin-patch.path` (`PathModified=`) and re-run `make install`.
Please also open an issue.

## System-wide Hubstaff install

The patcher only touches per-user desktop entries. If Hubstaff lives in
`/opt` or `/usr` and its menu entry is under `/usr/share/applications`, put a
per-user copy in place. Entries in `~/.local/share/applications` with the same
filename override system ones.

```sh
ls /usr/share/applications | grep -i hubstaff                # find the file name
cp /usr/share/applications/<name>.desktop ~/.local/share/applications/
~/.local/bin/hubstaff-kwin-patch
```

The copy won't follow future changes to the system entry. If an update moves
the binary, delete the copy and repeat. The launcher only needs `Hubstaff`
somewhere in the `Exec=` line.

## Hubstaff asks to install a GNOME Shell extension

This didn't happen with Hubstaff 1.9.8. If a newer version starts asking,
dismiss it. The extension can't run on KDE. Please open an issue with your
Hubstaff version.

## Screenshots stopped working

Screenshots on Wayland go through xdg-desktop-portal on KDE regardless of the
bridge. Check that `xdg-desktop-portal-kde` is installed and running:

```sh
systemctl --user status xdg-desktop-portal xdg-desktop-portal-kde
```

If screenshots work when Hubstaff is started *without* the launcher but not
with it, please open an issue. That means Hubstaff changed its GNOME code
path.

## Reporting a bug

Open an issue with:

- `make doctor` output
- distro, Plasma version (`plasmashell --version`), Hubstaff version
- `journalctl --user -u hubstaff-kwin-bridge -n 50`
- relevant lines from `~/.local/share/Hubstaff/logs/hubstaff.log` (remove
  anything private first)
