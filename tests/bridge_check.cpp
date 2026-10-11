// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "runtime/bridge.hpp"
#include "runtime/mcp.hpp"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Unref {
    void operator()(gpointer object) const { if (object) g_object_unref(object); }
};

template <typename T> using Object = std::unique_ptr<T, Unref>;

class Json {
public:
    explicit Json(const std::string& text) : parser(json_parser_new()) {
        GError* error = nullptr;
        if (!json_parser_load_from_data(parser.get(), text.c_str(), static_cast<gssize>(text.size()), &error)) {
            const std::string message = error ? error->message : "Invalid JSON";
            g_clear_error(&error);
            throw std::runtime_error(message);
        }
        require(JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser.get())), "Expected a JSON object response");
    }

    JsonObject* object() const { return json_node_get_object(json_parser_get_root(parser.get())); }

private:
    Object<JsonParser> parser;
};

std::string member(JsonObject* object, const char* name) {
    require(json_object_has_member(object, name), "Expected JSON response member is missing");
    const auto* value = json_object_get_string_member(object, name);
    require(value != nullptr, "Expected a JSON string response member");
    return value;
}

template <typename Predicate>
void wait_until(Predicate ready, const char* failure, std::chrono::seconds timeout = std::chrono::seconds(4)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!ready()) {
        require(std::chrono::steady_clock::now() < deadline, failure);
        while (g_main_context_iteration(nullptr, false)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

template <typename Action>
void rejected(Action action, const char* failure) {
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(failure);
}

std::string packet(std::uint64_t sequence, const std::string& type, const std::string& fields = {},
    const std::string& session = "bridge-check-session") {
    return "{\"protocol\":\"framerift.bridge\",\"version\":1,\"type\":\"" + type +
        "\",\"session_id\":\"" + session + "\",\"sequence\":" + std::to_string(sequence) + fields + "}";
}

class Runtime {
public:
    explicit Runtime(std::uint16_t bridge_port) {
        GError* error = nullptr;
        socket.reset(g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &error));
        require(socket != nullptr && !error, "Could not create the mock runtime UDP socket");
        Object<GInetAddress> loopback(g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4));
        Object<GSocketAddress> local(g_inet_socket_address_new(loopback.get(), 0));
        require(g_socket_bind(socket.get(), local.get(), false, &error) && !error, "Could not bind the mock runtime UDP socket");
        remote.reset(g_inet_socket_address_new(loopback.get(), bridge_port));
        g_socket_set_blocking(socket.get(), false);
    }

    void send(const std::string& bytes) {
        GError* error = nullptr;
        const auto count = g_socket_send_to(socket.get(), remote.get(), bytes.data(), bytes.size(), nullptr, &error);
        require(!error && count == static_cast<gssize>(bytes.size()), "Could not send the mock runtime UDP packet");
    }

    std::string receive() {
        std::array<char, 65536> buffer{};
        std::string result;
        wait_until([&] {
            GError* error = nullptr;
            GSocketAddress* sender = nullptr;
            const auto count = g_socket_receive_from(socket.get(), &sender, buffer.data(), buffer.size(), nullptr, &error);
            Object<GSocketAddress> owned_sender(sender);
            if (count >= 0) {
                require(G_IS_INET_SOCKET_ADDRESS(sender) && g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(sender)) ==
                    g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(remote.get())), "Command arrived from the wrong UDP endpoint");
                result.assign(buffer.data(), static_cast<std::size_t>(count));
                return true;
            }
            const bool waiting = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);
            g_clear_error(&error);
            require(waiting, "Could not receive the mock runtime UDP command");
            return false;
        }, "The runtime did not receive a UDP command");
        return result;
    }

private:
    Object<GSocket> socket;
    Object<GSocketAddress> remote;
};

struct HttpResult {
    unsigned status = 0;
    std::string body;
    std::string allow;
};

HttpResult request(const std::string& endpoint, const std::string& body, const char* method = "POST", const char* origin = nullptr) {
    HttpResult result;
    std::atomic<bool> finished{false};
    std::exception_ptr failure;
    Object<GCancellable> cancellable(g_cancellable_new());
    std::thread client([&] {
        try {
            Object<SoupSession> session(soup_session_new());
            Object<SoupMessage> message(soup_message_new(method, endpoint.c_str()));
            require(message != nullptr, "Could not create an MCP HTTP request");
            auto* headers = soup_message_get_request_headers(message.get());
            soup_message_headers_replace(headers, "Accept", "application/json, text/event-stream");
            if (origin) soup_message_headers_replace(headers, "Origin", origin);
            if (!body.empty()) {
                auto* bytes = g_bytes_new(body.data(), body.size());
                soup_message_set_request_body_from_bytes(message.get(), "application/json", bytes);
                g_bytes_unref(bytes);
            }
            GError* error = nullptr;
            auto* bytes = soup_session_send_and_read(session.get(), message.get(), cancellable.get(), &error);
            if (!bytes) {
                const std::string description = error ? error->message : "MCP request failed";
                g_clear_error(&error);
                throw std::runtime_error(description);
            }
            gsize length = 0;
            const auto* data = static_cast<const char*>(g_bytes_get_data(bytes, &length));
            result.body.assign(data, length);
            g_bytes_unref(bytes);
            result.status = soup_message_get_status(message.get());
            const auto* allow = soup_message_headers_get_one(soup_message_get_response_headers(message.get()), "Allow");
            if (allow) result.allow = allow;
        } catch (...) { failure = std::current_exception(); }
        finished.store(true, std::memory_order_release);
    });
    try {
        wait_until([&] { return finished.load(std::memory_order_acquire); }, "The MCP HTTP request did not finish");
    } catch (...) {
        g_cancellable_cancel(cancellable.get());
        client.join();
        throw;
    }
    client.join();
    if (failure) std::rethrow_exception(failure);
    return result;
}

JsonObject* rpc_result(const Json& response) {
    require(!json_object_has_member(response.object(), "error"), "MCP returned an unexpected JSON-RPC error");
    auto* result = json_object_get_object_member(response.object(), "result");
    require(result != nullptr, "MCP returned no result object");
    return result;
}

JsonObject* tool_result(const Json& response) {
    auto* result = rpc_result(response);
    require(!json_object_get_boolean_member(result, "isError"), "MCP tool unexpectedly failed");
    auto* structured = json_object_get_object_member(result, "structuredContent");
    require(structured != nullptr, "MCP tool returned no structured result");
    return structured;
}

std::string timing(std::uint64_t sequence, std::uint64_t frame, std::uint64_t count,
    std::uint64_t timestamp, const std::string& interval, std::uint64_t epoch = 1,
    std::uint64_t drops = 0, std::uint64_t generation = 1) {
    return packet(sequence, "event", ",\"name\":\"frame_timing\",\"frame\":" + std::to_string(frame) +
        ",\"data\":{\"output_handle\":7,\"output_generation\":" + std::to_string(generation) +
        ",\"completed_flip_count\":" + std::to_string(count) + ",\"timestamp_us\":" + std::to_string(timestamp) +
        ",\"flip_interval_us\":" + interval + ",\"monitoring_epoch\":" + std::to_string(epoch) +
        ",\"dropped_samples\":" + std::to_string(drops) + "}");
}

