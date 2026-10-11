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
#include <cmath>
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
constexpr std::size_t monitoring_limit = 8192;
constexpr std::uint64_t monitoring_history_us = 60 * 1000000;
constexpr std::uint64_t monitoring_window_us = 1000000;
constexpr double resource_stale_ms = 10000.0;
using Clock = std::chrono::steady_clock;

constexpr std::pair<const char*, std::uint64_t ResourceShaderState::*> resource_shader_fields[] = {
    {"translations_active", &ResourceShaderState::translations_active},
    {"translations_completed", &ResourceShaderState::translations_completed},
    {"cached_variants_count", &ResourceShaderState::cached_variants_count},
    {"source_code_bytes", &ResourceShaderState::source_code_bytes},
    {"variant_spirv_bytes", &ResourceShaderState::variant_spirv_bytes},
    {"disk_cache_hits", &ResourceShaderState::disk_cache_hits},
    {"disk_cache_misses", &ResourceShaderState::disk_cache_misses},
    {"disk_cache_writes", &ResourceShaderState::disk_cache_writes},
    {"disk_cache_load_failures", &ResourceShaderState::disk_cache_load_failures},
    {"disk_cache_write_failures", &ResourceShaderState::disk_cache_write_failures},
    {"preparation_queue_pending", &ResourceShaderState::preparation_queue_pending},
    {"preparation_active", &ResourceShaderState::preparation_active},
    {"pipeline_builds_active", &ResourceShaderState::pipeline_builds_active},
    {"pipeline_builds_completed", &ResourceShaderState::pipeline_builds_completed},
    {"pipeline_optimization_queue_pending", &ResourceShaderState::pipeline_optimization_queue_pending},
    {"pipeline_optimizations_active", &ResourceShaderState::pipeline_optimizations_active}
};

constexpr std::pair<const char*, std::optional<std::uint64_t> ResourceMemoryState::*> resource_memory_fields[] = {
    {"process_rss_bytes", &ResourceMemoryState::process_rss_bytes},
    {"process_rss_timestamp_us", &ResourceMemoryState::process_rss_timestamp_us},
    {"sampled_cache_bytes", &ResourceMemoryState::sampled_cache_bytes},
    {"sampled_cache_budget_bytes", &ResourceMemoryState::sampled_cache_budget_bytes},
    {"storage_cache_bytes", &ResourceMemoryState::storage_cache_bytes},
    {"storage_cache_budget_bytes", &ResourceMemoryState::storage_cache_budget_bytes},
    {"gpu_heap_usage_bytes", &ResourceMemoryState::gpu_heap_usage_bytes},
    {"gpu_heap_budget_bytes", &ResourceMemoryState::gpu_heap_budget_bytes},
    {"gpu_heap_timestamp_us", &ResourceMemoryState::gpu_heap_timestamp_us}
};

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

std::optional<std::uint64_t> nullable_unsigned_member(JsonObject* object, const char* name) {
    auto* node = json_object_get_member(object, name);
    if (node && JSON_NODE_HOLDS_NULL(node)) return {};
    return unsigned_member(object, name);
}

JsonObject* object_member(JsonObject* object, const char* name) {
    auto* node = json_object_get_member(object, name);
    if (!node || !JSON_NODE_HOLDS_OBJECT(node))
        throw std::runtime_error(std::string("Expected object: ") + name);
    return json_node_get_object(node);
}

bool boolean_member(JsonObject* object, const char* name) {
    auto* node = json_object_get_member(object, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_BOOLEAN)
        throw std::runtime_error(std::string("Expected boolean: ") + name);
    return json_node_get_boolean(node);
}

std::optional<std::pair<bool, std::uint64_t>> monitoring_member(JsonObject* object) {
    auto* node = json_object_get_member(object, "monitoring");
    if (!node) return {};
    if (!JSON_NODE_HOLDS_OBJECT(node)) throw std::runtime_error("Expected monitoring object");
    auto* value = json_node_get_object(node);
    return std::pair{boolean_member(value, "enabled"), unsigned_member(value, "epoch")};
}

