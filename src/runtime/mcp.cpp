// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "runtime/mcp.hpp"
#include "runtime/bridge.hpp"
#include "runtime/session.hpp"

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace framerift {
namespace {

constexpr std::size_t max_body = 64 * 1024;
constexpr std::size_t max_requests = 32;
constexpr const char* body_key = "framerift-mcp-body";
constexpr const char* current_version = "2025-11-25";
constexpr const char* older_version = "2025-03-26";

using Node = std::unique_ptr<JsonNode, decltype(&json_node_unref)>;

class InvalidParams : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Node object_node() {
    Node node(json_node_new(JSON_NODE_OBJECT), json_node_unref);
    json_node_take_object(node.get(), json_object_new());
    return node;
}

Node parse_json(std::string_view data) {
    JsonParser* parser = json_parser_new();
    const bool valid = json_parser_load_from_data(parser, data.data(), data.size(), nullptr);
    Node node(valid ? json_node_copy(json_parser_get_root(parser)) : nullptr, json_node_unref);
    g_object_unref(parser);
    return node;
}

std::string serialize(JsonNode* node) {
    gchar* data = json_to_string(node, false);
    std::string result(data);
    g_free(data);
    return result;
}

const char* string_member(JsonObject* object, const char* name) {
    JsonNode* node = json_object_get_member(object, name);
    return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING
        ? json_node_get_string(node) : nullptr;
}

JsonObject* object_member(JsonObject* object, const char* name) {
    JsonNode* node = json_object_get_member(object, name);
    return node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : nullptr;
}

bool only_members(JsonObject* object, std::initializer_list<std::string_view> names) {
    GList* members = json_object_get_members(object);
    bool valid = true;
    for (GList* entry = members; entry; entry = entry->next) {
        const std::string_view member(static_cast<const char*>(entry->data));
        bool found = false;
        for (const auto name : names) found = found || member == name;
        valid = valid && found;
    }
    g_list_free(members);
    return valid;
}

bool integer(JsonNode* node) {
    return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_INT64;
}

bool valid_id(JsonNode* node) {
    return integer(node) || (node && JSON_NODE_HOLDS_VALUE(node) &&
        json_node_get_value_type(node) == G_TYPE_STRING);
}

Node rpc_message(JsonNode* id) {
    Node node = object_node();
    JsonObject* object = json_node_get_object(node.get());
    json_object_set_string_member(object, "jsonrpc", "2.0");
    if (id) json_object_set_member(object, "id", json_node_copy(id));
    else json_object_set_null_member(object, "id");
    return node;
}

void send_json(SoupServerMessage* message, unsigned status, JsonNode* node) {
    const auto data = serialize(node);
    soup_server_message_set_status(message, status, nullptr);
    soup_server_message_set_response(message, "application/json", SOUP_MEMORY_COPY,
        data.data(), data.size());
    soup_message_headers_replace(soup_server_message_get_response_headers(message),
        "Cache-Control", "no-store");
}

void rpc_error(SoupServerMessage* message, JsonNode* id, int code, const char* text,
    unsigned status = SOUP_STATUS_OK) {
    Node node = rpc_message(id);
    JsonObject* error = json_object_new();
    json_object_set_int_member(error, "code", code);
    json_object_set_string_member(error, "message", text);
    json_object_set_object_member(json_node_get_object(node.get()), "error", error);
    send_json(message, status, node.get());
}

void rpc_result(SoupServerMessage* message, JsonNode* id, JsonNode* result) {
    Node node = rpc_message(id);
    json_object_set_member(json_node_get_object(node.get()), "result", json_node_copy(result));
    send_json(message, SOUP_STATUS_OK, node.get());
}

void http_error(SoupServerMessage* message, unsigned status, const char* text) {
    soup_server_message_set_status(message, status, nullptr);
    soup_server_message_set_response(message, "text/plain; charset=utf-8", SOUP_MEMORY_COPY,
        text, std::strlen(text));
}

bool supported_version(const char* version) {
    return !std::strcmp(version, current_version) || !std::strcmp(version, older_version);
}

bool accepts(SoupMessageHeaders* headers, const char* media_type) {
    const char* accept = soup_message_headers_get_list(headers, "Accept");
    if (!accept) return false;
    GSList* values = soup_header_parse_quality_list(accept, nullptr);
    bool found = false;
    for (GSList* entry = values; entry; entry = entry->next)
        found = found || !g_ascii_strcasecmp(static_cast<const char*>(entry->data), media_type);
    soup_header_free_list(values);
    return found;
}

bool local_host(const char* host) {
    return host && (!g_ascii_strcasecmp(host, "localhost") || !std::strcmp(host, "127.0.0.1"));
}

void close_socket(SoupServerMessage* message) {
    if (GSocket* socket = soup_server_message_get_socket(message))
        g_socket_close(socket, nullptr);
}

struct RequestBody {
    SoupServerMessage* message;
    std::string data;
    guint deadline = 0;

    explicit RequestBody(SoupServerMessage* value) : message(value) {}
    ~RequestBody() { if (deadline) g_source_remove(deadline); }
};

void receive_chunk(SoupServerMessage* message, GBytes* chunk, gpointer) noexcept {
    try {
        auto* body = static_cast<RequestBody*>(g_object_get_data(G_OBJECT(message), body_key));
        if (!body || soup_server_message_get_status(message)) return;
        gsize size = 0;
        const char* data = static_cast<const char*>(g_bytes_get_data(chunk, &size));
        if (size > max_body - body->data.size()) {
            http_error(message, SOUP_STATUS_REQUEST_ENTITY_TOO_LARGE, "MCP request exceeds 64 KiB.");
            return;
        }
        body->data.append(data, size);
    } catch (...) {
        close_socket(message);
    }
}

constexpr const char* tool_list = R"json({"tools":[
 {"name":"get_bridge_status","description":"Get the UDP bridge connection, current session and advertised runtime capabilities.","inputSchema":{"type":"object","additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}},
 {"name":"get_monitoring_snapshot","description":"Read main VideoOut flip timings, confirmed monitoring state, sample coverage and latest/worst frames, plus an independently refreshed shader and memory resource snapshot. Statistics use the last source-time second; FPS is estimated from observed intervals. Resource ages are independent of frames and memory source ages remain explicit; unavailable values are null. Work activity includes lifetime counters and elapsed wall time, an observed delta window, and stall evidence; durations overlap and are not GPU execution time. Set include_history for a bounded chart and recent frames; stale data is retained.","inputSchema":{"type":"object","properties":{"include_history":{"type":"boolean","default":false}},"additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}},
 {"name":"get_events","description":"Read buffered runtime events after a FrameRift event cursor. Returns immediately; does not wait for new events.","inputSchema":{"type":"object","properties":{"after":{"type":"integer","minimum":0,"maximum":9223372036854775807},"limit":{"type":"integer","minimum":1,"maximum":100}},"additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}},
 {"name":"send_command","description":"Send an asynchronous command supported by the connected runtime. Returns a command ID and local send status; delivery and execution require later runtime events. Put command-specific parameters in args.","inputSchema":{"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":128},"args":{"type":"object"}},"required":["name","args"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":true,"idempotentHint":false,"openWorldHint":false}},
 {"name":"get_command","description":"Get the latest received status of a previously sent command by its command ID.","inputSchema":{"type":"object","properties":{"command_id":{"type":"string","minLength":1,"maxLength":128}},"required":["command_id"],"additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}}
]})json";

constexpr const char* game_tool_list = R"json({"tools":[
 {"name":"list_games","description":"List scanned library games with stable game IDs, metadata titles and availability for launch.","inputSchema":{"type":"object","additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}},
 {"name":"get_game_status","description":"Get the current managed game session, preparation/process state and any asynchronous failure. Returns immediately.","inputSchema":{"type":"object","additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}},
 {"name":"start_game","description":"Start a scanned game by its stable game ID. Preparation and launch run asynchronously; poll get_game_status for readiness or failure. A running process does not imply a UDP runtime connection.","inputSchema":{"type":"object","properties":{"game_id":{"type":"string","minLength":1,"maxLength":128}},"required":["game_id"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false,"idempotentHint":false,"openWorldHint":false}},
 {"name":"stop_game","description":"Stop the managed game session matching the supplied stable game ID. Poll get_game_status until the process exits.","inputSchema":{"type":"object","properties":{"game_id":{"type":"string","minLength":1,"maxLength":128}},"required":["game_id"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":true,"idempotentHint":true,"openWorldHint":false}},
 {"name":"get_game_log","description":"Read the last lines of the current or latest managed session log, from game output or conversion. The result identifies the source. Returns five lines by default; never waits for new output.","inputSchema":{"type":"object","properties":{"lines":{"type":"integer","minimum":1,"maximum":200,"default":5}},"additionalProperties":false},"annotations":{"readOnlyHint":true,"openWorldHint":false}}
]})json";

} // namespace

struct McpServer::Impl {
    BridgeServer& bridge;
    GameSession* games;
    SoupServer* server = nullptr;
    std::uint16_t port = 0;
    std::unordered_set<SoupServerMessage*> requests;

    Impl(BridgeServer& value, std::uint16_t requested_port, GameSession* sessions)
        : bridge(value), games(sessions) {
        server = soup_server_new("server-header", "FrameRift", nullptr);
        g_signal_connect(server, "request-started", G_CALLBACK(request_started), this);
        g_signal_connect(server, "request-finished", G_CALLBACK(request_finished), this);
        g_signal_connect(server, "request-aborted", G_CALLBACK(request_finished), this);
        soup_server_add_early_handler(server, "/", early_handler, this, nullptr);
        soup_server_add_handler(server, "/", handler, this, nullptr);
        GError* error = nullptr;
        if (!soup_server_listen_local(server, requested_port, SOUP_SERVER_LISTEN_IPV4_ONLY, &error)) {
            const std::string reason(error ? error->message : "Could not listen on localhost.");
            g_clear_error(&error);
            g_object_unref(server);
            server = nullptr;
            throw std::runtime_error("MCP server: " + reason);
        }
        GSList* addresses = soup_server_get_uris(server);
        if (addresses) port = static_cast<std::uint16_t>(g_uri_get_port(static_cast<GUri*>(addresses->data)));
        g_slist_free_full(addresses, reinterpret_cast<GDestroyNotify>(g_uri_unref));
    }

    ~Impl() {
        if (server) {
            soup_server_disconnect(server);
            g_signal_handlers_disconnect_by_data(server, this);
            soup_server_remove_handler(server, "/");
            for (auto* message : requests) {
                g_object_set_data(G_OBJECT(message), body_key, nullptr);
                close_socket(message);
            }
            g_object_unref(server);
        }
    }

    static void request_started(SoupServer*, SoupServerMessage* message, gpointer data) noexcept {
        auto& self = *static_cast<Impl*>(data);
        try {
            soup_message_body_set_accumulate(soup_server_message_get_request_body(message), false);
            soup_message_headers_replace(soup_server_message_get_response_headers(message), "Connection", "close");
            if (self.requests.size() >= max_requests) {
                close_socket(message);
                return;
            }
            self.requests.insert(message);
            auto body = std::make_unique<RequestBody>(message);
            body->deadline = g_timeout_add_seconds(10, [](gpointer value) -> gboolean {
                auto* request = static_cast<RequestBody*>(value);
                request->deadline = 0;
                close_socket(request->message);
                return G_SOURCE_REMOVE;
            }, body.get());
            g_object_set_data_full(G_OBJECT(message), body_key, body.release(), [](gpointer value) {
                delete static_cast<RequestBody*>(value);
            });
            g_signal_connect(message, "got-chunk", G_CALLBACK(receive_chunk), nullptr);
        } catch (...) {
            close_socket(message);
        }
    }

    static void request_finished(SoupServer*, SoupServerMessage* message, gpointer data) noexcept {
        auto& self = *static_cast<Impl*>(data);
        self.requests.erase(message);
        g_object_set_data(G_OBJECT(message), body_key, nullptr);
    }

    bool validate_http(SoupServerMessage* message, const char* path) {
        auto* headers = soup_server_message_get_request_headers(message);
        const char* host = soup_message_headers_get_one(headers, "Host");
        const std::string expected_port = ":" + std::to_string(port);
        if (!host || (std::string_view(host) != "127.0.0.1" + expected_port &&
            std::string_view(host) != "localhost" + expected_port)) {
            http_error(message, SOUP_STATUS_FORBIDDEN, "MCP Host must name this localhost endpoint.");
            return false;
        }
        if (const char* origin = soup_message_headers_get_one(headers, "Origin")) {
            GUri* uri = g_uri_parse(origin, G_URI_FLAGS_NONE, nullptr);
            const bool valid = uri && local_host(g_uri_get_host(uri)) &&
                g_uri_get_scheme(uri) && (!g_ascii_strcasecmp(g_uri_get_scheme(uri), "http") ||
                !g_ascii_strcasecmp(g_uri_get_scheme(uri), "https")) &&
                !g_uri_get_userinfo(uri) && !g_uri_get_query(uri) && !g_uri_get_fragment(uri) &&
                (!g_uri_get_path(uri) || !*g_uri_get_path(uri));
            if (uri) g_uri_unref(uri);
            if (!valid) {
                http_error(message, SOUP_STATUS_FORBIDDEN, "MCP Origin must be local.");
                return false;
            }
        }
        if (const char* version = soup_message_headers_get_one(headers, "MCP-Protocol-Version")) {
            if (!supported_version(version)) {
                http_error(message, SOUP_STATUS_BAD_REQUEST, "Unsupported MCP protocol version.");
                return false;
            }
        }
        if (std::strcmp(path, "/mcp")) {
            http_error(message, SOUP_STATUS_NOT_FOUND, "MCP endpoint is /mcp.");
            return false;
        }
        if (std::strcmp(soup_server_message_get_method(message), SOUP_METHOD_POST)) {
            soup_message_headers_replace(soup_server_message_get_response_headers(message), "Allow", "POST");
            http_error(message, SOUP_STATUS_METHOD_NOT_ALLOWED, "This MCP endpoint accepts POST; SSE is not offered.");
            return false;
        }
        const char* content_type = soup_message_headers_get_content_type(headers, nullptr);
        if (!content_type || g_ascii_strcasecmp(content_type, "application/json")) {
            http_error(message, SOUP_STATUS_UNSUPPORTED_MEDIA_TYPE, "MCP requires application/json.");
            return false;
        }
        if (!accepts(headers, "application/json") || !accepts(headers, "text/event-stream")) {
            http_error(message, SOUP_STATUS_NOT_ACCEPTABLE, "MCP Accept must include application/json and text/event-stream.");
            return false;
        }
        if (soup_message_headers_get_content_length(headers) > static_cast<goffset>(max_body)) {
            http_error(message, SOUP_STATUS_REQUEST_ENTITY_TOO_LARGE, "MCP request exceeds 64 KiB.");
            return false;
        }
        return true;
    }

    static void early_handler(SoupServer*, SoupServerMessage* message, const char* path,
        GHashTable*, gpointer data) noexcept {
        try {
            static_cast<Impl*>(data)->validate_http(message, path);
        } catch (...) {
            http_error(message, SOUP_STATUS_INTERNAL_SERVER_ERROR, "MCP request could not be handled.");
        }
    }

    static void handler(SoupServer*, SoupServerMessage* message, const char*,
        GHashTable*, gpointer data) noexcept {
        try {
            static_cast<Impl*>(data)->handle(message);
        } catch (...) {
            http_error(message, SOUP_STATUS_INTERNAL_SERVER_ERROR, "MCP request could not be handled.");
        }
    }

    void tool_result(SoupServerMessage* message, JsonNode* id, const std::string& data, bool failed) {
        Node value = parse_json(data);
        if (!value || !JSON_NODE_HOLDS_OBJECT(value.get()))
            throw std::runtime_error("Tool returned invalid JSON.");
        Node result = object_node();
        JsonObject* object = json_node_get_object(result.get());
        JsonArray* content = json_array_new();
        JsonObject* text = json_object_new();
        json_object_set_string_member(text, "type", "text");
        json_object_set_string_member(text, "text", data.c_str());
        json_array_add_object_element(content, text);
        json_object_set_array_member(object, "content", content);
        json_object_set_member(object, "structuredContent", json_node_copy(value.get()));
        json_object_set_boolean_member(object, "isError", failed);
        rpc_result(message, id, result.get());
    }

    void call_tool(SoupServerMessage* message, JsonNode* id, JsonObject* params) {
        const char* name = params ? string_member(params, "name") : nullptr;
        JsonObject* arguments = params ? object_member(params, "arguments") : nullptr;
        Node empty = object_node();
        if (!arguments && params && !json_object_has_member(params, "arguments"))
            arguments = json_node_get_object(empty.get());
        if (!name || !arguments) {
            rpc_error(message, id, -32602, "Expected tool name and object arguments.");
            return;
        }
        std::string result;
        try {
            if (!std::strcmp(name, "get_bridge_status")) {
                if (!only_members(arguments, {})) throw InvalidParams("get_bridge_status takes no arguments.");
                result = bridge.status_json();
            } else if (!std::strcmp(name, "get_monitoring_snapshot")) {
                if (!only_members(arguments, {"include_history"})) throw InvalidParams("Unknown monitoring argument.");
                bool include_history = false;
                if (auto* node = json_object_get_member(arguments, "include_history")) {
                    if (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_BOOLEAN)
                        throw InvalidParams("include_history must be a boolean.");
                    include_history = json_node_get_boolean(node);
                }
                result = bridge.monitoring_snapshot_json(include_history);
            } else if (!std::strcmp(name, "get_events")) {
                if (!only_members(arguments, {"after", "limit"})) throw InvalidParams("Unknown event argument.");
                std::uint64_t after = 0;
                std::size_t limit = 100;
                if (JsonNode* node = json_object_get_member(arguments, "after")) {
                    if (!integer(node) || json_node_get_int(node) < 0) throw InvalidParams("after must be a nonnegative integer.");
                    after = static_cast<std::uint64_t>(json_node_get_int(node));
                }
                if (JsonNode* node = json_object_get_member(arguments, "limit")) {
                    if (!integer(node) || json_node_get_int(node) < 1 || json_node_get_int(node) > 100)
                        throw InvalidParams("limit must be an integer between 1 and 100.");
                    limit = static_cast<std::size_t>(json_node_get_int(node));
                }
                result = bridge.events_json(after, limit);
            } else if (!std::strcmp(name, "send_command")) {
                const char* command = string_member(arguments, "name");
                JsonObject* values = object_member(arguments, "args");
                if (!only_members(arguments, {"name", "args"}) || !command || !*command ||
                    std::strlen(command) > 128 || !values)
                    throw InvalidParams("Expected command name and object arguments.");
                result = bridge.send_command(command, serialize(json_object_get_member(arguments, "args")));
            } else if (!std::strcmp(name, "get_command")) {
                const char* command = string_member(arguments, "command_id");
                if (!only_members(arguments, {"command_id"}) || !command || !*command || std::strlen(command) > 128)
                    throw InvalidParams("Expected a command_id string of 1 to 128 bytes.");
                result = bridge.command_json(command);
            } else if (games && !std::strcmp(name, "list_games")) {
                if (!only_members(arguments, {})) throw InvalidParams("list_games takes no arguments.");
                result = games->games_json();
            } else if (games && !std::strcmp(name, "get_game_status")) {
                if (!only_members(arguments, {})) throw InvalidParams("get_game_status takes no arguments.");
                result = games->status_json();
            } else if (games && (!std::strcmp(name, "start_game") || !std::strcmp(name, "stop_game"))) {
                const char* game_id = string_member(arguments, "game_id");
                if (!only_members(arguments, {"game_id"}) || !game_id || !*game_id || std::strlen(game_id) > 128)
                    throw InvalidParams("Expected a game_id string of 1 to 128 bytes.");
                if (!std::strcmp(name, "start_game")) games->start(game_id);
                else games->stop(game_id);
                result = games->status_json();
            } else if (games && !std::strcmp(name, "get_game_log")) {
                if (!only_members(arguments, {"lines"})) throw InvalidParams("Unknown log argument.");
                std::size_t lines = 5;
                if (JsonNode* node = json_object_get_member(arguments, "lines")) {
                    if (!integer(node) || json_node_get_int(node) < 1 || json_node_get_int(node) > 200)
                        throw InvalidParams("lines must be an integer between 1 and 200.");
                    lines = static_cast<std::size_t>(json_node_get_int(node));
                }
                result = games->log_json(lines);
            } else {
                rpc_error(message, id, -32602, "Unknown tool.");
                return;
            }
        } catch (const InvalidParams& error) {
            rpc_error(message, id, -32602, error.what());
            return;
        } catch (const std::exception& error) {
            Node failure = object_node();
            json_object_set_string_member(json_node_get_object(failure.get()), "error", error.what());
            tool_result(message, id, serialize(failure.get()), true);
            return;
        }
        tool_result(message, id, result, false);
    }

    void handle(SoupServerMessage* message) {
        auto* body = static_cast<RequestBody*>(g_object_get_data(G_OBJECT(message), body_key));
        Node node = parse_json(body ? std::string_view(body->data) : std::string_view{});
        if (!node) {
            rpc_error(message, nullptr, -32700, "Parse error.", SOUP_STATUS_BAD_REQUEST);
            return;
        }
        if (!JSON_NODE_HOLDS_OBJECT(node.get())) {
            rpc_error(message, nullptr, -32600, "Expected a single JSON-RPC object.", SOUP_STATUS_BAD_REQUEST);
            return;
        }
        JsonObject* object = json_node_get_object(node.get());
        const char* version = string_member(object, "jsonrpc");
        const char* method = string_member(object, "method");
        JsonNode* id = json_object_get_member(object, "id");
        if (!version || std::strcmp(version, "2.0") || (id && !valid_id(id))) {
            rpc_error(message, nullptr, -32600, "Invalid JSON-RPC request.", SOUP_STATUS_BAD_REQUEST);
            return;
        }
        if (!method) {
            const bool result = json_object_has_member(object, "result");
            const bool error = json_object_has_member(object, "error");
            if (!json_object_has_member(object, "method") && id && result != error &&
                (!error || object_member(object, "error")))
                soup_server_message_set_status(message, SOUP_STATUS_ACCEPTED, nullptr);
            else rpc_error(message, nullptr, -32600, "Invalid JSON-RPC message.", SOUP_STATUS_BAD_REQUEST);
            return;
        }
        if (json_object_has_member(object, "result") || json_object_has_member(object, "error")) {
            rpc_error(message, nullptr, -32600, "Invalid JSON-RPC request.", SOUP_STATUS_BAD_REQUEST);
            return;
        }
        JsonObject* params = object_member(object, "params");
        if (json_object_has_member(object, "params") && !params) {
            rpc_error(message, id, -32602, "params must be an object.", id ? SOUP_STATUS_OK : SOUP_STATUS_BAD_REQUEST);
            return;
        }
        if (!id) {
            // No server-to-client requests are made, so notifications need no session state.
            soup_server_message_set_status(message, SOUP_STATUS_ACCEPTED, nullptr);
            return;
        }
        if (!std::strcmp(method, "initialize")) {
            const char* requested = params ? string_member(params, "protocolVersion") : nullptr;
            JsonObject* client = params ? object_member(params, "clientInfo") : nullptr;
            if (!requested || !object_member(params, "capabilities") || !client ||
                !string_member(client, "name") || !string_member(client, "version")) {
                rpc_error(message, id, -32602, "initialize requires protocolVersion, capabilities and clientInfo.");
                return;
            }
            Node result = object_node();
            JsonObject* value = json_node_get_object(result.get());
            json_object_set_string_member(value, "protocolVersion", supported_version(requested) ? requested : current_version);
            JsonObject* capabilities = json_object_new();
            json_object_set_object_member(capabilities, "tools", json_object_new());
            json_object_set_object_member(value, "capabilities", capabilities);
            JsonObject* server_info = json_object_new();
            json_object_set_string_member(server_info, "name", "framerift");
            json_object_set_string_member(server_info, "version", "0.1.0");
            json_object_set_object_member(value, "serverInfo", server_info);
            if (games) json_object_set_string_member(value, "instructions",
                "Use list_games to obtain stable game IDs. start_game and stop_game are asynchronous; "
                "use get_game_status to observe completion or failure. get_game_log returns the last "
                "five log lines by default. UDP runtime connectivity is reported separately by "
                "get_bridge_status; telemetry and runtime commands require the game's bridge adapter.");
            rpc_result(message, id, result.get());
        } else if (!std::strcmp(method, "ping")) {
            Node result = object_node();
            rpc_result(message, id, result.get());
        } else if (!std::strcmp(method, "tools/list")) {
            if (params && json_object_has_member(params, "cursor")) {
                rpc_error(message, id, -32602, "No pagination cursor is supported.");
                return;
            }
            Node result = parse_json(tool_list);
            if (games) {
                Node additions = parse_json(game_tool_list);
                JsonArray* listed = json_object_get_array_member(json_node_get_object(result.get()), "tools");
                JsonArray* extra = json_object_get_array_member(json_node_get_object(additions.get()), "tools");
                for (guint i = 0; i < json_array_get_length(extra); ++i)
                    json_array_add_element(listed, json_node_copy(json_array_get_element(extra, i)));
            }
            rpc_result(message, id, result.get());
        } else if (!std::strcmp(method, "tools/call")) call_tool(message, id, params);
        else rpc_error(message, id, -32601, "Method not found.");
    }
};

McpServer::McpServer(BridgeServer& bridge, std::uint16_t port, GameSession* games)
    : impl_(std::make_unique<Impl>(bridge, port, games)) {}

McpServer::~McpServer() = default;

std::uint16_t McpServer::port() const { return impl_->port; }

std::string McpServer::endpoint() const {
    return "http://127.0.0.1:" + std::to_string(port()) + "/mcp";
}

} // namespace framerift
