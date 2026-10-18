#include "tether/bluetooth/profile_supervisor.hpp"
#include "tether/bluetooth/objects.hpp"
#include "tether/log.hpp"
#include <tether/i18n.hpp>

#include <algorithm>
#include <cctype>

namespace tether::bluetooth {

    namespace {

        std::string lowered(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        bool contains(const std::string& haystack, const char* needle) {
            return haystack.find(needle) != std::string::npos;
        }

    } // namespace

    const char* to_string(ObexError error) {
        switch (error) {
        case ObexError::None:
            return "none";
        case ObexError::Forbidden:
            return "forbidden";
        case ObexError::Busy:
            return "busy";
        case ObexError::NoRecord:
            return "no_record";
        case ObexError::Unavailable:
            return "unavailable";
        case ObexError::NoDaemon:
            return "no_daemon";
        default:
            return "other";
        }
    }

    ObexError classify_obex_error(const std::string& message) {
        if (message.empty())
            return ObexError::None;

        const std::string text = lowered(message);

        // Bus-level, not phone-level: obexd is absent so the name has no owner
        // and no .service file to start one.
        if (contains(text, "not activatable") || contains(text, "serviceunknown") ||
            contains(text, "was not provided by any .service files"))
            return ObexError::NoDaemon;

        // permission toggle is off on the phone
        if (contains(text, "forbidden") || contains(text, "0x43"))
            return ObexError::Forbidden;

        if (contains(text, "connection refused") || contains(text, "(111)"))
            return ObexError::Busy;

        if (contains(text, "unable to find service record") || contains(text, "no such service"))
            return ObexError::NoRecord;

        if (contains(text, "host is down") || contains(text, "no route to host") || contains(text, "not connected") ||
            contains(text, "page timeout"))
            return ObexError::Unavailable;

        return ObexError::Other;
    }

    std::string obex_error_advice(ObexError error, const std::string& profile, bool ertm_disabled) {
        const bool map = profile == "map";
        // TRANSLATORS: the feature name Tether shows for this profile.
        const std::string label = map ? _("Messages") : _("Contacts");
        // TRANSLATORS: these two are toggle names in the iPhone's own Settings app.
        // Use Apple's wording for the target language, not a literal translation.
        const std::string toggle = map ? _("Show Message Notifications") : _("Sync Contacts");

        switch (error) {
        case ObexError::Forbidden:
            // TRANSLATORS: {} is an iPhone Settings toggle name.
            return tr_format(_("Enable \"{}\" for this computer in the iPhone's Settings > Bluetooth > (i). "
                               "This is a permission, not a pairing problem."),
                             toggle);
        case ObexError::Busy:
            // TRANSLATORS: {0} is Messages or Contacts, {1} is the profile name (map or pbap).
            return tr_format(_("{0} is unavailable because another computer is using the iPhone's {1} session. "
                               "Disconnect it there, then this will connect on its own."),
                             label,
                             profile);
        case ObexError::NoRecord:
            // obexd reports its own failed ERTM connect as a missing record too
            if (ertm_disabled)
                // TRANSLATORS: {} is Messages or Contacts.
                return tr_format(_("{} cannot connect because the kernel has L2CAP ERTM disabled "
                                   "(bluetooth.disable_ertm). Run tether --bt-setup; nothing is wrong on the iPhone."),
                                 label);
            // obexd reports a missing record whenever its SDP fetch is refused, so the real cause is usually one layer
            // down.
            // TRANSLATORS: {0} is Messages or Contacts, {1} is an iPhone Settings toggle name.
            return tr_format(_("{0} could not be fetched from the iPhone. Usually another computer holds the "
                               "phone's Bluetooth session, or \"{1}\" is off in Settings > Bluetooth > (i). "
                               "Check both; re-pairing is not the fix."),
                             label,
                             toggle);
        case ObexError::Unavailable:
            return tr_format(_("{} is unreachable; waiting for the iPhone to come back."), label);
        case ObexError::NoDaemon:
            return tr_format(_("{} needs BlueZ's OBEX daemon, which is not installed on this computer. Install it "
                               "(bluez-obex on Arch, bluez-obexd on Debian and Ubuntu). Nothing needs changing on "
                               "the iPhone."),
                             label);
        case ObexError::Other:
            return tr_format(_("{} could not be opened."), label);
        default:
            return {};
        }
    }

    ProfileSupervisor::ProfileSupervisor(ProfileOps& ops) : ops_(ops) {}

    ProfileSupervisor::~ProfileSupervisor() { reset(); }

