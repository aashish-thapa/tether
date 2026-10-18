#include "tether/session_lock.hpp"
#include "tether/wayland.hpp"

#include <tether/log.hpp>

#include <gio/gio.h>

namespace tether {

    namespace {

        constexpr const char* LOGIND_NAME = "org.freedesktop.login1";
        constexpr const char* SESSION_PATH = "/org/freedesktop/login1/session/auto";
        constexpr const char* SESSION_IFACE = "org.freedesktop.login1.Session";

        constexpr int CALL_TIMEOUT_MS = 1000;

        // logind is on the system bus, unlike the session services elsewhere here.
        GDBusConnection* bus() {
            static GDBusConnection* connection = [] {
                GError* error = nullptr;
                GDBusConnection* c = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
                if (!c) {
                    debug::log(WARN, "lock: no system bus: {}", error ? error->message : "unknown");
                    g_clear_error(&error);
                }
                return c;
            }();
            return connection;
        }

        bool bool_property(GDBusConnection* connection, const char* name) {
            GError* error = nullptr;
            GVariant* reply = g_dbus_connection_call_sync(connection,
                                                          LOGIND_NAME,
                                                          SESSION_PATH,
                                                          "org.freedesktop.DBus.Properties",
                                                          "Get",
                                                          g_variant_new("(ss)", SESSION_IFACE, name),
                                                          G_VARIANT_TYPE("(v)"),
                                                          G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                                          CALL_TIMEOUT_MS,
                                                          nullptr,
                                                          &error);
            if (!reply) {
                g_clear_error(&error);
                return false;
            }

            GVariant* boxed = nullptr;
            g_variant_get(reply, "(v)", &boxed);
            const bool value = boxed && g_variant_get_boolean(boxed);
            if (boxed)
                g_variant_unref(boxed);
            g_variant_unref(reply);
            return value;
        }

        bool lock_via_logind() {
            GDBusConnection* connection = bus();
            if (!connection)
                return false;

            if (bool_property(connection, "LockedHint")) {
                debug::log(INFO, "lock: session is already locked");
                return true;
            }

            GError* error = nullptr;
            GVariant* reply = g_dbus_connection_call_sync(connection,
                                                          LOGIND_NAME,
                                                          SESSION_PATH,
                                                          SESSION_IFACE,
                                                          "Lock",
                                                          nullptr,
                                                          nullptr,
                                                          G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                                          CALL_TIMEOUT_MS,
                                                          nullptr,
                                                          &error);
            if (!reply) {
                debug::log(WARN, "lock: logind refused: {}", error ? error->message : "unknown");
                g_clear_error(&error);
                return false;
            }
            g_variant_unref(reply);
            return true;
        }

        bool run_command(const std::string& command) {
            gchar** argv = nullptr;
            GError* error = nullptr;
            if (!g_shell_parse_argv(command.c_str(), nullptr, &argv, &error)) {
                debug::log(WARN, "lock: cannot parse lock_command: {}", error ? error->message : "unknown");
                g_clear_error(&error);
                return false;
            }

            const bool spawned = g_spawn_async(nullptr,
                                               argv,
                                               nullptr,
                                               static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_CLOEXEC_PIPES),
                                               nullptr,
                                               nullptr,
                                               nullptr,
                                               &error);
            if (!spawned) {
                debug::log(WARN, "lock: {} failed to start: {}", command, error ? error->message : "unknown");
                g_clear_error(&error);
            }
            g_strfreev(argv);
            return spawned;
        }

    } // namespace

    bool lock_session(const std::string& command) {
        if (!command.empty())
            return run_command(command);
        return lock_via_logind();
    }

    bool session_inactive() {
        if (wayland_idle())
            return true;
        GDBusConnection* connection = bus();
        // GNOME has no ext-idle-notify, but gnome-session sets IdleHint.
        return connection && (bool_property(connection, "LockedHint") || bool_property(connection, "IdleHint"));
    }

} // namespace tether