MonitoringSample timing_sample(JsonObject* object, JsonObject* data) {
    MonitoringSample sample;
    sample.frame = unsigned_member(object, "frame");
    sample.output_handle = unsigned_member(data, "output_handle");
    sample.output_generation = unsigned_member(data, "output_generation");
    sample.completed_flip_count = unsigned_member(data, "completed_flip_count");
    if (!sample.completed_flip_count) throw std::runtime_error("Expected a completed flip count greater than zero");
    sample.timestamp_us = unsigned_member(data, "timestamp_us");
    sample.monitoring_epoch = unsigned_member(data, "monitoring_epoch");
    sample.dropped_samples = unsigned_member(data, "dropped_samples");
    auto* interval = json_object_get_member(data, "flip_interval_us");
    if (!interval) throw std::runtime_error("Expected flip_interval_us");
    if (!JSON_NODE_HOLDS_NULL(interval)) {
        sample.flip_interval_us = unsigned_member(data, "flip_interval_us");
        if (!*sample.flip_interval_us) throw std::runtime_error("Expected a positive flip interval");
    }
    return sample;
}

constexpr std::array<std::pair<const char*, std::uint64_t ResourceWorkState::*>, 6> resource_work_fields = {{
    {"active", &ResourceWorkState::active}, {"started", &ResourceWorkState::started},
    {"completed", &ResourceWorkState::completed}, {"failed", &ResourceWorkState::failed},
    {"total_us", &ResourceWorkState::total_us}, {"max_us", &ResourceWorkState::max_us}
}};

JsonObject* activity_json(const ResourceActivity& activity) {
    auto* object = json_object_new();
    for (unsigned i = 0; i < activity.size(); ++i) {
        auto* work = json_object_new();
        for (const auto& [name, field] : resource_work_fields)
            json_object_set_int_member(work, name, activity[i].*field);
        json_object_set_object_member(object, resource_work_names[i], work);
    }
    return object;
}

