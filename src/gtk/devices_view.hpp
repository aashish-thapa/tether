#pragma once

#include <filesystem>
#include <gtk/gtk.h>
#include <nlohmann/json.hpp>
#include <vector>

namespace tether::ui {

    // The device manager: discovered, remembered and connected devices beside a
    // pane for acting on the selected one.
    GtkWidget* devices_view_new();

    // Returns true when the event belonged to this view.
    bool devices_view_handle_event(const nlohmann::json& event);

    // Sends files to the one connected device, or asks which when there are several.
    void devices_view_send_files(std::vector<std::filesystem::path> paths);

    // Drops the queue of files waiting on a daemon that is no longer there.
    void devices_view_handle_disconnect();

    void devices_view_refresh();
    void devices_view_trigger_discovery();

} // namespace tether::ui
