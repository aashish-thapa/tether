#include "scoped_env.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <tether/base64.hpp>
#include <tether/crypto.hpp>

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using json = nlohmann::json;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

    // Real Unix/TLS framing: retain broadcasts that arrive in the same read as a reply.
    class Peer {
    public:
        Peer() = default;
        Peer(const Peer&) = delete;
        Peer& operator=(const Peer&) = delete;
        int fd = -1;
        SSL* ssl = nullptr;
        ~Peer() {
            if (ssl)
                SSL_free(ssl);
            if (fd >= 0)
                close(fd);
        }
        void send(const json& value) {
            const auto bytes = value.dump() + "\n";
            size_t at = 0;
            while (at < bytes.size()) {
                const int n = ssl ? SSL_write(ssl, bytes.data() + at, bytes.size() - at)
                                  : ::send(fd, bytes.data() + at, bytes.size() - at, MSG_NOSIGNAL);
                if (n <= 0)
                    throw std::runtime_error("peer write failed");
                at += n;
            }
        }
        json next(std::chrono::milliseconds timeout = 3s) {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            for (;;) {
                const auto newline = buffer.find('\n');
                if (newline != std::string::npos) {
                    auto line = buffer.substr(0, newline);
                    buffer.erase(0, newline + 1);
                    if (line == "OK" || line.empty())
                        continue;
                    return json::parse(line);
                }
                const auto left =
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                if (left <= 0ms)
                    return {};
                pollfd pfd{fd, POLLIN, 0};
                if (!(ssl && SSL_pending(ssl)) && poll(&pfd, 1, left.count()) <= 0)
                    return {};
                char bytes[8192];
                const int n = ssl ? SSL_read(ssl, bytes, sizeof(bytes)) : read(fd, bytes, sizeof(bytes));
                if (n <= 0)
                    return {};
                buffer.append(bytes, n);
            }
        }
        json until(const std::string& command) {
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            while (std::chrono::steady_clock::now() < deadline) {
                auto value = next(
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
                if (value.is_null() || value.value("command", "") == command)
                    return value;
            }
            return {};
        }

    private:
        std::string buffer;
    };

    std::string contents(const fs::path& path) {
        std::ifstream in(path);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    class HeadlessRuntime : public ::testing::Test {
    protected:
        fs::path root;
        std::map<std::string, std::string> env;
        std::vector<pid_t> children;
        uint16_t port = 0;

        void SetUp() override {
            char dir[] = "/tmp/tether-h-XXXXXX";
            ASSERT_NE(mkdtemp(dir), nullptr);
            root = dir;
            for (const auto* name : {"bin", "home", "runtime", "downloads"})
                fs::create_directory(root / name);
            fs::copy_file(TETHERD_TEST_PATH, root / "bin/tetherd");
            fs::copy_file(TETHER_CLI_TEST_PATH, root / "bin/tether");
            env = {{"HOME", (root / "home").string()},
                   {"XDG_CONFIG_HOME", (root / "home/.config").string()},
                   {"XDG_DATA_HOME", (root / "home/.local/share").string()},
                   {"XDG_STATE_HOME", (root / "home/.local/state").string()},
                   {"XDG_RUNTIME_DIR", (root / "runtime").string()},
                   {"XDG_DOWNLOAD_DIR", (root / "downloads").string()},
                   {"DBUS_SESSION_BUS_ADDRESS", "unix:path=" + (root / "bus").string()},
                   {"DBUS_SYSTEM_BUS_ADDRESS", "unix:path=" + (root / "no-system-bus").string()},
                   {"WAYLAND_DISPLAY", ""},
                   {"DISPLAY", ""},
                   {"PATH", "/usr/bin:/bin"},
                   {"TETHER_LOG_STDERR", "1"},
                   {"TETHER_NO_AUTOSTART", "1"}};
        }
        void TearDown() override {
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                // Each test child owns its process group, including any dialog helpers.
                kill(-*it, SIGKILL);
                int status;
                waitpid(*it, &status, 0);
            }
            fs::remove_all(root);
        }
        pid_t spawn(const std::vector<std::string>& args, const std::string& log = "daemon.log") {
            std::vector<char*> argv;
            for (const auto& arg : args)
                argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);
            const auto output = (root / log).string();
            const pid_t pid = fork();
            if (pid == 0) {
                if (setpgid(0, 0) != 0)
                    _exit(126);
                umask(0077);
                for (const auto& [key, value] : env) {
                    if (value.empty())
                        unsetenv(key.c_str());
                    else
                        setenv(key.c_str(), value.c_str(), 1);
                }
                const int fd = open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (fd < 0)
                    _exit(126);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
                execv(argv[0], argv.data());
                _exit(127);
            }
            if (pid < 0)
                throw std::runtime_error("fork failed");
            children.push_back(pid);
            return pid;
        }
        int finish(pid_t pid) {
            int status = 0;
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline) {
                if (waitpid(pid, &status, WNOHANG) == pid) {
                    children.erase(std::find(children.begin(), children.end(), pid));
                    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                }
                std::this_thread::sleep_for(10ms);
            }
            throw std::runtime_error("child timed out");
        }
        // A free loopback port, so the suite never collides with a running tetherd.
        static uint16_t free_port() {
            int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            socklen_t length = sizeof(address);
            const bool ok = fd >= 0 && bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                            getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0;
            if (fd >= 0)
                close(fd);
            if (!ok)
                throw std::runtime_error("no free port");
            return ntohs(address.sin_port);
        }

        void start(Peer& local) {
            port = free_port();
            env["TETHER_PORT"] = std::to_string(port);
            spawn({"/usr/bin/dbus-daemon",
                   "--session",
                   "--nofork",
                   "--nopidfile",
                   "--address=" + env["DBUS_SESSION_BUS_ADDRESS"]},
                  "bus.log");
            for (int i = 0; i < 100 && !fs::exists(root / "bus"); ++i)
                std::this_thread::sleep_for(10ms);
            spawn({(root / "bin/tetherd").string()});
            const auto path = (root / "runtime/tether/tetherd.sock").string();
            for (int i = 0; i < 300; ++i) {
                local.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
                sockaddr_un address{};
                address.sun_family = AF_UNIX;
                strcpy(address.sun_path, path.c_str());
                if (connect(local.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                    // Unix bind precedes TCP startup. Wait for our daemon's event
                    // loop so a port conflict cannot send a test to another daemon.
                    local.send({{"command", "state_snapshot"}});
                    if (local.until("state_snapshot").value("command", "") != "state_snapshot")
                        throw std::runtime_error("daemon did not become ready: " + contents(root / "daemon.log"));
                    return;
                }
                close(local.fd);
                local.fd = -1;
                std::this_thread::sleep_for(10ms);
            }
            throw std::runtime_error("daemon did not start: " + contents(root / "daemon.log"));
        }
        void connect_phone(Peer& phone) {
            // The peer has its own identity; never reuse the daemon's certificate.
            const auto peer_home = (root / "peer").string();
            tether::testing::ScopedEnv home("HOME", peer_home);
            tether::testing::ScopedEnv config("XDG_CONFIG_HOME", peer_home + "/.config");
            tether::Crypto::instance().reset_for_tests();
            ASSERT_TRUE(tether::Crypto::instance().init());
            phone.fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ASSERT_EQ(connect(phone.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
            timeval timeout{3, 0};
            setsockopt(phone.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(phone.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            phone.ssl = SSL_new(tether::Crypto::instance().get_client_context());
            SSL_set_fd(phone.ssl, phone.fd);
            ASSERT_EQ(SSL_connect(phone.ssl), 1);
        }
        void check_pending_pair() {
            Peer local, phone;
            start(local);
            connect_phone(phone);
            phone.send({{"command", "pair_request"}, {"device_name", "test phone"}});
            ASSERT_EQ(phone.until("pair_pending").value("command", ""), "pair_pending");
            EXPECT_TRUE(phone.next(400ms).is_null()) << "a missing GUI must not reject or accept the request";
            phone.send({{"command", "clipboard_set"}, {"content", "not authorized"}});
            EXPECT_EQ(phone.until("error").value("message", ""), "unauthorized");
            local.send({{"command", "state_snapshot"}});
            EXPECT_EQ(local.until("state_snapshot")["pending_pairs"].size(), 1u);
            const auto fp = tether::Crypto::instance().get_my_fingerprint();
            local.send({{"command", "accept_device"}, {"fingerprint", fp}});
            EXPECT_TRUE(local.until("accept_device_result").value("connected", false));
            EXPECT_EQ(phone.until("pair_accepted").value("command", ""), "pair_accepted");
            EXPECT_EQ(json::parse(contents(root / "home/.config/tether/known_hosts.json"))[fp], "test phone");
        }
    };

    TEST_F(HeadlessRuntime, NoWaylandLeavesPairingPendingForExplicitApproval) {
        std::ofstream(root / "bin/tether-dialog")
            << "#!/bin/sh\nprintf invoked > '" << (root / "dialog-invoked").string() << "'\nexit 0\n";
        fs::permissions(root / "bin/tether-dialog", fs::perms::owner_all);
        check_pending_pair();
        EXPECT_FALSE(fs::exists(root / "dialog-invoked"));
    }

    TEST_F(HeadlessRuntime, MissingDialogLeavesPairingPendingForExplicitApproval) {
        env["WAYLAND_DISPLAY"] = "missing-wayland";
        check_pending_pair();
    }

    class DialogRejection : public HeadlessRuntime, public ::testing::WithParamInterface<int> {};
    TEST_P(DialogRejection, ExplicitRejectionAndTimeoutAreNotTreatedAsMissingGui) {
        env["WAYLAND_DISPLAY"] = "test-display";
        std::ofstream(root / "bin/tether-dialog") << "#!/bin/sh\nexit " << GetParam() << "\n";
        fs::permissions(root / "bin/tether-dialog", fs::perms::owner_all);
        Peer local, phone;
        start(local);
        connect_phone(phone);
        phone.send({{"command", "pair_request"}, {"device_name", "test phone"}});
        EXPECT_EQ(phone.until("pair_rejected").value("command", ""), "pair_rejected");
        local.send({{"command", "state_snapshot"}});
        EXPECT_TRUE(local.until("state_snapshot")["paired_devices"].empty());
    }
    INSTANTIATE_TEST_SUITE_P(DialogExit, DialogRejection, ::testing::Values(1, 2));

    TEST_F(HeadlessRuntime, CliApprovalSupersedesAnOutstandingDialog) {
        env["WAYLAND_DISPLAY"] = "test-display";
        std::ofstream(root / "bin/tether-dialog") << "#!/bin/sh\nexec sleep 5\n";
        fs::permissions(root / "bin/tether-dialog", fs::perms::owner_all);
        Peer local, phone;
        start(local);
        connect_phone(phone);
        phone.send({{"command", "pair_request"}, {"device_name", "test phone"}});
        ASSERT_EQ(phone.until("pair_pending").value("command", ""), "pair_pending");
        local.send({{"command", "accept_device"}, {"fingerprint", tether::Crypto::instance().get_my_fingerprint()}});
        ASSERT_TRUE(local.until("accept_device_result").value("connected", false));
        EXPECT_EQ(phone.until("pair_accepted").value("command", ""), "pair_accepted");
        EXPECT_TRUE(phone.next(400ms).is_null()) << "the superseded dialog must not reject the accepted peer";
    }

    TEST_F(HeadlessRuntime, ExplicitStderrLoggingDoesNotCreateALogFile) {
        env["XDG_RUNTIME_DIR"] = "/dev/null"; // deterministic startup error, before networking
        EXPECT_NE(finish(spawn({(root / "bin/tetherd").string()})), 0);
        EXPECT_NE(contents(root / "daemon.log").find("Initialization error"), std::string::npos);
        EXPECT_FALSE(fs::exists(root / "home/.local/state/tether/tetherd.log"));
    }

    TEST_F(HeadlessRuntime, DefaultLoggingStillUsesTheStateDirectory) {
        env["XDG_RUNTIME_DIR"] = "/dev/null";
        for (const auto* value : {"", "0", "true"}) {
            env["TETHER_LOG_STDERR"] = value;
            EXPECT_NE(finish(spawn({(root / "bin/tetherd").string()})), 0);
            EXPECT_TRUE(contents(root / "daemon.log").empty());
            EXPECT_NE(contents(root / "home/.local/state/tether/tetherd.log").find("Initialization error"),
                      std::string::npos);
            fs::remove(root / "home/.local/state/tether/tetherd.log");
        }
    }

    TEST_F(HeadlessRuntime, CliAutostartCanBeDisabledWithoutChangingTheDefault) {
        fs::remove(root / "bin/tetherd");
        std::ofstream(root / "bin/tetherd") << "#!/bin/sh\nprintf invoked > '" << (root / "spawned").string() << "'\n";
        fs::permissions(root / "bin/tetherd", fs::perms::owner_all);
        for (const auto* value : {"1", "", "0", "true"}) {
            env["TETHER_NO_AUTOSTART"] = value;
            EXPECT_NE(finish(spawn({(root / "bin/tether").string(), "status"}, "cli.log")), 0);
            EXPECT_EQ(fs::exists(root / "spawned"), std::string(value) != "1") << value;
            fs::remove(root / "spawned");
        }
    }

    TEST_F(HeadlessRuntime, StatusRejectsWrongRepliesAndAcceptsFirstLineSnapshots) {
        fs::create_directory(root / "runtime/tether");
        const auto path = (root / "runtime/tether/tetherd.sock").string();
        for (const auto& [reply, expected] : std::vector<std::pair<std::string, int>>{
                 {"{\"command\":\"unrelated\"}\n", 1},
                 {"not json\n", 1},
                 {"{\"command\":\"state_snapshot\"}\n{\"command\":\"unrelated\"}\n", 0}}) {
            unlink(path.c_str());
            Peer listener;
            listener.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            strcpy(address.sun_path, path.c_str());
            ASSERT_EQ(bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
            ASSERT_EQ(listen(listener.fd, 1), 0);
            auto pid = spawn({(root / "bin/tether").string(), "status"}, "cli.log");
            pollfd ready{listener.fd, POLLIN, 0};
            ASSERT_GT(poll(&ready, 1, 3000), 0);
            Peer client;
            client.fd = accept4(listener.fd, nullptr, nullptr, SOCK_CLOEXEC);
            ASSERT_GE(client.fd, 0);
            EXPECT_EQ(client.next().value("command", ""), "state_snapshot");
            ASSERT_EQ(::send(client.fd, reply.data(), reply.size(), MSG_NOSIGNAL), static_cast<ssize_t>(reply.size()));
            EXPECT_EQ(finish(pid), expected) << reply;
        }
    }

    TEST_F(HeadlessRuntime, StatusReassemblesAFragmentedSnapshot) {
        fs::create_directory(root / "runtime/tether");
        Peer listener;
        listener.fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        strcpy(address.sun_path, (root / "runtime/tether/tetherd.sock").c_str());
        ASSERT_EQ(bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
        ASSERT_EQ(listen(listener.fd, 1), 0);
        const auto pid = spawn({(root / "bin/tether").string(), "status"}, "cli.log");
        pollfd ready{listener.fd, POLLIN, 0};
        ASSERT_GT(poll(&ready, 1, 3000), 0);
        Peer client;
        client.fd = accept4(listener.fd, nullptr, nullptr, SOCK_CLOEXEC);
        ASSERT_GE(client.fd, 0);
        EXPECT_EQ(client.next().value("command", ""), "state_snapshot");
        const std::string first = "{\"command\":\"state_";
        const std::string last = "snapshot\"}\n";
        ASSERT_EQ(::send(client.fd, first.data(), first.size(), MSG_NOSIGNAL), static_cast<ssize_t>(first.size()));
        std::this_thread::sleep_for(100ms);
        // A broken client may already have closed; finish(pid) is the behavior assertion.
        ::send(client.fd, last.data(), last.size(), MSG_NOSIGNAL);
        EXPECT_EQ(finish(pid), 0);
    }

    TEST_F(HeadlessRuntime, ReceiveEventsDoNotDependOnNotifierInitialization) {
        env["LD_PRELOAD"] = TETHER_NOTIFY_FIXTURE;
        Peer local, phone;
        start(local);
        connect_phone(phone);
        phone.send({{"command", "pair_request"}, {"device_name", "test phone"}});
        ASSERT_EQ(phone.until("pair_pending").value("command", ""), "pair_pending");
        local.send({{"command", "accept_device"}, {"fingerprint", tether::Crypto::instance().get_my_fingerprint()}});
        ASSERT_TRUE(local.until("accept_device_result").value("connected", false));
        ASSERT_EQ(phone.until("pair_accepted").value("command", ""), "pair_accepted");
        local.send({{"command", "subscribe"}});
        ASSERT_EQ(local.until("bt_connection_changed").value("command", ""), "bt_connection_changed");
        phone.send({{"command", "file_start"}, {"filename", "hello.txt"}, {"size", 5}, {"transfer_id", "complete"}});
        phone.send({{"command", "file_chunk"}, {"transfer_id", "complete"}, {"chunk_index", 0}, {"data", "aGVsbG8="}});
        phone.send({{"command", "file_end"}, {"transfer_id", "complete"}});
        const auto received = local.until("file_received");
        ASSERT_FALSE(received.is_null());
        EXPECT_EQ(received["filename"], "hello.txt");
        EXPECT_EQ(received["bytes_written"], 5);
        EXPECT_EQ(contents(root / "downloads/hello.txt"), "hello");
        local.send({{"command", "state_snapshot"}});
        EXPECT_EQ(local.until("state_snapshot")["recent_received_files"].size(), 1u);
        phone.send({{"command", "file_start"}, {"filename", "partial.txt"}, {"size", 5}, {"transfer_id", "partial"}});
        phone.send({{"command", "file_end"}, {"transfer_id", "partial"}});
        EXPECT_TRUE(local.next(200ms).is_null());
        EXPECT_FALSE(fs::exists(root / "downloads/partial.txt"));
    }

} // namespace
