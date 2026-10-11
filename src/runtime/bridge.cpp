// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "bridge.hpp"

#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace framerift {
namespace {

constexpr std::size_t packet_limit = 16 * 1024;
constexpr std::size_t event_limit = 512;
constexpr std::size_t command_limit = 128;
constexpr std::size_t pending_limit = 64;
constexpr auto peer_timeout = std::chrono::seconds(5);
constexpr auto retry_delay = std::chrono::milliseconds(500);
using Clock = std::chrono::steady_clock;

struct ObjectUnref {
    void operator()(gpointer object) const { if (object) g_object_unref(object); }
};
template<typename T> using ObjectPtr = std::unique_ptr<T, ObjectUnref>;
struct NodeUnref {
    void operator()(JsonNode* node) const { if (node) json_node_unref(node); }
};
using NodePtr = std::unique_ptr<JsonNode, NodeUnref>;

std::string take_error(GError* error, const char* fallback) {
    std::string message = error ? error->message : fallback;
    g_clear_error(&error);
    return message;
}

NodePtr new_object() {
    NodePtr node(json_node_new(JSON_NODE_OBJECT));
    json_node_take_object(node.get(), json_object_new());
    return node;
}

std::string serialize(JsonNode* node) {
    ObjectPtr<JsonGenerator> generator(json_generator_new());
    json_generator_set_root(generator.get(), node);
    gchar* encoded = json_generator_to_data(generator.get(), nullptr);
    std::string result(encoded);
    g_free(encoded);
    return result;
}

NodePtr parse_object(const std::string& text) {
    ObjectPtr<JsonParser> parser(json_parser_new());
    GError* error = nullptr;
    if (!json_parser_load_from_data(parser.get(), text.data(), text.size(), &error))
        throw std::runtime_error(take_error(error, "Invalid JSON"));
    auto* root = json_parser_get_root(parser.get());
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) throw std::runtime_error("Expected a JSON object");
    return NodePtr(json_node_copy(root));
}

std::string string_member(JsonObject* object, const char* name, std::size_t max_length,
    bool required = true) {
    auto* node = json_object_get_member(object, name);
    if (!node && !required) return {};
    if (!node || !JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING)
        throw std::runtime_error(std::string("Expected string: ") + name);
    const char* value = json_node_get_string(node);
    if (!value || std::char_traits<char>::length(value) > max_length || (required && !*value))
        throw std::runtime_error(std::string("Invalid string: ") + name);
    return value;
}

std::uint64_t unsigned_member(JsonObject* object, const char* name) {
    auto* node = json_object_get_member(object, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_INT64)
        throw std::runtime_error(std::string("Expected unsigned integer: ") + name);
    const auto value = json_node_get_int(node);
    if (value < 0) throw std::runtime_error(std::string("Expected unsigned integer: ") + name);
    return static_cast<std::uint64_t>(value);
}

bool pending(const std::string& state) { return state == "pending" || state == "received"; }

bool same_peer(GSocketAddress* a, GSocketAddress* b) {
    if (!a || !b || !G_IS_INET_SOCKET_ADDRESS(a) || !G_IS_INET_SOCKET_ADDRESS(b)) return false;
    auto* first = G_INET_SOCKET_ADDRESS(a);
    auto* second = G_INET_SOCKET_ADDRESS(b);
    return g_inet_socket_address_get_port(first) == g_inet_socket_address_get_port(second)
        && g_inet_address_equal(g_inet_socket_address_get_address(first),
            g_inet_socket_address_get_address(second));
}

bool loopback_peer(GSocketAddress* address) {
    return address && G_IS_INET_SOCKET_ADDRESS(address)
        && g_inet_address_get_is_loopback(g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(address)));
}

} // namespace

struct BridgeServer::Impl {
    struct Event { std::uint64_t id; std::string json; };
    struct Command {
        std::string id;
        std::string session_id;
        std::string name;
        std::string state = "pending";
        std::string packet;
        std::string result;
        std::string error;
        unsigned attempts = 0;
        Clock::time_point next_attempt;
    };

