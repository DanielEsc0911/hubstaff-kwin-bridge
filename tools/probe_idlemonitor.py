#!/usr/bin/env python3
"""Probe: owns org.gnome.Mutter.IdleMonitor on the session bus and logs every call.

Returns a fixed idle time (default 0 ms). Used to discover which parts of the
interface the Hubstaff client actually calls.
"""
import sys
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

BUS_NAME = "org.gnome.Mutter.IdleMonitor"
OBJ_PATH = "/org/gnome/Mutter/IdleMonitor/Core"
XML = """
<node>
  <interface name="org.gnome.Mutter.IdleMonitor">
    <method name="GetIdletime"><arg type="t" name="idletime" direction="out"/></method>
    <method name="AddIdleWatch">
      <arg type="t" name="interval" direction="in"/>
      <arg type="u" name="id" direction="out"/>
    </method>
    <method name="AddUserActiveWatch"><arg type="u" name="id" direction="out"/></method>
    <method name="RemoveWatch"><arg type="u" name="id" direction="in"/></method>
    <signal name="WatchFired"><arg type="u" name="id"/></signal>
  </interface>
</node>
"""

IDLE_MS = int(sys.argv[1]) if len(sys.argv) > 1 else 0
next_id = 1


def log(msg):
    print(f"{time.strftime('%H:%M:%S')} {msg}", flush=True)


def on_call(conn, sender, path, iface, method, params, inv):
    global next_id
    log(f"{sender} {method}{params.unpack()}")
    if method == "GetIdletime":
        inv.return_value(GLib.Variant("(t)", (IDLE_MS,)))
    elif method in ("AddIdleWatch", "AddUserActiveWatch"):
        inv.return_value(GLib.Variant("(u)", (next_id,)))
        next_id += 1
    else:
        inv.return_value(None)


def on_bus(conn, name):
    info = Gio.DBusNodeInfo.new_for_xml(XML)
    conn.register_object(OBJ_PATH, info.interfaces[0], on_call, None, None)


Gio.bus_own_name(
    Gio.BusType.SESSION, BUS_NAME, Gio.BusNameOwnerFlags.NONE,
    on_bus, lambda *a: log("name acquired"), lambda *a: (log("name lost"), sys.exit(1)),
)
GLib.MainLoop().run()