std::string resources(std::uint64_t sequence, std::uint64_t timestamp, std::uint64_t epoch = 1,
    std::uint64_t active = 2, const std::string& memory = R"({"process_rss_bytes":1024,"process_rss_timestamp_us":9000000,"sampled_cache_bytes":100,"sampled_cache_budget_bytes":null,"storage_cache_bytes":0,"storage_cache_budget_bytes":1024,"gpu_heap_usage_bytes":500,"gpu_heap_budget_bytes":1000,"gpu_heap_timestamp_us":8000000})") {
    return packet(sequence, "event", ",\"name\":\"resource_status\",\"data\":{\"timestamp_us\":" +
        std::to_string(timestamp) + ",\"monitoring_epoch\":" + std::to_string(epoch) +
        ",\"shaders\":{\"translations_active\":" + std::to_string(active) +
        R"(,"translations_completed":9,"cached_variants_count":4,"source_code_bytes":4096,"variant_spirv_bytes":8192,"disk_cache_hits":5,"disk_cache_misses":2,"disk_cache_writes":1,"disk_cache_load_failures":0,"disk_cache_write_failures":0,"preparation_queue_pending":3,"preparation_active":1,"pipeline_builds_active":0,"pipeline_builds_completed":7,"pipeline_optimization_queue_pending":2,"pipeline_optimizations_active":1},"memory":)" + memory + "}");
}

template <typename Change>
std::string changed(const std::string& text, Change change) {
    const Json json(text);
    change(json.object());
    Object<JsonGenerator> generator(json_generator_new());
    auto* root = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(root, json.object());
    json_generator_set_root(generator.get(), root);
    auto* encoded = json_generator_to_data(generator.get(), nullptr);
    std::string result(encoded);
    g_free(encoded);
    json_node_unref(root);
    return result;
}

void near(const std::optional<double>& value, double expected, const char* failure) {
    require(value && std::abs(*value - expected) < 0.000001, failure);
}