    ObjectPtr<GSocket> socket;
    ObjectPtr<GCancellable> cancel{g_cancellable_new()};
    ObjectPtr<GSocketAddress> peer;
    mutable std::mutex mutex;
    std::atomic<bool> stopping{false};
    std::thread worker;
    BridgeStatus state;
    std::string expected_session;
    std::string expected_game;
    std::string ended_session;
    std::set<std::string> capabilities;
    std::deque<Event> events;
    std::deque<Command> commands;
    std::uint64_t event_id = 0;
    std::uint64_t outgoing_sequence = 0;
    std::uint64_t incoming_sequence = 0;
    bool sequence_initialized = false;
    Clock::time_point last_packet;

    explicit Impl(std::uint16_t port) {
        GError* error = nullptr;
        socket.reset(g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
            G_SOCKET_PROTOCOL_UDP, &error));
        if (!socket) throw std::runtime_error(take_error(error, "Could not create the bridge socket"));
        g_socket_set_blocking(socket.get(), FALSE);
        ObjectPtr<GInetAddress> address(g_inet_address_new_from_string("127.0.0.1"));
        ObjectPtr<GSocketAddress> binding(g_inet_socket_address_new(address.get(), port));
        if (!g_socket_bind(socket.get(), binding.get(), FALSE, &error))
            throw std::runtime_error(take_error(error, "Could not bind the bridge socket"));
        ObjectPtr<GSocketAddress> local(g_socket_get_local_address(socket.get(), &error));
        if (!local) throw std::runtime_error(take_error(error, "Could not read the bridge port"));
        state.port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(local.get()));
        worker = std::thread([this] { run(); });
    }

    ~Impl() {
        stopping.store(true);
        g_cancellable_cancel(cancel.get());
        if (worker.joinable()) worker.join();
    }

    void detach(const std::string& reason) {
        state.connected = false;
        peer.reset();
        capabilities.clear();
        for (auto& command : commands) {
            if (pending(command.state)) {
                command.state = "disconnected";
                command.error = reason;
            }
        }
    }

    void reset_session(const std::string& session_id, const std::string& game_id) {
        const auto port = state.port;
        state = {};
        state.port = port;
        state.session_id = session_id;
        state.game_id = game_id;
        incoming_sequence = 0;
        sequence_initialized = false;
        events.clear();
    }

    void add_event(JsonNode* packet) {
        NodePtr copy(json_node_copy(packet));
        const auto id = ++event_id;
        json_object_set_int_member(json_node_get_object(copy.get()), "event_id", id);
        events.push_back({id, serialize(copy.get())});
        if (events.size() > event_limit) events.pop_front();
        ++state.received_events;
    }

    std::string command_json(const Command& command) const {
        auto node = new_object();
        auto* object = json_node_get_object(node.get());
        json_object_set_string_member(object, "command_id", command.id.c_str());
        json_object_set_string_member(object, "session_id", command.session_id.c_str());
        json_object_set_string_member(object, "name", command.name.c_str());
        json_object_set_string_member(object, "status", command.state.c_str());
        json_object_set_int_member(object, "attempts", command.attempts);
        if (!command.error.empty()) json_object_set_string_member(object, "error", command.error.c_str());
        if (!command.result.empty()) {
            auto result = parse_object(command.result);
            json_object_set_member(object, "result", result.release());
        }
        return serialize(node.get());
    }

    void receive(const std::string& text, GSocketAddress* source) {
        auto packet = parse_object(text);
        auto* object = json_node_get_object(packet.get());
        if (string_member(object, "protocol", 64) != "framerift.bridge"
            || unsigned_member(object, "version") != 1) throw std::runtime_error("Unsupported bridge protocol");
        const auto type = string_member(object, "type", 64);
        const auto session = string_member(object, "session_id", 128);
        const auto sequence = unsigned_member(object, "sequence");
        std::uint64_t frame = 0;
        const bool has_frame = json_object_has_member(object, "frame");
        if (has_frame) frame = unsigned_member(object, "frame");

        std::set<std::string> advertised;
        std::string game;
        std::string command_id;
        std::string result;
        std::string failure;
        if (type == "hello") {
            game = string_member(object, "game_id", 128);
            auto* list_node = json_object_get_member(object, "capabilities");
            if (!list_node || !JSON_NODE_HOLDS_ARRAY(list_node)) throw std::runtime_error("Expected capabilities array");
            auto* list = json_node_get_array(list_node);
            if (json_array_get_length(list) > 32) throw std::runtime_error("Too many capabilities");
            for (guint i = 0; i < json_array_get_length(list); ++i) {
                auto* value = json_array_get_element(list, i);
                if (!JSON_NODE_HOLDS_VALUE(value) || json_node_get_value_type(value) != G_TYPE_STRING)
                    throw std::runtime_error("Expected capability string");
                const char* name = json_node_get_string(value);
                if (!name || !*name || std::char_traits<char>::length(name) > 64)
                    throw std::runtime_error("Invalid capability name");
                advertised.emplace(name);
            }
        } else if (type == "event") {
            string_member(object, "name", 128);
            auto* data = json_object_get_member(object, "data");
            if (!data || !JSON_NODE_HOLDS_OBJECT(data)) throw std::runtime_error("Expected event data object");
        } else if (type == "command_received" || type == "command_completed" || type == "command_failed") {
            command_id = string_member(object, "command_id", 128);
            if (json_object_has_member(object, "result")) {
                auto* value = json_object_get_member(object, "result");
                if (!value || !JSON_NODE_HOLDS_OBJECT(value)) throw std::runtime_error("Expected command result object");
                result = serialize(value);
            }
            failure = string_member(object, "error", 1024, false);
        } else if (type != "heartbeat" && type != "goodbye") {
            throw std::runtime_error("Unknown bridge packet type");
        }

        std::lock_guard lock(mutex);
        if (!loopback_peer(source)) return;
        if (type == "hello") {
            if (session == ended_session
                || (!expected_session.empty() && (session != expected_session || game != expected_game))
                || (state.connected && (!same_peer(peer.get(), source) || session != state.session_id
                    || game != state.game_id))) return;
            const bool same_session = state.session_id == session && state.game_id == game;
            if (same_session && sequence_initialized && sequence <= incoming_sequence) return;
            if (!state.connected) {
                detach("The bridge session changed");
                if (!same_session) reset_session(session, game);
                peer.reset(G_SOCKET_ADDRESS(g_object_ref(source)));
                state.connected = true;
                state.error.clear();
            }
            capabilities = std::move(advertised);
        } else if (!state.connected || session != state.session_id || !same_peer(peer.get(), source)) {
            return;
        }
        if ((type == "heartbeat" || type == "goodbye") && sequence <= incoming_sequence) return;
        last_packet = Clock::now();
        const bool old_sequence = sequence_initialized && sequence <= incoming_sequence;
        if (!sequence_initialized) {
            incoming_sequence = sequence;
            sequence_initialized = true;
        } else if (sequence > incoming_sequence) {
            state.missing_events += sequence - incoming_sequence - 1;
            incoming_sequence = sequence;
        }
        if (has_frame && !(type == "event" && old_sequence))
            state.frame = std::max(state.frame, frame);
        if (type == "event") {
            // Late and duplicate telemetry may be discarded; commands remain idempotent.
            if (!old_sequence) add_event(packet.get());
        } else if (!command_id.empty()) {
            auto found = std::find_if(commands.begin(), commands.end(), [&](const Command& command) {
                return command.id == command_id && command.session_id == session;
            });
            if (found != commands.end() && pending(found->state)) {
                if (type == "command_received") {
                    if (found->state == "pending") {
                        found->state = "received";
                        add_event(packet.get());
                    }
                } else {
                    found->state = type == "command_completed" ? "completed" : "failed";
                    found->result = std::move(result);
                    found->error = std::move(failure);
                    add_event(packet.get());
                }
            }
        } else if (type == "goodbye") {
            detach("The runtime disconnected");
        }
    }

    void tick() {
        std::lock_guard lock(mutex);
        const auto now = Clock::now();
        if (state.connected && now - last_packet >= peer_timeout) {
            state.error = "The runtime heartbeat expired";
            detach(state.error);
        }
        if (!state.connected) return;
        for (auto& command : commands) {
            if (command.state != "pending" || now < command.next_attempt) continue;
            if (command.attempts >= 3) {
                command.state = "timed_out";
                command.error = "No command receipt after three attempts";
                continue;
            }
            ++command.attempts;
            command.next_attempt = now + retry_delay;
            GError* error = nullptr;
            const auto sent = g_socket_send_to(socket.get(), peer.get(), command.packet.data(),
                command.packet.size(), cancel.get(), &error);
            if (sent < 0 && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)
                && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                command.state = "failed";
                command.error = error ? error->message : "Could not send the command";
            }
            g_clear_error(&error);
        }
    }

    void run() noexcept {
        try {
            std::array<char, packet_limit + 1> buffer{};
            while (!stopping.load()) {
                // Bound each receive batch so continuous telemetry cannot starve commands.
                for (unsigned i = 0; i < 32 && !stopping.load(); ++i) {
                    GSocketAddress* address = nullptr;
                    GError* error = nullptr;
                    const auto count = g_socket_receive_from(socket.get(), &address, buffer.data(),
                        buffer.size(), cancel.get(), &error);
                    ObjectPtr<GSocketAddress> source(address);
                    if (count < 0) {
                        const bool expected = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)
                            || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
                        if (!expected) {
                            // Winsock can report an oversized UDP datagram as a receive error.
                            if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE)) {
                                std::lock_guard lock(mutex);
                                ++state.malformed_packets;
                                g_clear_error(&error);
                                continue;
                            }
                            throw std::runtime_error(take_error(error, "Could not receive bridge data"));
                        }
                        g_clear_error(&error);
                        break;
                    }
                    try {
                        if (count == 0 || static_cast<std::size_t>(count) > packet_limit)
                            throw std::runtime_error("Invalid bridge packet size");
                        receive(std::string(buffer.data(), static_cast<std::size_t>(count)), source.get());
                    } catch (const std::exception&) {
                        std::lock_guard lock(mutex);
                        ++state.malformed_packets;
                    }
                }
                tick();
                GError* error = nullptr;
                if (!g_socket_condition_timed_wait(socket.get(), G_IO_IN, 100000, cancel.get(), &error)
                    && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)
                    && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                    throw std::runtime_error(take_error(error, "Could not wait for bridge data"));
                }
                g_clear_error(&error);
            }
        } catch (const std::exception& failure) {
            std::lock_guard lock(mutex);
            state.error = failure.what();
            detach("The bridge worker stopped");
        } catch (...) {
            std::lock_guard lock(mutex);
            state.error = "The bridge worker stopped unexpectedly";
            detach(state.error);
        }
    }
};

