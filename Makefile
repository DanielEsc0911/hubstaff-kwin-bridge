PROTO_XML := $(shell pkg-config --variable=pkgdatadir wayland-protocols)/staging/ext-idle-notify/ext-idle-notify-v1.xml
BUILD := build
BIN := $(BUILD)/hubstaff-kwin-bridge
CFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -I$(BUILD) $(shell pkg-config --cflags gio-2.0 gio-unix-2.0 wayland-client)
LDLIBS += $(shell pkg-config --libs gio-2.0 gio-unix-2.0 wayland-client)

all: $(BIN)

$(BUILD)/ext-idle-notify-v1-client-protocol.h: $(PROTO_XML) | $(BUILD)
	@grep -q 'name="ext_idle_notifier_v1" version="[2-9]"' $< || \
		{ echo "error: $< lacks ext_idle_notifier_v1 version 2; wayland-protocols >= 1.40 is required" >&2; exit 1; }
	wayland-scanner client-header $< $@

$(BUILD)/ext-idle-notify-v1-protocol.c: $(PROTO_XML) | $(BUILD)
	wayland-scanner private-code $< $@

$(BIN): src/hubstaff-kwin-bridge.c $(BUILD)/ext-idle-notify-v1-protocol.c $(BUILD)/ext-idle-notify-v1-client-protocol.h
	$(CC) $(CFLAGS) -o $@ src/hubstaff-kwin-bridge.c $(BUILD)/ext-idle-notify-v1-protocol.c $(LDLIBS)

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean

# --- user install -----------------------------------------------------------
LIBEXEC := $(HOME)/.local/libexec
BINDIR := $(HOME)/.local/bin
UNITDIR := $(HOME)/.config/systemd/user
DBUSDIR := $(HOME)/.local/share/dbus-1/services

install: $(BIN)
	install -Dm755 $(BIN) $(LIBEXEC)/hubstaff-kwin-bridge
	install -Dm755 data/hubstaff-kwin-launch $(BINDIR)/hubstaff-kwin-launch
	install -Dm755 data/hubstaff-kwin-patch $(BINDIR)/hubstaff-kwin-patch
	install -Dm644 data/hubstaff-kwin-bridge.service $(UNITDIR)/hubstaff-kwin-bridge.service
	install -Dm644 data/hubstaff-kwin-patch.service $(UNITDIR)/hubstaff-kwin-patch.service
	install -Dm644 data/hubstaff-kwin-patch.path $(UNITDIR)/hubstaff-kwin-patch.path
	install -Dm644 data/org.gnome.Mutter.IdleMonitor.service $(DBUSDIR)/org.gnome.Mutter.IdleMonitor.service
	systemctl --user daemon-reload
	-busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig
	systemctl --user enable --now hubstaff-kwin-patch.path
	systemctl --user try-restart hubstaff-kwin-bridge.service
	$(BINDIR)/hubstaff-kwin-patch

uninstall:
	-systemctl --user disable --now hubstaff-kwin-patch.path hubstaff-kwin-bridge.service
	sed -i 's|^Exec="$(BINDIR)/hubstaff-kwin-launch" |Exec=|' \
		$(HOME)/.local/share/applications/*hubstaff*.desktop $(HOME)/.config/autostart/*[Hh]ubstaff*.desktop 2>/dev/null || true
	rm -f $(LIBEXEC)/hubstaff-kwin-bridge $(BINDIR)/hubstaff-kwin-launch $(BINDIR)/hubstaff-kwin-patch \
		$(UNITDIR)/hubstaff-kwin-bridge.service $(UNITDIR)/hubstaff-kwin-patch.service \
		$(UNITDIR)/hubstaff-kwin-patch.path $(DBUSDIR)/org.gnome.Mutter.IdleMonitor.service
	systemctl --user daemon-reload
	-busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig

doctor:
	@sh tools/doctor.sh

.PHONY: install uninstall doctor