void check_automatic_monitoring() {
    framerift::BridgeServer bridge(0);
    Runtime runtime(bridge.port());
    bridge.expect_session("bridge-check-session", "PPSA01234", true);
    runtime.send(packet(1, "hello", R"(,"game_id":"PPSA01234","capabilities":["set_monitoring"],"monitoring":{"enabled":false,"epoch":0})"));
    const Json command(runtime.receive());
    require(member(command.object(), "name") == "set_monitoring" &&
        json_object_get_boolean_member(json_object_get_object_member(command.object(), "args"), "enabled"),
        "Game startup did not automatically request monitoring");
    const auto id = member(command.object(), "command_id");
    require(bridge.monitoring_snapshot().pending && !bridge.monitoring_snapshot().enabled,
        "Automatic monitoring was treated as confirmed before runtime receipt");
    runtime.send(packet(2, "command_completed", ",\"command_id\":\"" + id + R"(","result":{"enabled":true,"epoch":1})"));
    wait_until([&] { return bridge.monitoring_snapshot().enabled; }, "Automatic monitoring was not confirmed");
    const Json disabled(bridge.send_command("set_monitoring", "{\"enabled\":false}"));
    const auto disabled_id = member(disabled.object(), "command_id");
    runtime.receive();
    runtime.send(packet(3, "command_completed", ",\"command_id\":\"" + disabled_id + R"(","result":{"enabled":false,"epoch":2})"));
    runtime.send(packet(4, "hello", R"(,"game_id":"PPSA01234","capabilities":["set_monitoring"],"monitoring":{"enabled":false,"epoch":2})"));
    wait_until([&] { return !bridge.monitoring_snapshot().pending && !bridge.monitoring_snapshot().enabled; },
        "Manual monitoring disable was overridden by automatic startup");
    require(bridge.monitoring_snapshot().command_id == disabled_id, "Repeated hello re-enabled monitoring");
}

void check_monitoring() {
    framerift::BridgeServer bridge(0);
    Runtime runtime(bridge.port());
    Runtime stranger(bridge.port());
    framerift::McpServer mcp(bridge, 0);
    bridge.expect_session("bridge-check-session", "PPSA01234");
    const Json initial_status(bridge.status_json());
    require(!bridge.status().has_frame && JSON_NODE_HOLDS_NULL(json_object_get_member(initial_status.object(), "frame")),
        "A bridge with no measurements fabricated frame zero");
    std::uint64_t sequence = 1;
    runtime.send(packet(sequence, "hello", ",\"game_id\":\"PPSA01234\",\"capabilities\":[\"set_monitoring\"],\"monitoring\":{\"enabled\":false,\"epoch\":0}"));
    wait_until([&] { return bridge.status().connected; }, "The monitoring runtime did not connect");
    auto snapshot = bridge.monitoring_snapshot();
    require(snapshot.supported && !snapshot.enabled && !snapshot.has_frames && snapshot.state == "disabled",
        "Monitoring was not initially confirmed disabled without data");
    const auto dispatched = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"send_command","arguments":{"name":"set_monitoring","args":{"enabled":true}}}})");
    const Json dispatched_json(dispatched.body);
    const auto command_id = member(tool_result(dispatched_json), "command_id");
    const Json command(runtime.receive());
    require(member(command.object(), "name") == "set_monitoring" &&
        json_object_get_boolean_member(json_object_get_object_member(command.object(), "args"), "enabled"),
        "MCP did not send the real monitoring enable command");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.pending && !snapshot.enabled && snapshot.requested_enabled == true && snapshot.command_id == command_id,
        "A requested monitoring switch was incorrectly treated as confirmed");
    sequence = 3;
    runtime.send(packet(sequence, "command_completed", ",\"command_id\":\"" + command_id + "\",\"result\":{\"enabled\":true,\"epoch\":1}"));
    runtime.send(packet(2, "command_received", ",\"command_id\":\"" + command_id + "\""));
    wait_until([&] { return bridge.monitoring_snapshot().enabled; }, "Reordered monitoring completion was not confirmed");
    snapshot = bridge.monitoring_snapshot();
    require(!snapshot.pending && snapshot.command_status == "completed" && snapshot.state == "waiting", "Enabled monitoring without frames was not waiting");
    runtime.send(timing(++sequence, 500, 100, 10000000, "null"));
    wait_until([&] { return bridge.monitoring_snapshot().samples.size() == 1; }, "First monitoring completion was not normalized");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.has_frames && !snapshot.stats.estimated_fps && !snapshot.samples.front().flip_interval_us &&
        snapshot.state == "waiting_for_intervals", "First null interval fabricated performance values");
    std::uint64_t timestamp = 10000000;
    for (std::uint64_t index = 1; index <= 4; ++index) {
        timestamp += index * 10000;
        runtime.send(timing(++sequence, 500 + index, 100 + index, timestamp, std::to_string(index * 10000)));
    }
    wait_until([&] { return bridge.monitoring_snapshot().samples.size() == 5; }, "Monitoring intervals were not normalized");
    snapshot = bridge.monitoring_snapshot();
    near(snapshot.stats.estimated_fps, 40.0, "Estimated FPS used receiver arrival time instead of source intervals");
    near(snapshot.stats.mean_interval_us, 25000.0, "Monitoring mean is wrong");
    near(snapshot.stats.p95_interval_us, 40000.0, "Monitoring P95 is not nearest rank");
    near(snapshot.stats.worst_interval_us, 40000.0, "Monitoring worst interval is wrong");
    near(snapshot.stats.coverage, 1.0, "Complete observed input samples did not report full coverage");
    require(snapshot.stats.sample_count == 4 && !snapshot.stats.complete, "A short source window was presented as a full second");
    timestamp += 1000000;
    runtime.send(timing(++sequence, 505, 105, timestamp, "1000000"));
    wait_until([&] { return bridge.monitoring_snapshot().samples.size() == 6; }, "Source window sample was not received");
    snapshot = bridge.monitoring_snapshot();
    near(snapshot.stats.mean_interval_us, 1000000.0, "The source-time last-second window included an expired boundary sample");
    require(snapshot.stats.complete && snapshot.stats.sample_count == 1, "A full source-time window was not identified");
    ++sequence;
    timestamp += 40000;
    const auto gap_packet = timing(++sequence, 508, 108, timestamp, "20000", 1, 1);
    runtime.send(gap_packet);
    wait_until([&] { return bridge.monitoring_snapshot().samples.size() == 7; }, "Monitoring sample gap was not accepted");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.stats.missing_samples == 2 && snapshot.stats.sender_dropped_samples == 1 && snapshot.stats.packet_gaps == 2,
        "Source queue losses, sample gaps and packet gaps were conflated");
    near(snapshot.stats.coverage, 0.5, "Monitoring coverage ignored missing completed flips");
    require(!snapshot.stats.complete, "A sample gap was presented as complete coverage");
    runtime.send(gap_packet);
    stranger.send(timing(++sequence, 999, 109, timestamp + 20000, "20000", 1, 1));
    runtime.send(packet(sequence, "heartbeat", ",\"monitoring\":{\"enabled\":true,\"epoch\":1}"));
    const auto malformed = bridge.status().malformed_packets;
    runtime.send(timing(++sequence, 509, 109, timestamp + 20000, "null", 1, 1));
    runtime.send(timing(++sequence, 509, 109, timestamp - 1, "20000", 1, 1));
    runtime.send(timing(++sequence, 509, 109, timestamp + 20000, "20000", 1, 0));
    runtime.send(timing(++sequence, 509, 109, timestamp + 20000, "0", 1, 1));
    runtime.send(timing(++sequence, 509, 0, timestamp + 20000, "20000", 1, 1));
    wait_until([&] { return bridge.status().malformed_packets >= malformed + 5; }, "Invalid null, timestamp, zero or cumulative-drop state was accepted");
    require(bridge.monitoring_snapshot().samples.size() == 7 && bridge.status().frame == 508,
        "Duplicate, wrong-peer or invalid samples changed normalized monitoring");
    const auto compact = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":23,"method":"tools/call","params":{"name":"get_monitoring_snapshot","arguments":{}}})");
    const Json compact_json(compact.body);
    auto* summary = tool_result(compact_json);
    require(!json_object_has_member(summary, "frames") && !json_object_has_member(summary, "chart_samples") &&
        json_object_get_int_member(json_object_get_object_member(summary, "latest_frame"), "frame") == 508 &&
        json_object_get_int_member(json_object_get_object_member(summary, "worst_frame"), "frame") == 505 &&
        member(json_object_get_object_member(json_object_get_object_member(summary, "stats"), "loss_scope"), "sender_dropped_samples") == "monitoring_epoch",
        "Default monitoring MCP response omitted selected frames/loss scope or streamed unrequested history");
    const auto queried = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":21,"method":"tools/call","params":{"name":"get_monitoring_snapshot","arguments":{"include_history":true}}})");
    const Json queried_json(queried.body);
    auto* result = tool_result(queried_json);
    require(queried.status == 200 && json_object_get_boolean_member(result, "enabled") &&
        json_array_get_length(json_object_get_array_member(result, "frames")) == 7 &&
        member(json_object_get_object_member(result, "units"), "mean_interval_us") == "microseconds",
        "Monitoring MCP snapshot lost typed values, bounded frames or units");
    const auto invalid_args = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":22,"method":"tools/call","params":{"name":"get_monitoring_snapshot","arguments":{"limit":1}}})");
    require(json_object_get_int_member(json_object_get_object_member(Json(invalid_args.body).object(), "error"), "code") == -32602,
        "Monitoring MCP accepted undocumented arguments");
    wait_until([&] { return bridge.monitoring_snapshot().stale; }, "One second without new samples did not mark monitoring stale");
    require(bridge.status().connected && bridge.monitoring_snapshot().samples.size() == 7,
        "Sample staleness disconnected the bridge or discarded historical data");
    const Json disabled(bridge.send_command("set_monitoring", "{\"enabled\":false}"));
    const auto disabled_id = member(disabled.object(), "command_id");
    runtime.receive();
    runtime.send(packet(++sequence, "heartbeat", ",\"monitoring\":{\"enabled\":false,\"epoch\":2}"));
    wait_until([&] { return !bridge.monitoring_snapshot().enabled; }, "Heartbeat did not recover a lost monitoring completion");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.state == "disabled" && !snapshot.pending && snapshot.stale && snapshot.samples.size() == 7 &&
        snapshot.monitoring_epoch == 2 && snapshot.samples.back().monitoring_epoch == 1,
        "Disabling monitoring discarded the previous epoch or left its switch pending");
    runtime.send(packet(++sequence, "command_completed", ",\"command_id\":\"" + disabled_id + "\",\"result\":{\"enabled\":false,\"epoch\":2}"));
    const Json enabled(bridge.send_command("set_monitoring", "{\"enabled\":true}"));
    const auto enabled_id = member(enabled.object(), "command_id");
    runtime.receive();
    runtime.send(packet(++sequence, "command_completed", ",\"command_id\":\"" + enabled_id + "\",\"result\":{\"enabled\":true,\"epoch\":3}"));
    wait_until([&] { return bridge.monitoring_snapshot().monitoring_epoch == 3; }, "A new monitoring epoch was not accepted");
    require(bridge.monitoring_snapshot().samples.empty(), "New monitoring activation retained old current-window data");
    ++sequence;
    runtime.send(timing(++sequence, 700, 500, 20000000, "20000", 3));
    wait_until([&] { return bridge.monitoring_snapshot().samples.size() == 1; }, "Lost first-null sample blocked all later measurements");
    snapshot = bridge.monitoring_snapshot();
    require(!snapshot.stats.coverage && !snapshot.stats.complete && snapshot.stats.estimated_fps,
        "A lost first-null baseline was incorrectly presented as complete coverage");
    runtime.send(timing(++sequence, 701, 1, 20020000, "null", 3, 0, 2));
    wait_until([&] { return bridge.monitoring_snapshot().output_generation == 2; }, "Output generation reset was not applied");
    require(bridge.monitoring_snapshot().samples.size() == 1 && !bridge.monitoring_snapshot().stats.estimated_fps,
        "A new output stream mixed intervals from the previous stream");
    const Json noop(bridge.send_command("set_monitoring", "{\"enabled\":true}"));
    const auto noop_id = member(noop.object(), "command_id");
    runtime.receive();
    require(bridge.monitoring_snapshot().pending, "A new no-op monitoring request was not initially pending");
    runtime.send(packet(++sequence, "heartbeat", ",\"monitoring\":{\"enabled\":true,\"epoch\":3}"));
    wait_until([&] { return !bridge.monitoring_snapshot().pending; }, "Same-epoch heartbeat did not resolve an idempotent monitoring request");
    require(bridge.monitoring_snapshot().enabled && bridge.monitoring_snapshot().monitoring_epoch == 3,
        "Idempotent monitoring confirmation reset its source epoch");
    runtime.send(packet(++sequence, "command_completed", ",\"command_id\":\"" + noop_id + "\",\"result\":{\"enabled\":true,\"epoch\":3}"));
    timestamp = 20020000;
    std::uint64_t count = 1;
    for (unsigned batch = 0; batch < 1030; ++batch) {
        for (unsigned index = 0; index < 8; ++index) {
            ++count;
            timestamp += 1000;
            runtime.send(timing(++sequence, 700 + count, count, timestamp, "1000", 3, 0, 2));
        }
        wait_until([&] { return bridge.status().frame == 700 + count; }, "Bounded monitoring batch was not received");
    }
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.samples.size() == 8192 && snapshot.history_truncated, "High-rate monitoring history was not bounded by sample count");
    const Json bounded(bridge.monitoring_snapshot_json(true));
    require(json_array_get_length(json_object_get_array_member(bounded.object(), "chart_samples")) == 600 &&
        json_array_get_length(json_object_get_array_member(bounded.object(), "frames")) == 20 &&
        json_object_get_boolean_member(bounded.object(), "chart_downsampled"), "Monitoring MCP payload did not bound its charts and selected frames");
    timestamp += 61000000;
    ++count;
    runtime.send(timing(++sequence, 700 + count, count, timestamp, "61000000", 3, 0, 2));
    wait_until([&] { return bridge.monitoring_snapshot().samples.size() == 1; }, "Source-time history retained samples older than sixty seconds");
    runtime.send(packet(++sequence, "goodbye"));
    wait_until([&] { return !bridge.status().connected; }, "Monitoring runtime did not disconnect");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.stale && snapshot.samples.size() == 1 && snapshot.stats.estimated_fps,
        "Disconnect discarded the last source-time performance snapshot");
    bridge.end_session("bridge-check-session");
    require(bridge.monitoring_snapshot().samples.size() == 1, "Managed process exit discarded the last monitoring history");
    bridge.expect_session("monitoring-next-session", "PPSA01234");
    require(bridge.monitoring_snapshot().samples.empty() && !bridge.monitoring_snapshot().has_frames && !bridge.status().has_frame,
        "A new managed session inherited old monitoring history or frame identity");
}