ResourceSample resource_sample(JsonObject* data) {
    ResourceSample sample;
    sample.timestamp_us = unsigned_member(data, "timestamp_us");
    sample.monitoring_epoch = unsigned_member(data, "monitoring_epoch");
    auto* shaders = object_member(data, "shaders");
    for (const auto& [name, field] : resource_shader_fields)
        sample.shaders.*field = unsigned_member(shaders, name);
    auto* memory = object_member(data, "memory");
    for (const auto& [name, field] : resource_memory_fields)
        sample.memory.*field = nullable_unsigned_member(memory, name);
    if (json_object_has_member(data, "activity")) {
        auto* activity = object_member(data, "activity");
        sample.activity.emplace();
        for (unsigned i = 0; i < resource_work_names.size(); ++i) {
            auto* work = object_member(activity, resource_work_names[i]);
            for (const auto& [name, field] : resource_work_fields)
                (*sample.activity)[i].*field = unsigned_member(work, name);
        }
    }
    const auto& values = sample.memory;
    if (values.process_rss_bytes.has_value() != values.process_rss_timestamp_us.has_value())
        throw std::runtime_error("RSS bytes and timestamp must be available together");
    const bool any_heap = values.gpu_heap_usage_bytes || values.gpu_heap_budget_bytes || values.gpu_heap_timestamp_us;
    if (any_heap && !(values.gpu_heap_usage_bytes && values.gpu_heap_budget_bytes && values.gpu_heap_timestamp_us))
        throw std::runtime_error("GPU heap usage, budget and timestamp must be available together");
    for (const auto& budget : {values.sampled_cache_budget_bytes, values.storage_cache_budget_bytes, values.gpu_heap_budget_bytes})
        if (budget && !*budget) throw std::runtime_error("An unavailable memory budget must be null");
    for (const auto& timestamp : {values.process_rss_timestamp_us, values.gpu_heap_timestamp_us})
        if (timestamp && *timestamp > sample.timestamp_us)
            throw std::runtime_error("Memory source timestamp exceeds the resource timestamp");
    return sample;
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

std::optional<double> monitoring_fps_at(const std::vector<MonitoringSample>& samples, std::size_t index) {
    if (index >= samples.size()) return {};
    const auto& last = samples[index];
    if (last.timestamp_us < 1000000) return {};
    const auto start_us = last.timestamp_us - 1000000;
    const auto after = std::upper_bound(samples.begin(), samples.begin() + index + 1, start_us,
        [](std::uint64_t timestamp, const MonitoringSample& sample) { return timestamp < sample.timestamp_us; });
    if (after == samples.begin()) return {};
    const auto& baseline = *std::prev(after);
    const auto count = static_cast<std::uint64_t>(samples.begin() + index + 1 - after);
    if (baseline.monitoring_epoch != last.monitoring_epoch || baseline.output_handle != last.output_handle ||
        baseline.output_generation != last.output_generation || last.completed_flip_count < baseline.completed_flip_count ||
        last.completed_flip_count - baseline.completed_flip_count != count) return {};
    return static_cast<double>(count);
}

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
    bool monitoring_known = false;
    bool monitoring_enabled = false;
    std::uint64_t monitoring_epoch = 0;
    std::uint64_t monitoring_packet_base = 0;
    std::uint64_t monitoring_drops = 0;
    std::uint64_t monitoring_request_epoch = 0;
    std::optional<bool> monitoring_requested;
    std::string monitoring_command;
    bool monitoring_confirmed_request = false;
    bool monitoring_baseline_known = false;
    bool monitoring_truncated = false;
    Clock::time_point last_sample;
    std::deque<MonitoringSample> monitoring_samples;
    Clock::time_point last_resource;
    std::optional<ResourceSample> resource;
    std::deque<ResourceSample> resource_history;

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
        monitoring_known = false;
        monitoring_enabled = false;
        monitoring_epoch = 0;
        monitoring_requested.reset();
        monitoring_command.clear();
        monitoring_confirmed_request = false;
        clear_monitoring();
    }

    void clear_monitoring() {
        monitoring_samples.clear();
        resource.reset();
        resource_history.clear();
        monitoring_baseline_known = false;
        monitoring_truncated = false;
        monitoring_drops = 0;
        monitoring_packet_base = state.missing_events;
    }

    void update_monitoring(bool enabled, std::uint64_t epoch) {
        if (monitoring_known && epoch < monitoring_epoch) return;
        if (monitoring_known && epoch == monitoring_epoch && enabled != monitoring_enabled)
            throw std::runtime_error("Monitoring state changed without a new epoch");
        if (enabled && (!monitoring_known || epoch > monitoring_epoch)) clear_monitoring();
        monitoring_known = true;
        monitoring_enabled = enabled;
        monitoring_epoch = epoch;
        if (monitoring_requested && *monitoring_requested == enabled && epoch >= monitoring_request_epoch)
            monitoring_confirmed_request = true;
    }

    void accept_sample(MonitoringSample sample) {
        if (!capabilities.contains("set_monitoring")) throw std::runtime_error("Unadvertised monitoring stream");
        if (monitoring_known && (sample.monitoring_epoch < monitoring_epoch ||
            (sample.monitoring_epoch == monitoring_epoch && !monitoring_enabled))) return;
        const bool new_epoch = !monitoring_known || sample.monitoring_epoch > monitoring_epoch;
        const bool new_stream = monitoring_samples.empty() || new_epoch ||
            monitoring_samples.back().output_handle != sample.output_handle ||
            monitoring_samples.back().output_generation != sample.output_generation;
        if (!new_epoch && sample.dropped_samples < monitoring_drops)
            throw std::runtime_error("Monitoring drop count regressed");
        if (!new_stream) {
            const auto& previous = monitoring_samples.back();
            if (sample.timestamp_us <= previous.timestamp_us || sample.completed_flip_count <= previous.completed_flip_count)
                throw std::runtime_error("Monitoring source counters are not increasing");
            if (!sample.flip_interval_us) throw std::runtime_error("Unexpected monitoring interval reset");
            const auto elapsed = sample.timestamp_us - previous.timestamp_us;
            if (*sample.flip_interval_us > elapsed + 1)
                throw std::runtime_error("Monitoring interval exceeds its source-time span");
            sample.missing_before = sample.completed_flip_count - previous.completed_flip_count - 1;
            if (!sample.missing_before && (elapsed > *sample.flip_interval_us + 1))
                throw std::runtime_error("Monitoring interval disagrees with source timestamps");
        } else if (!new_epoch && !monitoring_samples.empty() &&
            sample.output_handle == monitoring_samples.back().output_handle &&
            sample.output_generation < monitoring_samples.back().output_generation) {
            throw std::runtime_error("Monitoring output generation regressed");
        }
        update_monitoring(true, sample.monitoring_epoch);
        if (new_stream) {
            monitoring_samples.clear();
            monitoring_truncated = false;
            monitoring_baseline_known = !sample.flip_interval_us.has_value();
        }
        monitoring_drops = sample.dropped_samples;
        last_sample = Clock::now();
        monitoring_samples.push_back(std::move(sample));
        const auto newest = monitoring_samples.back().timestamp_us;
        while (monitoring_samples.size() > 1 && newest - monitoring_samples.front().timestamp_us > monitoring_history_us)
            monitoring_samples.pop_front();
        while (monitoring_samples.size() > monitoring_limit) {
            monitoring_samples.pop_front();
            monitoring_truncated = true;
        }
    }

    void accept_resource(ResourceSample sample) {
        if (!capabilities.contains("set_monitoring") || !capabilities.contains("resource_status"))
            throw std::runtime_error("Unadvertised resource stream");
        if (monitoring_known && (sample.monitoring_epoch < monitoring_epoch ||
            (sample.monitoring_epoch == monitoring_epoch && !monitoring_enabled))) return;
        if (resource && sample.monitoring_epoch == resource->monitoring_epoch && sample.timestamp_us <= resource->timestamp_us)
            throw std::runtime_error("Resource source timestamp is not increasing");
        if (sample.activity && !capabilities.contains("work_activity"))
            throw std::runtime_error("Unadvertised work activity");
        if (capabilities.contains("work_activity") && !sample.activity)
            throw std::runtime_error("Missing work activity");
        update_monitoring(true, sample.monitoring_epoch);
        resource_history.push_back(sample);
        while (resource_history.size() > 64 || (resource_history.size() > 2 &&
            sample.timestamp_us - resource_history.front().timestamp_us > 10000000))
            resource_history.pop_front();
        resource = std::move(sample);
        last_resource = Clock::now();
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
        std::optional<MonitoringSample> sample;
        std::optional<ResourceSample> resource_update;
        std::optional<std::pair<bool, std::uint64_t>> monitoring;
        if (type == "hello" || type == "heartbeat") monitoring = monitoring_member(object);
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
            const auto name = string_member(object, "name", 128);
            auto* data = json_object_get_member(object, "data");
            if (!data || !JSON_NODE_HOLDS_OBJECT(data)) throw std::runtime_error("Expected event data object");
            if (name == "frame_timing") sample = timing_sample(object, json_node_get_object(data));
            else if (name == "resource_status") {
                if (has_frame) throw std::runtime_error("Resource status has no frame correlation");
                resource_update = resource_sample(json_node_get_object(data));
            }
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
        const bool old_sequence = sequence_initialized && sequence <= incoming_sequence;
        if (monitoring && !old_sequence) update_monitoring(monitoring->first, monitoring->second);
        if (sample && !old_sequence) accept_sample(std::move(*sample));
        if (resource_update && !old_sequence) accept_resource(std::move(*resource_update));
        if (type == "command_completed") {
            const auto found = std::find_if(commands.begin(), commands.end(), [&](const Command& command) {
                return command.id == command_id && command.session_id == session && command.name == "set_monitoring" && pending(command.state);
            });
            if (found != commands.end()) {
                auto value = parse_object(result);
                auto* values = json_node_get_object(value.get());
                update_monitoring(boolean_member(values, "enabled"), unsigned_member(values, "epoch"));
                if (command_id == monitoring_command) monitoring_confirmed_request = true;
            }
        }
        last_packet = Clock::now();
        if (!sequence_initialized) {
            incoming_sequence = sequence;
            sequence_initialized = true;
        } else if (sequence > incoming_sequence) {
            state.missing_events += sequence - incoming_sequence - 1;
            incoming_sequence = sequence;
        }
        if (has_frame && !(type == "event" && old_sequence)) {
            state.has_frame = true;
            state.frame = std::max(state.frame, frame);
        }
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
    json_object_set_boolean_member(object, "has_frame", state.has_frame);
    if (state.has_frame) json_object_set_int_member(object, "frame", state.frame);
    else json_object_set_null_member(object, "frame");
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
    std::optional<bool> monitoring_request;
    if (name == "set_monitoring") {
        auto* values = json_node_get_object(arguments.get());
        if (json_object_get_size(values) != 1) throw std::runtime_error("set_monitoring requires only enabled");
        monitoring_request = boolean_member(values, "enabled");
    }
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
    if (monitoring_request) {
        impl_->monitoring_command = command.id;
        impl_->monitoring_requested = monitoring_request;
        impl_->monitoring_request_epoch = impl_->monitoring_epoch;
        impl_->monitoring_confirmed_request = false;
    }
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

MonitoringSnapshot BridgeServer::monitoring_snapshot() const {
    MonitoringSnapshot snapshot;
    bool baseline_known = false;
    std::uint64_t packet_base = 0;
    {
        std::lock_guard lock(impl_->mutex);
        snapshot.connected = impl_->state.connected;
        snapshot.supported = impl_->capabilities.contains("set_monitoring");
        snapshot.enabled = impl_->monitoring_enabled;
        snapshot.monitoring_epoch = impl_->monitoring_epoch;
        snapshot.session_id = impl_->state.session_id;
        snapshot.game_id = impl_->state.game_id;
        snapshot.error = impl_->state.error;
        snapshot.command_id = impl_->monitoring_command;
        snapshot.requested_enabled = impl_->monitoring_requested;
        const auto command = std::find_if(impl_->commands.begin(), impl_->commands.end(), [&](const auto& value) {
            return value.id == impl_->monitoring_command;
        });
        if (command != impl_->commands.end()) {
            snapshot.command_status = command->state;
            snapshot.pending = pending(command->state) && !impl_->monitoring_confirmed_request;
            if (!command->error.empty()) snapshot.error = command->error;
        }
        snapshot.samples.assign(impl_->monitoring_samples.begin(), impl_->monitoring_samples.end());
        snapshot.history_truncated = impl_->monitoring_truncated;
        baseline_known = impl_->monitoring_baseline_known;
        packet_base = impl_->monitoring_packet_base;
        snapshot.stats.packet_gaps = impl_->state.missing_events - packet_base;
        snapshot.stats.sender_dropped_samples = impl_->monitoring_drops;
        if (!snapshot.samples.empty())
            snapshot.age_ms = std::chrono::duration<double, std::milli>(Clock::now() - impl_->last_sample).count();
        snapshot.resources.supported = impl_->capabilities.contains("resource_status");
        snapshot.resources.sample = impl_->resource;
        snapshot.resources.activity_supported = impl_->capabilities.contains("work_activity");
        if (impl_->resource && impl_->resource->activity && impl_->resource_history.size() > 1) {
            const auto& latest = *impl_->resource;
            const auto start = latest.timestamp_us > 1000000 ? latest.timestamp_us - 1000000 : 0;
            const ResourceSample* baseline = &impl_->resource_history.front();
            for (const auto& item : impl_->resource_history) {
                if (item.timestamp_us > start) break;
                baseline = &item;
            }
            if (baseline->activity && baseline->timestamp_us < latest.timestamp_us) {
                ResourceActivity delta = *latest.activity;
                bool valid = true;
                for (unsigned i = 0; i < delta.size(); ++i) {
                    for (const auto& [name, field] : resource_work_fields) {
                        if (field == &ResourceWorkState::active || field == &ResourceWorkState::max_us) continue;
                        if (delta[i].*field < (*baseline->activity)[i].*field) valid = false;
                        else delta[i].*field -= (*baseline->activity)[i].*field;
                    }
                }
                if (valid) {
                    snapshot.resources.work_delta = delta;
                    snapshot.resources.work_window_us = latest.timestamp_us - baseline->timestamp_us;
                }
            }
        }
        if (impl_->resource)
            snapshot.resources.age_ms = std::chrono::duration<double, std::milli>(Clock::now() - impl_->last_resource).count();
    }
    auto& resources = snapshot.resources;
    resources.has_sample = resources.sample.has_value();
    resources.stale = resources.has_sample && (!snapshot.connected || !resources.supported || !snapshot.enabled ||
        *resources.age_ms >= resource_stale_ms);
    if (!snapshot.connected) resources.state = "disconnected";
    else if (!resources.supported) resources.state = "unsupported";
    else if (!snapshot.enabled) resources.state = "disabled";
    else if (!resources.has_sample) resources.state = "waiting";
    else resources.state = resources.stale ? "stale" : "active";
    if (resources.sample) {
        const auto source_age = [&](const std::optional<std::uint64_t>& timestamp) -> std::optional<double> {
            if (!timestamp) return {};
            return *resources.age_ms + static_cast<double>(resources.sample->timestamp_us - *timestamp) / 1000.0;
        };
        resources.process_rss_age_ms = source_age(resources.sample->memory.process_rss_timestamp_us);
        resources.gpu_heap_age_ms = source_age(resources.sample->memory.gpu_heap_timestamp_us);
    }
    snapshot.has_frames = !snapshot.samples.empty();
    snapshot.stale = snapshot.has_frames && (!snapshot.connected || !snapshot.enabled || *snapshot.age_ms >= 1000.0);
    if (!snapshot.connected) snapshot.state = "disconnected";
    else if (!snapshot.supported) snapshot.state = "unsupported";
    else if (snapshot.pending) snapshot.state = "pending";
    else if (!snapshot.enabled) snapshot.state = "disabled";
    else if (!snapshot.has_frames) snapshot.state = "waiting";
    else snapshot.state = snapshot.stale ? "stale" : "active";
    if (snapshot.connected && snapshot.enabled && snapshot.stale) {
        bool active = false;
        if (resources.sample && resources.sample->activity && !resources.stale)
            for (const auto& work : *resources.sample->activity) active = active || work.active != 0;
        snapshot.stall_evidence = active ? "instrumented_work_active" : "unknown";
    }
    if (!snapshot.has_frames) return snapshot;
    snapshot.output_handle = snapshot.samples.back().output_handle;
    snapshot.output_generation = snapshot.samples.back().output_generation;
    auto& stats = snapshot.stats;
    stats.window_end_us = snapshot.samples.back().timestamp_us;
    stats.window_start_us = stats.window_end_us > monitoring_window_us ? stats.window_end_us - monitoring_window_us : 0;
    const auto first = std::upper_bound(snapshot.samples.begin(), snapshot.samples.end(), stats.window_start_us,
        [](std::uint64_t timestamp, const MonitoringSample& value) { return timestamp < value.timestamp_us; });
    std::vector<std::uint64_t> intervals;
    long double sum = 0;
    for (auto iterator = first; iterator != snapshot.samples.end(); ++iterator) {
        stats.missing_samples += iterator->missing_before;
        if (!iterator->flip_interval_us) continue;
        ++stats.observed_samples;
        intervals.push_back(*iterator->flip_interval_us);
        sum += *iterator->flip_interval_us;
    }
    stats.sample_count = intervals.size();
    stats.expected_samples = stats.observed_samples + stats.missing_samples;
    const bool crosses_unknown_gap = first != snapshot.samples.begin() && first != snapshot.samples.end() &&
        first->missing_before && std::prev(first)->timestamp_us < stats.window_start_us;
    const bool reliable = (first != snapshot.samples.begin() || baseline_known) && !crosses_unknown_gap;
    if (stats.expected_samples && reliable)
        stats.coverage = static_cast<double>(stats.observed_samples) / static_cast<double>(stats.expected_samples);
    stats.complete = reliable && snapshot.samples.front().timestamp_us <= stats.window_start_us &&
        stats.window_end_us - snapshot.samples.front().timestamp_us >= monitoring_window_us && !stats.missing_samples;
    if (intervals.empty()) {
        if (snapshot.state == "active") snapshot.state = "waiting_for_intervals";
        return snapshot;
    }
    stats.mean_interval_us = static_cast<double>(sum / intervals.size());
    if (sum > 0) stats.estimated_fps = static_cast<double>(intervals.size() * 1000000.0L / sum);
    std::sort(intervals.begin(), intervals.end());
    stats.p95_interval_us = static_cast<double>(intervals[(95 * intervals.size() + 99) / 100 - 1]);
    stats.worst_interval_us = static_cast<double>(intervals.back());
    return snapshot;
}

std::string BridgeServer::monitoring_snapshot_json(bool include_history) const {
    const auto snapshot = monitoring_snapshot();
    auto node = new_object();
    auto* object = json_node_get_object(node.get());
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, bool>>{
        {"connected", snapshot.connected}, {"supported", snapshot.supported}, {"enabled", snapshot.enabled},
        {"pending", snapshot.pending}, {"has_frames", snapshot.has_frames}, {"stale", snapshot.stale},
        {"history_truncated", snapshot.history_truncated}}) json_object_set_boolean_member(object, name, value);
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, std::string>>{
        {"state", snapshot.state}, {"session_id", snapshot.session_id}, {"game_id", snapshot.game_id},
        {"command_id", snapshot.command_id}, {"command_status", snapshot.command_status}, {"error", snapshot.error}})
        json_object_set_string_member(object, name, value.c_str());
    if (snapshot.requested_enabled) json_object_set_boolean_member(object, "requested_enabled", *snapshot.requested_enabled);
    else json_object_set_null_member(object, "requested_enabled");
    const auto number = [](JsonObject* target, const char* name, const std::optional<double>& value) {
        if (value) json_object_set_double_member(target, name, *value);
        else json_object_set_null_member(target, name);
    };
    number(object, "age_ms", snapshot.age_ms);
    json_object_set_int_member(object, "monitoring_epoch", snapshot.monitoring_epoch);
    if (snapshot.stall_evidence.empty()) json_object_set_null_member(object, "stall_evidence");
    else json_object_set_string_member(object, "stall_evidence", snapshot.stall_evidence.c_str());
    auto* resources = json_object_new();
    const auto& resource = snapshot.resources;
    json_object_set_boolean_member(resources, "supported", resource.supported);
    json_object_set_boolean_member(resources, "activity_supported", resource.activity_supported);
    if (resource.sample && resource.sample->activity)
        json_object_set_object_member(resources, "activity", activity_json(*resource.sample->activity));
    else json_object_set_null_member(resources, "activity");
    if (resource.work_delta) {
        auto* window = json_object_new();
        json_object_set_int_member(window, "duration_us", resource.work_window_us);
        json_object_set_int_member(window, "start_us", resource.sample->timestamp_us - resource.work_window_us);
        json_object_set_int_member(window, "end_us", resource.sample->timestamp_us);
        json_object_set_object_member(window, "delta", activity_json(*resource.work_delta));
        json_object_set_object_member(resources, "activity_window", window);
    } else json_object_set_null_member(resources, "activity_window");
    json_object_set_boolean_member(resources, "has_sample", resource.has_sample);
    json_object_set_boolean_member(resources, "stale", resource.stale);
    json_object_set_string_member(resources, "state", resource.state.c_str());
    number(resources, "age_ms", resource.age_ms);
    number(resources, "process_rss_age_ms", resource.process_rss_age_ms);
    number(resources, "gpu_heap_age_ms", resource.gpu_heap_age_ms);
    json_object_set_int_member(resources, "stale_after_ms", static_cast<std::int64_t>(resource_stale_ms));
    if (resource.sample) {
        const auto& sample = *resource.sample;
        json_object_set_int_member(resources, "timestamp_us", sample.timestamp_us);
        json_object_set_int_member(resources, "monitoring_epoch", sample.monitoring_epoch);
        auto* shaders = json_object_new();
        for (const auto& [name, field] : resource_shader_fields)
            json_object_set_int_member(shaders, name, sample.shaders.*field);
        json_object_set_object_member(resources, "shaders", shaders);
        auto* memory = json_object_new();
        for (const auto& [name, field] : resource_memory_fields) {
            const auto& value = sample.memory.*field;
            if (value) json_object_set_int_member(memory, name, *value);
            else json_object_set_null_member(memory, name);
        }
        json_object_set_object_member(resources, "memory", memory);
    } else {
        for (const auto* name : {"timestamp_us", "monitoring_epoch", "shaders", "memory"})
            json_object_set_null_member(resources, name);
    }
    auto* resource_scope = json_object_new();
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, const char*>>{
        {"snapshot", "runtime_process_resources_independent_of_videoout"},
        {"shader_counters", "process_lifetime"},
        {"activity", "process_lifetime_attempts_active_gauges_and_elapsed_wall_time"},
        {"activity_window", "observed_source_interval_approximately_last_1s_reported_duration"},
        {"activity_delta_max_us", "process_lifetime_max_not_window_max"},
        {"activity_elapsed", "nested_overlapping_not_additive_not_cpu_busy_or_gpu_execution"},
        {"stall_coverage", "instrumented_work_only_other_cpu_io_locks_and_gpu_work_unknown"},
        {"shader_gauges", "latest_sample"},
        {"shader_bytes", "retained_cpu_source_and_variant_spirv_excluding_ir_metadata_gpu_memory"},
        {"preparation_queue", "jobs_not_variants"},
        {"pipeline_builds", "vulkan_pipeline_calls_not_backend_shader_compilation"},
        {"pipeline_optimization_queue", "pending_excludes_active"},
        {"cache_memory", "runtime_cache_allocation_accounting_not_physical_vram"},
        {"gpu_heap", "largest_device_local_heap_driver_reported_estimate"},
        {"process_rss", "linux_process_resident_set"}})
        json_object_set_string_member(resource_scope, name, value);
    json_object_set_object_member(resources, "scope", resource_scope);
    auto* resource_units = json_object_new();
    json_object_set_string_member(resource_units, "bytes", "bytes");
    json_object_set_string_member(resource_units, "counts", "count");
    json_object_set_string_member(resource_units, "timestamps", "monotonic_microseconds");
    json_object_set_string_member(resource_units, "age_ms", "milliseconds_since_local_receipt");
    json_object_set_string_member(resource_units, "source_age_ms", "source_timestamp_gap_plus_milliseconds_since_local_receipt");
    json_object_set_object_member(resources, "units", resource_units);
    json_object_set_object_member(object, "resources", resources);
    auto* scope = json_object_new();
    json_object_set_string_member(scope, "metric", "completed_main_videoout_flip_interval");
    json_object_set_string_member(scope, "frame_semantics", "runtime_global_correlation_id_not_simulation_tick");
    json_object_set_string_member(scope, "window", "last_1s_of_source_timestamps");
    json_object_set_int_member(scope, "history_seconds", 60);
    json_object_set_int_member(scope, "history_limit", monitoring_limit);
    json_object_set_string_member(scope, "packet_gaps_semantics", "forward_sequence_gaps_including_reordered_and_nonmeasurement_packets");
    if (snapshot.has_frames) {
        json_object_set_int_member(scope, "output_handle", snapshot.output_handle);
        json_object_set_int_member(scope, "output_generation", snapshot.output_generation);
        json_object_set_int_member(scope, "monitoring_epoch", snapshot.samples.back().monitoring_epoch);
    } else {
        json_object_set_null_member(scope, "output_handle");
        json_object_set_null_member(scope, "output_generation");
        json_object_set_null_member(scope, "monitoring_epoch");
    }
    json_object_set_object_member(object, "scope", scope);
    auto* units = json_object_new();
    for (const auto* name : {"timestamp_us", "flip_interval_us", "mean_interval_us", "p95_interval_us", "worst_interval_us"})
        json_object_set_string_member(units, name, "microseconds");
    json_object_set_string_member(units, "estimated_fps", "flips_per_second");
    json_object_set_string_member(units, "age_ms", "milliseconds_since_local_receipt");
    json_object_set_string_member(units, "coverage", "observed_interval_samples_over_expected_samples");
    json_object_set_object_member(object, "units", units);
    auto* stats = json_object_new();
    const auto& values = snapshot.stats;
    for (const auto& [name, value] : std::initializer_list<std::pair<const char*, std::uint64_t>>{
        {"sample_count", values.sample_count}, {"observed_samples", values.observed_samples}, {"expected_samples", values.expected_samples},
        {"missing_samples", values.missing_samples}, {"sender_dropped_samples", values.sender_dropped_samples},
        {"packet_gaps", values.packet_gaps}, {"window_start_us", values.window_start_us}, {"window_end_us", values.window_end_us}})
        json_object_set_int_member(stats, name, value);
    json_object_set_boolean_member(stats, "complete", values.complete);
    number(stats, "coverage", values.coverage);
    number(stats, "estimated_fps", values.estimated_fps);
    number(stats, "mean_interval_us", values.mean_interval_us);
    number(stats, "p95_interval_us", values.p95_interval_us);
    number(stats, "worst_interval_us", values.worst_interval_us);
    auto* loss_scope = json_object_new();
    json_object_set_string_member(loss_scope, "packet_gaps", "monitoring_epoch");
    json_object_set_string_member(loss_scope, "sender_dropped_samples", "monitoring_epoch");
    json_object_set_string_member(loss_scope, "missing_samples", "source_window");
    json_object_set_object_member(stats, "loss_scope", loss_scope);
    json_object_set_object_member(object, "stats", stats);
    const auto sample_node = [](const MonitoringSample& sample) {
        auto* value = json_object_new();
        for (const auto& [name, integer] : std::initializer_list<std::pair<const char*, std::uint64_t>>{
            {"frame", sample.frame}, {"output_handle", sample.output_handle}, {"output_generation", sample.output_generation},
            {"completed_flip_count", sample.completed_flip_count}, {"timestamp_us", sample.timestamp_us},
            {"monitoring_epoch", sample.monitoring_epoch}, {"dropped_samples", sample.dropped_samples}, {"missing_before", sample.missing_before}})
            json_object_set_int_member(value, name, integer);
        if (sample.flip_interval_us) json_object_set_int_member(value, "flip_interval_us", *sample.flip_interval_us);
        else json_object_set_null_member(value, "flip_interval_us");
        return value;
    };
    const auto count = snapshot.samples.size();
    if (count) json_object_set_object_member(object, "latest_frame", sample_node(snapshot.samples.back()));
    else json_object_set_null_member(object, "latest_frame");
    const auto worst = std::find_if(snapshot.samples.begin(), snapshot.samples.end(), [&](const auto& sample) {
        return sample.timestamp_us > values.window_start_us && sample.flip_interval_us && values.worst_interval_us &&
            static_cast<double>(*sample.flip_interval_us) == *values.worst_interval_us;
    });
    if (worst != snapshot.samples.end()) json_object_set_object_member(object, "worst_frame", sample_node(*worst));
    else json_object_set_null_member(object, "worst_frame");
    if (include_history) {
        auto* frames = json_array_new();
        for (std::size_t index = count > 20 ? count - 20 : 0; index < count; ++index)
            json_array_add_object_element(frames, sample_node(snapshot.samples[index]));
        json_object_set_array_member(object, "frames", frames);
        auto* chart = json_array_new();
        const auto chart_count = std::min<std::size_t>(count, 600);
        for (std::size_t index = 0; index < chart_count; ++index) {
            const auto selected = chart_count < 2 ? 0 : index * (count - 1) / (chart_count - 1);
            json_array_add_object_element(chart, sample_node(snapshot.samples[selected]));
        }
        json_object_set_array_member(object, "chart_samples", chart);
        json_object_set_boolean_member(object, "chart_downsampled", count > chart_count);
    }
    json_object_set_int_member(object, "history_sample_count", count);
    json_object_set_boolean_member(object, "history_included", include_history);
    return serialize(node.get());
}

} // namespace framerift
