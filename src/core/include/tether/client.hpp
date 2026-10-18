#pragma once
#include <openssl/ssl.h>
#include <string>
#include <vector>

namespace tether {

    // Starts tetherd detached unless supervised (TETHER_NO_AUTOSTART=1 or enabled systemd unit).
    void spawn_daemon();

    class Client {
    public:
        Client();
        ~Client();

        // host="" uses the local UNIX socket, auto-launching if needed unless supervised.
        // host!="" opens a TCP+TLS connection to a remote daemon.
        bool connect(const std::string& host = "", int port = 5134);
        void disconnect();
        bool is_connected() const;

        // Base Level payload loop
        bool send(const std::string& payload);
        std::string send_and_wait(const std::string& payload);
        // target is a peer fingerprint; empty sends to every connected peer.
        bool send_file(const std::string& path, std::string& err_out, const std::string& target = "");

        std::string get_peer_fingerprint() const;
        ssize_t read(char* buf, size_t count);
        // Blocks up to timeout_ms for read() to return data without blocking; false means timed out.
        bool wait_readable(int timeout_ms) const;

        // High Level Features
        std::string get_clipboard(std::string& err_out);
        bool set_clipboard(const std::string& text, std::string& err_out);

        std::string list_devices();
        bool accept_device(const std::string& fingerprint, const std::string& name = "Paired Device");
        bool forget_device(const std::string& fingerprint);
        std::string pair(const std::string& device_name, std::string& err_out);

    private:
        int connect_unix(bool retry);

        int sock_ = -1;
        SSL* ssl_ = nullptr;
        std::vector<char> read_buf_;
    };

} // namespace tether
