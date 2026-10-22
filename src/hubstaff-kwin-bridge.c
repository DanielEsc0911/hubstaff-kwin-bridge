/*
 * hubstaff-kwin-bridge: implements org.gnome.Mutter.IdleMonitor on the session
 * bus, backed by the Wayland ext-idle-notify-v1 protocol (KWin).
 *
 * Hubstaff on Wayland only reads activity from Mutter's IdleMonitor. This
 * daemon answers those calls with the compositor's real input-idle state.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <wayland-client.h>

#include "ext-idle-notify-v1-client-protocol.h"

#define BUS_NAME "org.gnome.Mutter.IdleMonitor"
#define OBJ_PATH "/org/gnome/Mutter/IdleMonitor/Core"

/* Timeout of the notification that tracks "last input" for GetIdletime.
 * GetIdletime reports 0 while input happened within this window. */
#define CORE_TIMEOUT_MS 1000

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.gnome.Mutter.IdleMonitor'>"
    "    <method name='GetIdletime'>"
    "      <arg type='t' name='idletime' direction='out'/>"
    "    </method>"
    "    <method name='AddIdleWatch'>"
    "      <arg type='t' name='interval' direction='in'/>"
    "      <arg type='u' name='id' direction='out'/>"
    "    </method>"
    "    <method name='AddUserActiveWatch'>"
    "      <arg type='u' name='id' direction='out'/>"
    "    </method>"
    "    <method name='RemoveWatch'>"
    "      <arg type='u' name='id' direction='in'/>"
    "    </method>"
    "    <signal name='WatchFired'>"
    "      <arg type='u' name='id'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

typedef struct {
    guint id;
    gboolean user_active; /* one-shot, fires on next input */
    struct ext_idle_notification_v1 *notif;
} Watch;

static struct {
    struct wl_display *display;
    struct wl_seat *seat;
    struct ext_idle_notifier_v1 *notifier;
    guint notifier_version;

    struct ext_idle_notification_v1 *core;
    gboolean idle;
    gint64 last_input_us; /* monotonic, valid while idle */

    GDBusConnection *bus;
    GHashTable *watches; /* id -> Watch* */
    guint next_watch_id;
    GMainLoop *loop;
    gboolean verbose;
} st;

#define LOG(...)                                                               \
    do {                                                                       \
        if (st.verbose) g_printerr(__VA_ARGS__);                               \
    } while (0)

/* --- Wayland ------------------------------------------------------------- */

static struct ext_idle_notification_v1 *new_notification(guint32 timeout_ms) {
    if (st.notifier_version >= 2)
        return ext_idle_notifier_v1_get_input_idle_notification(st.notifier, timeout_ms, st.seat);
    return ext_idle_notifier_v1_get_idle_notification(st.notifier, timeout_ms, st.seat);
}

static void emit_watch_fired(guint id) {
    LOG("WatchFired(%u)\n", id);
    g_dbus_connection_emit_signal(st.bus, NULL, OBJ_PATH, BUS_NAME, "WatchFired",
                                  g_variant_new("(u)", id), NULL);
}

static void core_idled(void *data, struct ext_idle_notification_v1 *n) {
    st.idle = TRUE;
    st.last_input_us = g_get_monotonic_time() - (gint64)CORE_TIMEOUT_MS * 1000;
    LOG("idle\n");
}

static void core_resumed(void *data, struct ext_idle_notification_v1 *n) {
    st.idle = FALSE;
    LOG("active\n");

    /* User-active watches are one-shot. */
    GHashTableIter it;
    gpointer key, value;
    GList *fired = NULL;
    g_hash_table_iter_init(&it, st.watches);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        Watch *w = value;
        if (w->user_active) fired = g_list_prepend(fired, GUINT_TO_POINTER(w->id));
    }
    for (GList *l = fired; l; l = l->next) {
        guint id = GPOINTER_TO_UINT(l->data);
        g_hash_table_remove(st.watches, GUINT_TO_POINTER(id));
        emit_watch_fired(id);
    }
    g_list_free(fired);
}

static const struct ext_idle_notification_v1_listener core_listener = {
    .idled = core_idled,
    .resumed = core_resumed,
};

static void watch_idled(void *data, struct ext_idle_notification_v1 *n) {
    emit_watch_fired(((Watch *)data)->id);
}

static void watch_resumed(void *data, struct ext_idle_notification_v1 *n) {}