    void ProfileSupervisor::reset() {
        if (!map_path_.empty())
            ops_.remove_session(map_path_);
        if (!pbap_path_.empty())
            ops_.remove_session(pbap_path_);
        map_path_.clear();
        pbap_path_.clear();
        map_retried_ = false;
        pbap_retried_ = false;
        next_attempt_ = 0;
        status_.map_open = false;
        status_.pbap_open = false;
    }

    void ProfileSupervisor::drop_map() {
        if (!map_path_.empty())
            ops_.remove_session(map_path_);
        map_path_.clear();
        map_retried_ = false;
        status_.map_open = false;
        // Reopen on the next tick rather than waiting out the steady poll.
        next_attempt_ = 0;
    }

    void ProfileSupervisor::set_held(bool map, bool pbap, bool by_lock) {
        held_by_lock_ = by_lock;
        if (map != status_.map_held) {
            status_.map_held = map;
            if (map) {
                if (!map_path_.empty())
                    ops_.remove_session(map_path_);
                map_path_.clear();
                map_retried_ = false;
                status_.map_open = false;
                status_.map_error = ObexError::None;
            }
            next_attempt_ = 0;
        }
        if (pbap != status_.pbap_held) {
            status_.pbap_held = pbap;
            if (pbap) {
                if (!pbap_path_.empty())
                    ops_.remove_session(pbap_path_);
                pbap_path_.clear();
                pbap_retried_ = false;
                status_.pbap_open = false;
                status_.pbap_error = ObexError::None;
            }
            next_attempt_ = 0;
        }
    }

    bool
        ProfileSupervisor::open_profile(const std::string& target, std::string& path, ObexError& error, bool& retried) {
        std::string err;
        // create_session() clears `path` on failure, so the session that may need
        // dropping has to be captured before the call.
        const std::string previous = path;
        path = ops_.create_session(target, err);
        if (!path.empty()) {
            error = ObexError::None;
            retried = false;
            return true;
        }

        error = classify_obex_error(err);
        debug::log(WARN, "bluetooth: {} session failed ({}): {}", target, to_string(error), err);

        // A stale session from a previous run can present as Forbidden. Drop only
        // this profile's session and try once more, rather than restarting obexd.
        if (error == ObexError::Forbidden && !retried) {
            retried = true;
            ops_.remove_session(previous);
            path = ops_.create_session(target, err);
            if (!path.empty()) {
                error = ObexError::None;
                return true;
            }
            error = classify_obex_error(err);
        }
        return false;
    }

    bool ProfileSupervisor::tick(int64_t now, bool device_ready) {
        ProfileStatus previous = status_;

        if (!device_ready) {
            status_.reason = _("Waiting for the iPhone.");
            return !(status_ == previous);
        }

        const bool map_done = status_.map_open || status_.map_held;
        const bool pbap_done = status_.pbap_open || status_.pbap_held;

        if ((!map_done || !pbap_done) && now >= next_attempt_) {
            if (!map_done)
                status_.map_open = open_profile("map", map_path_, status_.map_error, map_retried_);
            if (!pbap_done)
                status_.pbap_open = open_profile("pbap", pbap_path_, status_.pbap_error, pbap_retried_);
            if (status_.map_open || status_.pbap_open)
                opened_once_ = true;
            next_attempt_ = now + (opened_once_ ? PROFILE_STEADY_POLL_SECONDS : PROFILE_INITIAL_POLL_SECONDS);
        }

        if (status_.map_held || status_.pbap_held) {
            const std::string names = status_.map_held && status_.pbap_held ? _("Messages and contacts")
                                      : status_.map_held                    ? _("Messages")
                                                                            : _("Contacts");
            // TRANSLATORS: {} is Messages, Contacts, or Messages and contacts.
            status_.reason = held_by_lock_ ? tr_format(_("{} released while this computer is locked."), names)
                                           : tr_format(_("{} released on this computer."), names);
        } else if (status_.map_open && status_.pbap_open) {
            status_.reason = _("Messages and contacts are connected.");
        } else if (!status_.map_open && status_.map_error != ObexError::None) {
            // MAP is the feature people notice, so its problem leads.
            status_.reason = obex_error_advice(status_.map_error, "map", l2cap_ertm_disabled());
        } else if (!status_.pbap_open && status_.pbap_error != ObexError::None) {
            status_.reason = obex_error_advice(status_.pbap_error, "pbap", l2cap_ertm_disabled());
        } else {
            status_.reason = _("Opening messages and contacts...");
        }

        return !(status_ == previous);
    }

} // namespace tether::bluetooth