void check_work_activity() {
    framerift::BridgeServer bridge(0);
    Runtime runtime(bridge.port());
    bridge.expect_session("bridge-check-session", "PPSA01234");
    std::uint64_t sequence = 1;
    runtime.send(packet(sequence, "hello", R"(,"game_id":"PPSA01234","capabilities":["set_monitoring","resource_status","work_activity"],"monitoring":{"enabled":true,"epoch":1})"));
    wait_until([&] { return bridge.status().connected; }, "Work runtime did not connect");
    const auto sample = [&](std::uint64_t timestamp, std::uint64_t count, std::uint64_t epoch = 1) {
        return changed(resources(++sequence, timestamp, epoch), [&](JsonObject* object) {
            auto* activity = json_object_new();
            for (const auto* name : framerift::resource_work_names) {
                auto* work = json_object_new();
                json_object_set_int_member(work, "active", 0);
                json_object_set_int_member(work, "started", count);
                json_object_set_int_member(work, "completed", count);
                json_object_set_int_member(work, "failed", 0);
                json_object_set_int_member(work, "total_us", count * 1000);
                json_object_set_int_member(work, "max_us", 1000);
                json_object_set_object_member(activity, name, work);
            }
            json_object_set_object_member(json_object_get_object_member(object, "data"), "activity", activity);
        });
    };
    runtime.send(sample(10000000, 100));
    wait_until([&] { return bridge.monitoring_snapshot().resources.has_sample; }, "Work baseline was not accepted");
    require(!bridge.monitoring_snapshot().resources.work_delta, "First sample fabricated a work interval");
    for (unsigned i = 1; i <= 5; ++i) {
        runtime.send(sample(10000000 + i * 250000, 100 + i * 2));
        wait_until([&] { return bridge.monitoring_snapshot().resources.sample->timestamp_us == 10000000 + i * 250000; }, "Work sample was not accepted");
    }
    auto snapshot = bridge.monitoring_snapshot();
    require(snapshot.resources.activity_supported && snapshot.resources.work_delta && snapshot.resources.work_window_us == 1000000
        && (*snapshot.resources.work_delta)[3].active == 0 && (*snapshot.resources.work_delta)[3].completed == 8
        && (*snapshot.resources.work_delta)[3].total_us == 8000 && (*snapshot.resources.work_delta)[3].max_us == 1000,
        "Short completed work vanished or the observed interval included older operations");
    const Json encoded(bridge.monitoring_snapshot_json(false));
    auto* resource = json_object_get_object_member(encoded.object(), "resources");
    auto* window = json_object_get_object_member(resource, "activity_window");
    require(window && json_object_get_int_member(window, "duration_us") == 1000000
        && json_object_get_int_member(json_object_get_object_member(json_object_get_object_member(window, "delta"), "pipeline_build"), "completed") == 8,
        "MCP and the typed UI work window disagree");
    const auto malformed = bridge.status().malformed_packets;
    runtime.send(changed(sample(11500000, 112), [](JsonObject* object) {
        auto* activity = json_object_get_object_member(json_object_get_object_member(object, "data"), "activity");
        json_object_set_int_member(json_object_get_object_member(activity, "gpu_present_wait"), "failed", -1);
    }));
    runtime.send(resources(++sequence, 11500000));
    wait_until([&] { return bridge.status().malformed_packets >= malformed + 2; }, "Invalid or missing advertised work activity was accepted");
    require(bridge.monitoring_snapshot().resources.sample->timestamp_us == 11250000, "Malformed work replaced valid resources");
    runtime.send(sample(11500000, 1));
    wait_until([&] { return bridge.monitoring_snapshot().resources.sample->timestamp_us == 11500000; }, "Reset sample was not accepted");
    require(!bridge.monitoring_snapshot().resources.work_delta, "Decreasing counters underflowed the work delta");
    runtime.send(packet(++sequence, "heartbeat", R"(,"monitoring":{"enabled":false,"epoch":2})"));
    wait_until([&] { return !bridge.monitoring_snapshot().enabled; }, "Work monitoring did not stop");
    require(bridge.monitoring_snapshot().resources.sample->activity.has_value(), "OFF erased retained work data");
    runtime.send(sample(20000000, 200, 3));
    wait_until([&] { return bridge.monitoring_snapshot().monitoring_epoch == 3; }, "Work reactivation did not advance epoch");
    require(!bridge.monitoring_snapshot().resources.work_delta, "New ON epoch inherited a work baseline");
    runtime.send(timing(++sequence, 500, 1, 20000000, "null", 3));
    runtime.send(timing(++sequence, 501, 2, 20010000, "10000", 3));
    wait_until([&] { return bridge.monitoring_snapshot().has_frames; }, "Work test frames were not accepted");
    wait_until([&] { return bridge.monitoring_snapshot().stale; }, "Work test frame age did not expire");
    runtime.send(changed(sample(20250000, 200, 3), [](JsonObject* object) {
        auto* activity = json_object_get_object_member(json_object_get_object_member(object, "data"), "activity");
        auto* work = json_object_get_object_member(activity, "gpu_present_wait");
        json_object_set_int_member(work, "active", 1);
        json_object_set_int_member(work, "started", 201);
    }));
    wait_until([&] { return bridge.monitoring_snapshot().resources.sample->timestamp_us == 20250000; }, "Active wait sample was not accepted");
    require(bridge.monitoring_snapshot().stall_evidence == "instrumented_work_active", "A stalled frame with an active GPU wait was labeled idle");
    runtime.send(sample(20500000, 201, 3));
    wait_until([&] { return bridge.monitoring_snapshot().resources.sample->timestamp_us == 20500000; }, "Inactive wait sample was not accepted");
    require(bridge.monitoring_snapshot().stall_evidence == "unknown", "An unexplained stalled frame was labeled idle");
}

