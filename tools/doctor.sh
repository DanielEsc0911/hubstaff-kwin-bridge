#!/bin/sh
# Read-only health check for hubstaff-kwin-bridge. Prints one line per check
# and exits non-zero if any check FAILs. Safe to run repeatedly.
# Note: the GetIdletime check D-Bus-activates the bridge if it is not running.

LIBEXEC="$HOME/.local/libexec"
BINDIR="$HOME/.local/bin"
UNITDIR="$HOME/.config/systemd/user"
DBUSDIR="$HOME/.local/share/dbus-1/services"
APPDIR="$HOME/.local/share/applications"
LAUNCH="$BINDIR/hubstaff-kwin-launch"
BUS=org.gnome.Mutter.IdleMonitor
OBJ=/org/gnome/Mutter/IdleMonitor/Core

if [ -t 1 ]; then
    G='\033[32m' Y='\033[33m' R='\033[31m' B='\033[1m' N='\033[0m'
else
    G='' Y='' R='' B='' N=''
fi
fails=0
ok()   { printf '%b  ok%b  %s\n' "$G" "$N" "$*"; }
warn() { printf '%bwarn%b  %s\n' "$Y" "$N" "$*"; }
fail() { printf '%bFAIL%b  %s\n' "$R" "$N" "$*"; fails=$((fails + 1)); }
hint() { printf '        -> %s\n' "$*"; }
section() { printf '\n%b%s%b\n' "$B" "$*" "$N"; }

section "Session"
case "$XDG_SESSION_TYPE:$XDG_CURRENT_DESKTOP" in
    wayland:*KDE*) ok "KDE Plasma on Wayland ($XDG_CURRENT_DESKTOP)" ;;
    wayland:*GNOME*) warn "GNOME session: Mutter provides IdleMonitor itself, the bridge is not needed" ;;
    *) warn "not a KDE Wayland session ($XDG_SESSION_TYPE:$XDG_CURRENT_DESKTOP): the bridge stays inert here" ;;
esac

section "Hubstaff"
if [ -x "$HOME/Hubstaff/HubstaffCLI.bin.x86_64" ]; then
    ok "Hubstaff found in ~/Hubstaff"
else
    warn "$HOME/Hubstaff/HubstaffCLI.bin.x86_64 not found (non-standard install location?)"
fi
entries=$(ls "$APPDIR"/*hubstaff*.desktop "$HOME"/.config/autostart/*[Hh]ubstaff*.desktop 2>/dev/null)
if [ -z "$entries" ]; then
    fail "no Hubstaff desktop entry in $APPDIR"
    hint "see docs/troubleshooting.md, 'System-wide Hubstaff install'"
fi
for f in $entries; do
    grep -q '^Exec=.*Hubstaff' "$f" || continue
    if grep -q "^Exec=\"\?$LAUNCH" "$f"; then
        ok "launcher patched into $(basename "$f")"
    else
        fail "$(basename "$f") does not go through hubstaff-kwin-launch"
        hint "run: $BINDIR/hubstaff-kwin-patch"
    fi
done

section "Installed files"
for f in "$LIBEXEC/hubstaff-kwin-bridge" "$LAUNCH" "$BINDIR/hubstaff-kwin-patch" \
         "$UNITDIR/hubstaff-kwin-bridge.service" "$UNITDIR/hubstaff-kwin-patch.service" \
         "$UNITDIR/hubstaff-kwin-patch.path" "$DBUSDIR/$BUS.service"; do
    if [ -e "$f" ]; then ok "$f"; else fail "missing $f"; hint "run: make install"; fi
done
if systemctl --user is-active -q hubstaff-kwin-patch.path; then
    ok "hubstaff-kwin-patch.path is active (re-patches after Hubstaff updates)"
else
    fail "hubstaff-kwin-patch.path is not active"
    hint "run: systemctl --user enable --now hubstaff-kwin-patch.path"
fi

section "Bridge"
if [ -n "$WAYLAND_DISPLAY" ]; then
    reply=$(busctl --user call "$BUS" "$OBJ" "$BUS" GetIdletime 2>&1)
    case "$reply" in
        "t "*) ok "GetIdletime answered: ${reply#t } ms idle" ;;
        *) fail "GetIdletime failed: $reply"
           hint "freshly installed? reload D-Bus: busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig"
           hint "logs: journalctl --user -u hubstaff-kwin-bridge -n 50" ;;
    esac
    owner=$(busctl --user status "$BUS" 2>/dev/null | sed -n 's/^Comm=//p')
    case "$owner" in
        hubstaff-kwin*) ok "$BUS is owned by the bridge" ;;
        "") ;;
        *) warn "$BUS is owned by '$owner', not the bridge" ;;
    esac
else
    warn "WAYLAND_DISPLAY not set, skipping bridge checks"
fi

section "Running Hubstaff clients"
pids=$(pgrep -f 'HubstaffClient\.bin' 2>/dev/null)
if [ -z "$pids" ]; then
    warn "Hubstaff is not running (open it from the app menu, then re-run this check)"
fi
for pid in $pids; do
    desk=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | sed -n 's/^XDG_CURRENT_DESKTOP=//p')
    if [ "$desk" = GNOME ]; then
        ok "pid $pid runs with XDG_CURRENT_DESKTOP=GNOME (uses the bridge)"
    else
        fail "pid $pid runs with XDG_CURRENT_DESKTOP=$desk (started without the launcher)"
        hint "quit Hubstaff from its tray icon and reopen it from the app menu"
    fi
done

echo
if [ "$fails" -eq 0 ]; then
    printf '%bAll checks passed.%b\n' "$G" "$N"
else
    printf '%b%d check(s) failed.%b\n' "$R" "$fails" "$N"
fi
[ "$fails" -eq 0 ]