BridgeServer::BridgeServer(std::uint16_t port) : impl_(std::make_unique<Impl>(port)) {}
BridgeServer::~BridgeServer() = default;

std::uint16_t BridgeServer::port() const { return status().port; }

void BridgeServer::expect_session(const std::string& session_id, const std::string& game_id) {
    if (session_id.empty() || game_id.empty() || session_id.size() > 128 || game_id.size() > 128)
        throw std::runtime_error("A bridge session requires valid session and game IDs");
    std::lock_guard lock(impl_->mutex);
    impl_->detach("A new game session was started");
    impl_->expected_session = session_id;
    impl_->expected_game = game_id;
    impl_->ended_session.clear();
    impl_->reset_session(session_id, game_id);
}

void BridgeServer::end_session(const std::string& session_id) {
    std::lock_guard lock(impl_->mutex);
    if (session_id != impl_->state.session_id) return;
    impl_->detach("The game session ended");
    impl_->ended_session = session_id;
    // Keep the expected identity and reject late hello packets after process exit.
}

BridgeStatus BridgeServer::status() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->state;
}

std::string BridgeServer::status_json() const {
    std::lock_guard lock(impl_->mutex);
    const auto& state = impl_->state;
    auto node = new_object();
    auto* object = json_node_get_object(node.get());
    json_object_set_boolean_member(object, "connected", state.connected);
    json_object_set_int_member(object, "port", state.port);
    json_object_set_string_member(object, "session_id", state.session_id.c_str());
    json_object_set_string_member(object, "game_id", state.game_id.c_str());
    json_object_set_string_member(object, "error", state.error.c_str());
    json_object_set_int_member(object, "frame", state.frame);
    json_object_set_int_member(object, "received_events", state.received_events);
    json_object_set_int_member(object, "missing_events", state.missing_events);
    json_object_set_int_member(object, "malformed_packets", state.malformed_packets);
    auto* list = json_array_new();
    for (const auto& name : impl_->capabilities) json_array_add_string_element(list, name.c_str());
    json_object_set_array_member(object, "capabilities", list);
    return serialize(node.get());
}