void check_resources() {
    framerift::BridgeServer bridge(0);
    Runtime runtime(bridge.port());
    Runtime stranger(bridge.port());
    framerift::McpServer mcp(bridge, 0);
    bridge.expect_session("bridge-check-session", "PPSA01234");
    std::uint64_t sequence = 1;
    const std::string hello = ",\"game_id\":\"PPSA01234\",\"capabilities\":[\"set_monitoring\",\"resource_status\"]";
    runtime.send(packet(sequence, "hello", ",\"game_id\":\"PPSA01234\",\"capabilities\":[\"set_monitoring\"],\"monitoring\":{\"enabled\":true,\"epoch\":1}"));
    wait_until([&] { return bridge.status().connected; }, "The resource runtime did not connect");
    auto snapshot = bridge.monitoring_snapshot();
    require(!snapshot.resources.supported && !snapshot.resources.has_sample && snapshot.resources.state == "unsupported",
        "An older frame-only runtime advertised nonexistent resource support");
    auto malformed = bridge.status().malformed_packets;
    runtime.send(resources(++sequence, 10000000));
    wait_until([&] { return bridge.status().malformed_packets > malformed; }, "An unadvertised resource stream was accepted");
    runtime.send(packet(++sequence, "hello", hello + ",\"monitoring\":{\"enabled\":false,\"epoch\":2}"));
    wait_until([&] { return bridge.monitoring_snapshot().resources.supported; }, "Resource capability was not recorded");
    runtime.send(resources(++sequence, 10000000, 2));
    runtime.send(packet(++sequence, "heartbeat", ",\"monitoring\":{\"enabled\":true,\"epoch\":3}"));
    wait_until([&] { return bridge.monitoring_snapshot().monitoring_epoch == 3; }, "Resource activation was not confirmed");
    require(!bridge.monitoring_snapshot().resources.has_sample && bridge.monitoring_snapshot().resources.state == "waiting",
        "Disabled resource traffic became a measurement");
    runtime.send(resources(++sequence, 10000000, 3));
    wait_until([&] { return bridge.monitoring_snapshot().resources.has_sample; }, "A valid resource snapshot was not normalized");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.resources.state == "active" && !snapshot.resources.stale && !snapshot.has_frames && !bridge.status().has_frame &&
        snapshot.resources.sample->shaders.translations_completed == 9 && snapshot.resources.sample->shaders.source_code_bytes == 4096 &&
        snapshot.resources.sample->memory.process_rss_bytes == 1024 && !snapshot.resources.sample->memory.sampled_cache_budget_bytes,
        "Resource data required a frame, fabricated frame zero or lost typed values");
    near(snapshot.resources.process_rss_age_ms, *snapshot.resources.age_ms + 1000.0, "RSS freshness ignored its own source timestamp");
    near(snapshot.resources.gpu_heap_age_ms, *snapshot.resources.age_ms + 2000.0, "GPU heap freshness ignored its own source timestamp");
    const Json queried(request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":40,"method":"tools/call","params":{"name":"get_monitoring_snapshot","arguments":{}}})").body);
    auto* result = tool_result(queried);
    auto* resource = json_object_get_object_member(result, "resources");
    require(resource && json_object_get_boolean_member(resource, "has_sample") && member(resource, "state") == "active" &&
        json_object_get_int_member(json_object_get_object_member(resource, "shaders"), "variant_spirv_bytes") == 8192 &&
        JSON_NODE_HOLDS_NULL(json_object_get_member(json_object_get_object_member(resource, "memory"), "sampled_cache_budget_bytes")) &&
        JSON_NODE_HOLDS_NULL(json_object_get_member(result, "latest_frame")) && !json_object_has_member(result, "chart_samples"),
        "Compact MCP omitted resources, converted unknown memory to zero or fabricated a frame");
    runtime.send(timing(++sequence, 500, 1, 10000000, "null", 3));
    runtime.send(timing(++sequence, 501, 2, 10010000, "10000", 3));
    wait_until([&] { return bridge.monitoring_snapshot().samples.size() == 2; }, "Frames alongside resources were not accepted");
    wait_until([&] { return bridge.monitoring_snapshot().stale; }, "Resource test frame freshness did not expire");
    runtime.send(resources(++sequence, 12000000, 3));
    wait_until([&] { return bridge.monitoring_snapshot().resources.sample->timestamp_us == 12000000; }, "Periodic resource refresh was not accepted");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.stale && snapshot.stall_evidence == "unknown" && !snapshot.resources.stale && snapshot.resources.state == "active" && bridge.status().frame == 501 &&
        snapshot.samples.size() == 2, "Resource freshness refreshed stale frame timing or changed frame identity");
    near(snapshot.stats.estimated_fps, 100.0, "Resource traffic changed source-time FPS");
    near(snapshot.resources.gpu_heap_age_ms, *snapshot.resources.age_ms + 4000.0, "Resource refresh hid an older heap sample");

    malformed = bridge.status().malformed_packets;
    runtime.send(resources(sequence, 14000000, 3));
    stranger.send(resources(++sequence, 14000000, 3));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        json_object_set_string_member(object, "session_id", "wrong-session");
    }));
    runtime.send(resources(++sequence, 12000000, 3));
    runtime.send(resources(++sequence, 11999999, 3));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        json_object_set_int_member(object, "frame", 999);
    }));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        auto* data = json_object_get_object_member(object, "data");
        json_object_remove_member(json_object_get_object_member(data, "shaders"), "disk_cache_hits");
    }));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        auto* shaders = json_object_get_object_member(json_object_get_object_member(object, "data"), "shaders");
        json_object_set_int_member(shaders, "translations_active", -1);
    }));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        auto* shaders = json_object_get_object_member(json_object_get_object_member(object, "data"), "shaders");
        json_object_set_double_member(shaders, "translations_completed", 1.5);
    }));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        auto* memory = json_object_get_object_member(json_object_get_object_member(object, "data"), "memory");
        json_object_set_string_member(memory, "storage_cache_bytes", "100");
    }));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        auto* memory = json_object_get_object_member(json_object_get_object_member(object, "data"), "memory");
        json_object_set_null_member(memory, "process_rss_timestamp_us");
    }));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        auto* memory = json_object_get_object_member(json_object_get_object_member(object, "data"), "memory");
        json_object_set_int_member(memory, "process_rss_timestamp_us", 14000001);
    }));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        auto* memory = json_object_get_object_member(json_object_get_object_member(object, "data"), "memory");
        json_object_set_null_member(memory, "gpu_heap_budget_bytes");
    }));
    runtime.send(changed(resources(++sequence, 14000000, 3), [](JsonObject* object) {
        auto* memory = json_object_get_object_member(json_object_get_object_member(object, "data"), "memory");
        json_object_set_int_member(memory, "storage_cache_budget_bytes", 0);
    }));
    wait_until([&] { return bridge.status().malformed_packets >= malformed + 11; }, "Invalid resource schema or source timestamp was accepted");
    require(bridge.monitoring_snapshot().resources.sample->timestamp_us == 12000000 && bridge.status().frame == 501,
        "Malformed, duplicate, reordered or wrong-session resource events replaced valid measurements");
    runtime.send(timing(++sequence, 502, 1, 13000000, "null", 3, 0, 2));
    wait_until([&] { return bridge.monitoring_snapshot().output_generation == 2; }, "Resource test output generation did not reset");
    require(bridge.monitoring_snapshot().resources.sample->timestamp_us == 12000000,
        "A VideoOut stream reset discarded process resource gauges");
    const std::string unknown_memory = R"({"process_rss_bytes":null,"process_rss_timestamp_us":null,"sampled_cache_bytes":0,"sampled_cache_budget_bytes":null,"storage_cache_bytes":null,"storage_cache_budget_bytes":null,"gpu_heap_usage_bytes":null,"gpu_heap_budget_bytes":null,"gpu_heap_timestamp_us":null})";
    runtime.send(resources(++sequence, 13000000, 3, 1, unknown_memory));
    wait_until([&] { return bridge.monitoring_snapshot().resources.sample->timestamp_us == 13000000; }, "Decreasing resource gauges were not accepted");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.resources.sample->shaders.translations_active == 1 && !snapshot.resources.process_rss_age_ms &&
        !snapshot.resources.gpu_heap_age_ms && snapshot.resources.sample->memory.sampled_cache_bytes == 0,
        "Resource gauges became cumulative or unavailable sources became zero-valued samples");
    const auto fresh_at = std::chrono::steady_clock::now();
    auto next_heartbeat = fresh_at;
    wait_until([&] {
        if (std::chrono::steady_clock::now() >= next_heartbeat) {
            runtime.send(packet(++sequence, "heartbeat", ",\"monitoring\":{\"enabled\":true,\"epoch\":3}"));
            next_heartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
        return bridge.monitoring_snapshot().resources.stale;
    }, "Ten seconds without resource data did not mark resources stale", std::chrono::seconds(12));
    require(bridge.status().connected && bridge.monitoring_snapshot().resources.state == "stale" &&
        *bridge.monitoring_snapshot().resources.age_ms >= 10000.0 &&
        std::chrono::steady_clock::now() - fresh_at < std::chrono::seconds(12),
        "Heartbeats refreshed resources or resource freshness disconnected the game");
    runtime.send(resources(++sequence, 14000000, 3));
    wait_until([&] { return bridge.monitoring_snapshot().resources.sample->timestamp_us == 14000000; }, "Stale resource snapshot could not refresh");
    runtime.send(packet(++sequence, "heartbeat", ",\"monitoring\":{\"enabled\":false,\"epoch\":4}"));
    wait_until([&] { return !bridge.monitoring_snapshot().enabled; }, "Resource monitoring did not stop");
    snapshot = bridge.monitoring_snapshot();
    require(snapshot.resources.stale && snapshot.resources.state == "disabled" && snapshot.resources.has_sample &&
        snapshot.resources.sample->monitoring_epoch == 3 && snapshot.monitoring_epoch == 4,
        "OFF erased the previous resource snapshot or labeled it current");
    const auto received_before_late = bridge.status().received_events;
    runtime.send(resources(++sequence, 15000000, 3));
    runtime.send(packet(++sequence, "heartbeat", ",\"monitoring\":{\"enabled\":false,\"epoch\":4}"));
    wait_until([&] { return bridge.status().received_events > received_before_late; }, "Resource epoch packet barrier was not received");
    require(bridge.monitoring_snapshot().resources.sample->timestamp_us == 14000000,
        "A late resource epoch changed the retained disabled snapshot");
    runtime.send(packet(++sequence, "heartbeat", ",\"monitoring\":{\"enabled\":true,\"epoch\":5}"));
    wait_until([&] { return bridge.monitoring_snapshot().monitoring_epoch == 5; }, "New resource epoch was not confirmed");
    require(!bridge.monitoring_snapshot().resources.has_sample && !bridge.monitoring_snapshot().has_frames,
        "A new ON epoch inherited old current resources or frame statistics");
    runtime.send(resources(++sequence, 20000000, 5));
    wait_until([&] { return bridge.monitoring_snapshot().resources.has_sample; }, "New epoch did not receive resources");
    require(bridge.monitoring_snapshot().resources.sample->shaders.translations_completed == 9,
        "Monitoring epoch changed process-lifetime shader counters");
    const Json history(bridge.monitoring_snapshot_json(true));
    require(json_object_get_boolean_member(json_object_get_object_member(history.object(), "resources"), "has_sample") &&
        json_array_get_length(json_object_get_array_member(history.object(), "chart_samples")) == 0,
        "Resource-only history snapshot omitted resource state or fabricated chart frames");
    runtime.send(packet(++sequence, "goodbye"));
    wait_until([&] { return !bridge.status().connected; }, "Resource runtime did not disconnect");
    require(bridge.monitoring_snapshot().resources.has_sample && bridge.monitoring_snapshot().resources.stale &&
        bridge.monitoring_snapshot().resources.state == "disconnected", "Disconnect discarded or refreshed resources");
    bridge.end_session("bridge-check-session");
    require(bridge.monitoring_snapshot().resources.has_sample, "Managed process exit erased resource diagnostics");
    bridge.expect_session("resource-next-session", "PPSA01234");
    require(!bridge.monitoring_snapshot().resources.has_sample && !bridge.monitoring_snapshot().resources.age_ms &&
        !bridge.status().has_frame, "A new managed session inherited resource state or frame identity");
}

} // namespace