static const struct ext_idle_notification_v1_listener watch_listener = {
    .idled = watch_idled,
    .resumed = watch_resumed,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t version) {
    if (strcmp(iface, wl_seat_interface.name) == 0 && !st.seat) {
        st.seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
    } else if (strcmp(iface, ext_idle_notifier_v1_interface.name) == 0) {
        st.notifier_version = MIN(version, 2);
        st.notifier = wl_registry_bind(reg, name, &ext_idle_notifier_v1_interface,
                                       st.notifier_version);
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static gboolean on_wayland_fd(gint fd, GIOCondition cond, gpointer data) {
    if (cond & (G_IO_ERR | G_IO_HUP) || wl_display_dispatch(st.display) < 0) {
        g_printerr("Wayland connection lost\n");
        g_main_loop_quit(st.loop);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void flush(void) {
    wl_display_flush(st.display);
}

/* --- D-Bus --------------------------------------------------------------- */

static void watch_free(gpointer p) {
    Watch *w = p;
    if (w->notif) ext_idle_notification_v1_destroy(w->notif);
    g_free(w);
    flush();
}

static guint64 idletime_ms(void) {
    if (!st.idle) return 0;
    return (guint64)((g_get_monotonic_time() - st.last_input_us) / 1000);
}

static void on_method_call(GDBusConnection *conn, const gchar *sender, const gchar *path,
                           const gchar *iface, const gchar *method, GVariant *params,
                           GDBusMethodInvocation *inv, gpointer data) {
    if (g_strcmp0(method, "GetIdletime") == 0) {
        guint64 ms = idletime_ms();
        LOG("GetIdletime -> %" G_GUINT64_FORMAT "\n", ms);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(t)", ms));
    } else if (g_strcmp0(method, "AddIdleWatch") == 0) {
        guint64 interval;
        g_variant_get(params, "(t)", &interval);
        if (interval == 0 || interval > G_MAXUINT32) {
            g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                                  "Invalid interval");
            return;
        }
        Watch *w = g_new0(Watch, 1);
        w->id = st.next_watch_id++;
        w->notif = new_notification((guint32)interval);
        ext_idle_notification_v1_add_listener(w->notif, &watch_listener, w);
        flush();
        g_hash_table_insert(st.watches, GUINT_TO_POINTER(w->id), w);
        LOG("AddIdleWatch(%" G_GUINT64_FORMAT ") -> %u\n", interval, w->id);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", w->id));
    } else if (g_strcmp0(method, "AddUserActiveWatch") == 0) {
        Watch *w = g_new0(Watch, 1);
        w->id = st.next_watch_id++;
        w->user_active = TRUE;
        g_hash_table_insert(st.watches, GUINT_TO_POINTER(w->id), w);
        LOG("AddUserActiveWatch -> %u\n", w->id);
        g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", w->id));
    } else if (g_strcmp0(method, "RemoveWatch") == 0) {
        guint id;
        g_variant_get(params, "(u)", &id);
        LOG("RemoveWatch(%u)\n", id);
        g_hash_table_remove(st.watches, GUINT_TO_POINTER(id));
        g_dbus_method_invocation_return_value(inv, NULL);
    }
}

static const GDBusInterfaceVTable vtable = {.method_call = on_method_call};

static void on_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer data) {
    GError *err = NULL;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    st.bus = conn;
    if (!g_dbus_connection_register_object(conn, OBJ_PATH, info->interfaces[0], &vtable,
                                           NULL, NULL, &err)) {
        g_printerr("register_object: %s\n", err->message);
        exit(1);
    }
    g_dbus_node_info_unref(info);
}

static void on_name_lost(GDBusConnection *conn, const gchar *name, gpointer data) {
    g_printerr("Could not own %s (already owned or no session bus)\n", name);
    exit(1);
}

/* --- main ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    st.verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    st.next_watch_id = 1;
    st.watches = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, watch_free);

    st.display = wl_display_connect(NULL);
    if (!st.display) {
        g_printerr("Cannot connect to Wayland display: %s\n", strerror(errno));
        return 1;
    }
    struct wl_registry *reg = wl_display_get_registry(st.display);
    wl_registry_add_listener(reg, &registry_listener, NULL);
    wl_display_roundtrip(st.display);
    if (!st.seat || !st.notifier) {
        g_printerr("Compositor lacks wl_seat or ext_idle_notifier_v1\n");
        return 1;
    }
    LOG("ext_idle_notifier_v1 version %u\n", st.notifier_version);

    st.core = new_notification(CORE_TIMEOUT_MS);
    ext_idle_notification_v1_add_listener(st.core, &core_listener, NULL);
    flush();

    st.loop = g_main_loop_new(NULL, FALSE);
    g_unix_fd_add(wl_display_get_fd(st.display), G_IO_IN | G_IO_ERR | G_IO_HUP, on_wayland_fd,
                  NULL);
    g_bus_own_name(G_BUS_TYPE_SESSION, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired,
                   NULL, on_name_lost, NULL, NULL);

    g_main_loop_run(st.loop);
    return 1;
}
