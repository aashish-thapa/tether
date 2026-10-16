#pragma once

#include <cstdint>
#include <string>

namespace tether::bluetooth {

    // Poll fast until the first session opens, then slowly.
    inline constexpr int PROFILE_INITIAL_POLL_SECONDS = 5;
    inline constexpr int PROFILE_STEADY_POLL_SECONDS = 15;

    // Why an OBEX session could not be opened. Forbidden and Busy look alike from a distance, and reporting one as the
    // other sends the user off re-pairing for nothing.
    enum class ObexError {
        None,
        // OBEX Forbidden / 0x43: the iPhone's permission toggle is off. Not a pairing failure
        Forbidden,
        // Transport-level "Connection refused (111)": another computer probably
        // owns the iPhone's single MAP session. Keep polling; do not re-pair.
        Busy,
        // obexd's live SDP query found no MAS/PSE record. NOT a permissions failure. Recovers on its own once the phone
        // serves them again.
        NoRecord,
        // The phone is not reachable right now.
        Unavailable,
        // org.bluez.obex is not on the session bus and cannot be activated,
        // meaning obexd is not installed. No phone-side setting affects this.
        NoDaemon,
        Other,
    };

    const char* to_string(ObexError error);

    // Maps an obexd/D-Bus error message onto the cases above. Pure so the
    // classification can be pinned down by tests against real message text.
    ObexError classify_obex_error(const std::string& message);

    // Advice for the user, phrased so it can be shown verbatim. `ertm_disabled`
    // names the local cause of a NoRecord that would otherwise blame the phone.
    std::string obex_error_advice(ObexError error, const std::string& profile, bool ertm_disabled = false);

    // Session operations on obexd.
    class ProfileOps {
    public:
        virtual ~ProfileOps() = default;

        // Returns the new session's object path, or an empty string with err set.
        virtual std::string create_session(const std::string& target, std::string& err) = 0;
        virtual void remove_session(const std::string& path) = 0;
    };

    struct ProfileStatus {
        bool map_open = false;
        bool pbap_open = false;
        ObexError map_error = ObexError::None;
        ObexError pbap_error = ObexError::None;
        std::string reason;

        bool operator==(const ProfileStatus&) const = default;
    };

    // Keeps exactly one MAP and one PBAP session alive for the daemon's lifetime.
    class ProfileSupervisor {
    public:
        explicit ProfileSupervisor(ProfileOps& ops);

        // Releases both obexd sessions. The iPhone serves one MAP session at a
        // time, so a session abandoned here blocks the next one.
        ~ProfileSupervisor();

        // Drive periodically with a monotonic clock. `device_ready` is a paired
        // device, not a link: obexd opens its own transport.
        bool tick(int64_t now, bool device_ready);

        // Drops both sessions after a disconnect so the next tick reopens them.
        void reset();

        // Drops only the MAP session, for a caller that has found it dead while
        // the link itself is still up. The next tick reopens it.
        void drop_map();

        const ProfileStatus& status() const { return status_; }
        const std::string& map_session() const { return map_path_; }
        const std::string& pbap_session() const { return pbap_path_; }

    private:
        bool open_profile(const std::string& target, std::string& path, ObexError& error, bool& retried);

        ProfileOps& ops_;
        ProfileStatus status_;
        std::string map_path_;
        std::string pbap_path_;
        bool map_retried_ = false;
        bool pbap_retried_ = false;
        int64_t next_attempt_ = 0;
        bool opened_once_ = false;
    };

} // namespace tether::bluetooth