int main() {
    try {
        std::vector<framerift::MonitoringSample> frames;
        for (std::uint64_t i = 0; i <= 60; ++i) {
            framerift::MonitoringSample frame;
            frame.timestamp_us = 2000000 + i * 1000000 / 60;
            frame.completed_flip_count = i;
            frame.monitoring_epoch = 1;
            frames.push_back(frame);
        }
        require(!framerift::monitoring_fps_at(frames, 59), "FPS needs a full source second");
        require(framerift::monitoring_fps_at(frames, 60) == 60, "Steady completed flips should report 60 FPS");
        auto short_flip = frames.back();
        short_flip.timestamp_us += 2500;
        short_flip.completed_flip_count++;
        short_flip.flip_interval_us = 2500;
        frames.push_back(short_flip);
        require(framerift::monitoring_fps_at(frames, 61) == 61, "A 2.5 ms flip must not report 400 FPS");
        auto resumed = frames.back();
        resumed.timestamp_us += 5000000;
        resumed.completed_flip_count++;
        frames.push_back(resumed);
        require(framerift::monitoring_fps_at(frames, 62) == 1, "Resume after a stall must count only the new completion");
        frames.back().completed_flip_count += 2;
        require(!framerift::monitoring_fps_at(frames, 62), "Loss must not fabricate FPS");
        frames.back().completed_flip_count -= 2;
        frames.back().monitoring_epoch++;
        require(!framerift::monitoring_fps_at(frames, 62), "FPS must not cross monitoring epochs");
        framerift::BridgeServer bridge(0);
        require(bridge.port() != 0 && !bridge.status().connected, "UDP bridge did not start disconnected on an assigned port");
        bridge.expect_session("bridge-check-session", "PPSA01234");
        Runtime runtime(bridge.port());
        Runtime stranger(bridge.port());
        const std::string hello = ",\"game_id\":\"PPSA01234\",\"capabilities\":[\"capture_frame\",\"set_input\"]";

        runtime.send(packet(1, "hello", hello, "wrong-session"));
        runtime.send("{invalid-json");
        wait_until([&] { return bridge.status().malformed_packets != 0; }, "Malformed UDP data was not counted");
        require(!bridge.status().connected, "A wrong session or malformed packet connected the runtime");
        runtime.send(packet(1, "hello", hello));
        wait_until([&] { return bridge.status().connected; }, "The expected runtime did not connect");
        require(bridge.status().session_id == "bridge-check-session" && bridge.status().game_id == "PPSA01234", "Hello lost session or game identity");
        runtime.send(packet(2, "event", ",\"name\":\"frame\",\"frame\":523,\"data\":{\"fps\":59.5}"));
        runtime.send(packet(4, "event", ",\"name\":\"frame\",\"frame\":563,\"data\":{\"fps\":60}"));
        wait_until([&] { return bridge.status().frame == 563; }, "Live UDP frame events were not received");
        require(bridge.status().missing_events == 1, "A UDP sequence gap was not reported");
        const auto received = bridge.status().received_events;
        stranger.send(packet(5, "event", ",\"name\":\"frame\",\"frame\":999,\"data\":{}"));
        runtime.send(packet(5, "event", ",\"name\":\"frame\",\"frame\":564,\"data\":{}"));
        runtime.send(packet(6, "event", ",\"name\":\"frame\",\"frame\":999,\"data\":{}", "wrong-session"));
        runtime.send(packet(6, "event", ",\"name\":\"frame\",\"frame\":565,\"data\":{}"));
        wait_until([&] { return bridge.status().frame == 565; }, "Accepted UDP events stopped after rejected packets");
        require(bridge.status().received_events == received + 2 && bridge.status().missing_events == 1,
            "A different UDP peer or session changed accepted event history");
        const auto malformed_before_duplicate = bridge.status().malformed_packets;
        runtime.send(packet(4, "event", ",\"name\":\"frame\",\"frame\":999,\"data\":{}"));
        runtime.send(packet(4, "goodbye"));
        runtime.send(packet(4, "hello", ",\"game_id\":\"PPSA01234\",\"capabilities\":[]"));
        runtime.send("{invalid-json");
        wait_until([&] { return bridge.status().malformed_packets > malformed_before_duplicate; }, "Duplicate telemetry packet barrier was not received");
        require(bridge.status().connected && bridge.status().frame == 565 && bridge.status().received_events == received + 2,
            "Stale UDP packets disconnected the runtime or changed accepted telemetry");

        const Json history(bridge.events_json(0, 100));
        auto* events = json_object_get_array_member(history.object(), "events");
        require(events != nullptr && json_array_get_length(events) >= 4, "Live events were not retained for MCP polling");
        const auto cursor = static_cast<std::uint64_t>(json_object_get_int_member(json_array_get_object_element(events, 0), "event_id"));
        const Json page(bridge.events_json(cursor, 1));
        auto* following = json_object_get_array_member(page.object(), "events");
        require(following && json_array_get_length(following) == 1 &&
            static_cast<std::uint64_t>(json_object_get_int_member(json_array_get_object_element(following, 0), "event_id")) > cursor,
            "Event polling did not respect its cursor and limit");

        const Json sent(bridge.send_command("capture_frame", "{\"frame\":600}"));
        const auto command_id = member(sent.object(), "command_id");
        const Json command(runtime.receive());
        require(member(command.object(), "type") == "command" && member(command.object(), "command_id") == command_id &&
            member(command.object(), "name") == "capture_frame" &&
            json_object_get_int_member(json_object_get_object_member(command.object(), "args"), "frame") == 600,
            "Command delivery changed the ID, name or arguments");
        const Json retried(runtime.receive());
        require(member(retried.object(), "command_id") == command_id && member(retried.object(), "name") == "capture_frame",
            "A lost command receipt was not retried with the same command ID");
        runtime.send(packet(7, "command_received", ",\"command_id\":\"" + command_id + "\""));
        wait_until([&] { return member(Json(bridge.command_json(command_id)).object(), "status") == "received"; }, "Command receipt was not acknowledged");
        runtime.send(packet(8, "command_completed", ",\"command_id\":\"" + command_id + "\",\"result\":{\"frame\":600}"));
        wait_until([&] { return member(Json(bridge.command_json(command_id)).object(), "status") == "completed"; }, "Command completion was not retained");
        rejected([&] { bridge.send_command("schedule_inputs", "{}"); }, "An unadvertised runtime command was sent");

        framerift::McpServer mcp(bridge, 0);
        require(mcp.port() != 0, "MCP did not listen on an assigned port");
        const auto initialized = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"bridge-check","version":"1"}}})");
        require(initialized.status == 200, "MCP initialize failed over HTTP");
        const Json initialized_json(initialized.body);
        require(member(rpc_result(initialized_json), "protocolVersion") == "2025-11-25", "MCP negotiated the wrong protocol version");
        const auto listed = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");
        const Json listed_json(listed.body);
        auto* tools = json_object_get_array_member(rpc_result(listed_json), "tools");
        require(listed.status == 200 && tools && json_array_get_length(tools) == 5, "MCP did not expose the five bridge tools");
        for (const auto* expected : {"get_bridge_status", "get_monitoring_snapshot", "get_events", "send_command", "get_command"}) {
            bool found = false;
            for (guint index = 0; index < json_array_get_length(tools); ++index)
                found |= member(json_array_get_object_element(tools, index), "name") == expected;
            require(found, "An expected MCP bridge tool is missing");
        }
        const auto queried = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_bridge_status","arguments":{}}})");
        const Json queried_json(queried.body);
        require(queried.status == 200 && json_object_get_boolean_member(tool_result(queried_json), "connected") &&
            json_object_get_int_member(tool_result(queried_json), "frame") == 565, "MCP status did not reflect the real UDP runtime");
        const auto polled = request(mcp.endpoint(), "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\",\"params\":{\"name\":\"get_events\",\"arguments\":{\"after\":" + std::to_string(cursor) + ",\"limit\":1}}}");
        const Json polled_json(polled.body);
        auto* polled_events = json_object_get_array_member(tool_result(polled_json), "events");
        require(polled.status == 200 && polled_events && json_array_get_length(polled_events) == 1 &&
            static_cast<std::uint64_t>(json_object_get_int_member(json_array_get_object_element(polled_events, 0), "event_id")) > cursor,
            "MCP event polling lost the UDP event cursor");
        const auto dispatched = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"send_command","arguments":{"name":"set_input","args":{"buttons":["cross"]}}}})");
        const Json dispatched_json(dispatched.body);
        const auto mcp_command_id = member(tool_result(dispatched_json), "command_id");
        const Json delivered(runtime.receive());
        require(dispatched.status == 200 && member(delivered.object(), "command_id") == mcp_command_id &&
            member(delivered.object(), "name") == "set_input", "MCP did not dispatch an actual UDP command");
        const auto pending = request(mcp.endpoint(), "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"get_command\",\"arguments\":{\"command_id\":\"" + mcp_command_id + "\"}}}");
        const Json pending_json(pending.body);
        require(pending.status == 200 && member(tool_result(pending_json), "status") == "pending", "MCP command polling returned a wrong status");

        const auto malformed = request(mcp.endpoint(), "{invalid-json");
        const Json malformed_json(malformed.body);
        require(malformed.status == 400 && json_object_get_int_member(json_object_get_object_member(malformed_json.object(), "error"), "code") == -32700,
            "MCP did not reject malformed JSON with a parse error");
        const auto unknown = request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":6,"method":"unknown-method"})");
        const Json unknown_json(unknown.body);
        require(unknown.status == 200 && json_object_get_int_member(json_object_get_object_member(unknown_json.object(), "error"), "code") == -32601,
            "MCP did not report an unknown JSON-RPC method");
        require(request(mcp.endpoint(), R"({"jsonrpc":"2.0","id":7,"method":"tools/list"})", "POST", "https://remote.invalid").status == 403,
            "MCP accepted a remote browser Origin");
        const auto get = request(mcp.endpoint(), {}, "GET");
        require(get.status == 405 && get.allow == "POST", "MCP accepted GET or omitted its allowed method");
        require(request(mcp.endpoint(), std::string(65537, 'x')).status == 413, "MCP accepted an oversized request body");

        runtime.send(packet(9, "goodbye"));
        wait_until([&] { return !bridge.status().connected; }, "Runtime goodbye did not disconnect the bridge");
        require(member(Json(bridge.command_json(mcp_command_id)).object(), "status") == "disconnected", "Disconnection left a pending command unresolved");
        rejected([&] { bridge.send_command("set_input", "{}"); }, "A command was sent after runtime disconnection");
        bridge.end_session("bridge-check-session");
        runtime.send(packet(10, "hello", hello));
        const auto malformed_before = bridge.status().malformed_packets;
        runtime.send("{invalid-json");
        wait_until([&] { return bridge.status().malformed_packets > malformed_before; }, "Late-session packet barrier was not received");
        require(!bridge.status().connected, "A late hello reconnected an ended game session");

        bridge.expect_session("ring-check-session", "PPSA01234");
        runtime.send(packet(1, "hello", hello, "ring-check-session"));
        wait_until([&] { return bridge.status().connected; }, "A new expected session could not connect");
        for (std::uint64_t sent_count = 0; sent_count < 520; sent_count += 8) {
            for (std::uint64_t index = sent_count; index < sent_count + 8; ++index)
                runtime.send(packet(index + 2, "event", ",\"name\":\"frame\",\"data\":{}", "ring-check-session"));
            wait_until([&] { return bridge.status().received_events == sent_count + 8; }, "Bounded telemetry history stopped accepting live events");
        }
        const Json retained(bridge.events_json(0, 512));
        auto* retained_events = json_object_get_array_member(retained.object(), "events");
        require(retained_events && json_array_get_length(retained_events) == 512 &&
            json_object_get_boolean_member(retained.object(), "lost_history"), "Telemetry retention did not stay bounded or report an expired cursor");
        rejected([&] { bridge.events_json(0, 513); }, "An unbounded event page was accepted");
        wait_until([&] { return !bridge.status().connected; }, "A silent runtime did not expire its connection", std::chrono::seconds(7));
        require(!bridge.status().error.empty(), "Runtime heartbeat expiry was not reported");
        bridge.end_session("ring-check-session");
        check_automatic_monitoring();
        check_monitoring();
        check_resources();
        check_work_activity();
        std::cout << "UDP session, frame and resource monitoring, command receipt and HTTP MCP checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