std::string BridgeServer::events_json(std::uint64_t after, std::size_t limit) const {
    if (!limit || limit > event_limit) throw std::runtime_error("Event limit must be between 1 and 512");
    if (after > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::runtime_error("Event cursor exceeds the JSON integer range");
    std::lock_guard lock(impl_->mutex);
    auto node = new_object();
    auto* object = json_node_get_object(node.get());
    auto* list = json_array_new();
    std::uint64_t next = after;
    std::size_t count = 0;
    for (const auto& event : impl_->events) {
        if (event.id <= after) continue;
        auto value = parse_object(event.json);
        json_array_add_element(list, value.release());
        next = event.id;
        if (++count == limit) break;
    }
    const auto oldest = impl_->events.empty() ? 0 : impl_->events.front().id;
    json_object_set_array_member(object, "events", list);
    json_object_set_int_member(object, "oldest_event_id", oldest);
    json_object_set_int_member(object, "next_event_id", next);
    json_object_set_boolean_member(object, "lost_history", oldest > 0 && after < oldest - 1);
    return serialize(node.get());
}

std::string BridgeServer::send_command(const std::string& name, const std::string& arguments_json) {
    static const std::set<std::string> names{
        "set_monitoring", "set_input", "schedule_inputs", "cancel_schedule", "capture_frame"};
    if (!names.contains(name)) throw std::runtime_error("Unknown bridge command: " + name);
    if (arguments_json.size() > packet_limit) throw std::runtime_error("Command arguments are too large");
    auto arguments = parse_object(arguments_json);
    std::lock_guard lock(impl_->mutex);
    if (!impl_->state.connected) throw std::runtime_error("No runtime is connected to the bridge");
    if (!impl_->capabilities.contains(name)) throw std::runtime_error("Runtime capability unavailable: " + name);
    if (static_cast<std::size_t>(std::count_if(impl_->commands.begin(), impl_->commands.end(),
            [](const auto& command) { return pending(command.state); })) >= pending_limit)
        throw std::runtime_error("Too many pending bridge commands");
    if (impl_->commands.size() >= command_limit) {
        const auto terminal = std::find_if(impl_->commands.begin(), impl_->commands.end(),
            [](const auto& command) { return !pending(command.state); });
        if (terminal == impl_->commands.end()) throw std::runtime_error("The bridge command history is full");
        impl_->commands.erase(terminal);
    }
    Impl::Command command;
    gchar* id = g_uuid_string_random();
    command.id = id;
    g_free(id);
    command.session_id = impl_->state.session_id;
    command.name = name;
    command.next_attempt = Clock::now();
    auto node = new_object();
    auto* object = json_node_get_object(node.get());
    json_object_set_string_member(object, "protocol", "framerift.bridge");
    json_object_set_int_member(object, "version", 1);
    json_object_set_string_member(object, "type", "command");
    json_object_set_string_member(object, "session_id", command.session_id.c_str());
    json_object_set_int_member(object, "sequence", ++impl_->outgoing_sequence);
    json_object_set_string_member(object, "command_id", command.id.c_str());
    json_object_set_string_member(object, "name", command.name.c_str());
    json_object_set_member(object, "args", arguments.release());
    command.packet = serialize(node.get());
    if (command.packet.size() > packet_limit) throw std::runtime_error("The command packet is too large");
    const auto response = impl_->command_json(command);
    impl_->commands.push_back(std::move(command));
    return response;
}

std::string BridgeServer::command_json(const std::string& command_id) const {
    std::lock_guard lock(impl_->mutex);
    const auto found = std::find_if(impl_->commands.begin(), impl_->commands.end(),
        [&](const auto& command) { return command.id == command_id; });
    if (found == impl_->commands.end()) throw std::runtime_error("Unknown bridge command ID");
    return impl_->command_json(*found);
}

} // namespace framerift
