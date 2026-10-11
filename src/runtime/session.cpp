// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "session.hpp"

#include <json-glib/json-glib.h>

#include <algorithm>
#include <fstream>
#include <utility>
#include <vector>
#ifdef G_OS_UNIX
#include <signal.h>
#endif

namespace framerift {
namespace {

struct NodeUnref { void operator()(JsonNode* node) const { json_node_unref(node); } };
using Node = std::unique_ptr<JsonNode, NodeUnref>;

Node object_node() {
    Node node(json_node_new(JSON_NODE_OBJECT));
    json_node_take_object(node.get(), json_object_new());
    return node;
}

std::string encoded(JsonNode* node) {
    const std::unique_ptr<JsonGenerator, decltype(&g_object_unref)> generator(json_generator_new(), g_object_unref);
    json_generator_set_root(generator.get(), node);
    const std::unique_ptr<gchar, decltype(&g_free)> data(json_generator_to_data(generator.get(), nullptr), g_free);
    return data.get();
}

bool available(const Game& game) {
#ifndef __linux__
    (void)game;
    return false;
#else
    if (game.is_demo || game.installation_path.empty()) return false;
    std::error_code error;
    const auto executable = std::filesystem::path(game.installation_path) / "eboot.bin";
    return std::filesystem::is_regular_file(executable, error) && std::ifstream(executable, std::ios::binary).good();
#endif
}

Node status_node(const GameSessionStatus& state) {
    auto node = object_node();
    auto* object = json_node_get_object(node.get());
    json_object_set_string_member(object, "state", state.state.c_str());
    json_object_set_boolean_member(object, "busy", state.busy());
    json_object_set_string_member(object, "game_id", state.game_id.c_str());
    json_object_set_string_member(object, "title", state.title.c_str());
    json_object_set_string_member(object, "session_id", state.session_id.c_str());
    json_object_set_boolean_member(object, "rebuilding", state.rebuilding);
    json_object_set_string_member(object, "error", state.error.c_str());
    json_object_set_int_member(object, "exit_code", state.exit_code);
    json_object_set_int_member(object, "term_signal", state.term_signal);
    json_object_set_string_member(object, "log_path", state.prepared.log_path.string().c_str());
    json_object_set_string_member(object, "conversion_log_path", state.prepared.conversion_log_path.string().c_str());
    return node;
}

std::string valid_log_text(const std::string& bytes) {
    // Preserve raw bytes on disk while keeping the MCP result valid UTF-8 JSON.
    std::string printable;
    for (const char byte : bytes) {
        if (byte == '\0') printable += "\\0";
        else printable += byte;
    }
    const std::unique_ptr<gchar, decltype(&g_free)> valid(
        g_utf8_make_valid(printable.data(), printable.size()), g_free);
    return valid.get();
}

} // namespace

bool GameSessionStatus::busy() const {
    return state == "preparing" || state == "running" || state == "stopping";
}

struct GameSession::Impl : std::enable_shared_from_this<GameSession::Impl> {
    struct Request {
        Game game;
        LaunchSettings settings;
        std::filesystem::path cache;
        std::filesystem::path data;
        bool rebuild;
    };
    struct Preparation {
        PreparedGame prepared;
        std::string error;
    };

    Library& library;
    std::filesystem::path cache;
    std::filesystem::path data;
    GApplication* application;
    BridgeServer* bridge;
    std::function<void(const GameSessionStatus&)> changed;
    GameOutput output;
    GameSessionStatus state;
    GSubprocess* process = nullptr;
    GCancellable* cancellation = nullptr;
    bool held = false;
    bool alive = true;

    Impl(Library& library, std::filesystem::path cache, std::filesystem::path data,
        GApplication* application, BridgeServer* bridge,
        std::function<void(const GameSessionStatus&)> changed, GameOutput output)
        : library(library), cache(std::move(cache)), data(std::move(data)),
          application(application ? G_APPLICATION(g_object_ref(application)) : nullptr), bridge(bridge),
          changed(std::move(changed)), output(std::move(output)) {}

    ~Impl() {
        g_clear_object(&process);
        g_clear_object(&cancellation);
        if (held && application) g_application_release(application);
        g_clear_object(&application);
    }

    void notify() noexcept {
        try { if (alive && changed) changed(state); }
        catch (...) { g_warning("Could not display game session state"); }
    }

    void release() noexcept {
        g_clear_object(&cancellation);
        if (held && application) g_application_release(application);
        held = false;
    }

    void close_bridge() noexcept {
        try { if (bridge && !state.session_id.empty()) bridge->end_session(state.session_id); }
        catch (...) { g_warning("Could not close the game bridge session"); }
    }

    void finished(GSubprocess* child, const std::string& error) noexcept {
        try {
            const bool stopping = state.state == "stopping";
            state.exit_code = g_subprocess_get_if_exited(child) ? g_subprocess_get_exit_status(child) : -1;
            state.term_signal = g_subprocess_get_if_signaled(child) ? g_subprocess_get_term_sig(child) : 0;
            state.error = error;
            if (!stopping && state.error.empty()) {
                if (state.term_signal)
                    state.error = "Game terminated by signal " + std::to_string(state.term_signal);
                else if (state.exit_code != 0)
                    state.error = "Game exited with status " + std::to_string(state.exit_code);
            }
            state.state = stopping ? "stopped" : (state.error.empty() ? "exited" : "failed");
            close_bridge();
            g_clear_object(&process);
            release();
            notify();
        } catch (...) {
            close_bridge();
            g_clear_object(&process);
            release();
            g_warning("Could not finalize game session state");
        }
    }

    void prepared(std::unique_ptr<Preparation> result) noexcept {
        try {
            state.prepared = std::move(result->prepared);
            if (!state.prepared.log_path.empty())
                state.session_id = state.prepared.log_path.parent_path().filename().string();
            else if (!state.prepared.conversion_log_path.empty())
                state.session_id = state.prepared.conversion_log_path.parent_path().filename().string();
            if (!alive || state.state == "stopping") {
                state.state = "stopped";
                release();
                notify();
                return;
            }
            if (!result->error.empty()) throw std::runtime_error(result->error);
            if (state.rebuilding) {
                state.state = "rebuilt";
                release();
                notify();
                return;
            }
            BridgeLaunch configuration;
            if (bridge) {
                bridge->expect_session(state.session_id, state.game_id, true);
                configuration = {bridge->port(), state.session_id};
            }
            const auto owner = shared_from_this();
            launch_game(state.prepared, [owner](std::string_view bytes) {
                if (owner->alive && owner->output) owner->output(bytes);
            }, [owner](GSubprocess* child, const std::string& error) {
                owner->finished(child, error);
            }, configuration, &process);
            state.state = "running";
            try { library.mark_played(state.game_id); }
            catch (const std::exception& error) { state.error = error.what(); }
            notify();
        } catch (const std::exception& error) {
            state.error = error.what();
            state.state = "failed";
            close_bridge();
            release();
            notify();
        } catch (...) {
            state.state = "failed";
            close_bridge();
            release();
            g_warning("Could not prepare the game session");
            notify();
        }
    }

    void request_stop() {
        if (state.state == "stopping") return;
        if (process) {
#ifdef G_OS_UNIX
            g_subprocess_send_signal(process, SIGTERM);
#else
            throw std::runtime_error("Managed game stopping is supported on Linux only");
#endif
        } else if (cancellation) {
            g_cancellable_cancel(cancellation);
        }
        state.state = "stopping";
        notify();
    }
};

GameSession::GameSession(Library& library, std::filesystem::path cache_root, std::filesystem::path data_root,
    GApplication* application, BridgeServer* bridge,
    std::function<void(const GameSessionStatus&)> changed, GameOutput output)
    : impl_(std::make_shared<Impl>(library, std::move(cache_root), std::move(data_root), application,
          bridge, std::move(changed), std::move(output))) {}

GameSession::~GameSession() {
    impl_->alive = false;
    impl_->changed = nullptr;
    impl_->output = nullptr;
    impl_->close_bridge();
    impl_->bridge = nullptr;
    try { if (impl_->state.busy()) impl_->request_stop(); }
    catch (...) { g_warning("Could not stop the managed game during shutdown"); }
}

void GameSession::start(const std::string& game_id, bool rebuild) {
    if (impl_->state.busy()) throw std::runtime_error("Another game session is already active");
    const auto games = impl_->library.visible_games();
    const auto game = std::find_if(games.begin(), games.end(), [&](const Game& entry) { return entry.id == game_id; });
    if (game == games.end()) throw std::runtime_error("Unknown game ID in the configured library");
    if (!available(*game)) throw std::runtime_error("The selected game has no available local installation");
    const auto settings = impl_->library.launch_settings();
    if (settings.anyps5_directory.empty())
        throw std::runtime_error("Configure the AnyPS5 checkout before starting a game");
    auto request = std::make_unique<Impl::Request>(Impl::Request{*game, settings, impl_->cache, impl_->data, rebuild});
    impl_->state = {};
    impl_->state.state = "preparing";
    impl_->state.game_id = game->id;
    impl_->state.title = game->title;
    impl_->state.rebuilding = rebuild;
    impl_->cancellation = g_cancellable_new();
    if (impl_->application) {
        g_application_hold(impl_->application);
        impl_->held = true;
    }
    auto callback_owner = std::make_unique<std::shared_ptr<Impl>>(impl_);
    auto* task = g_task_new(nullptr, impl_->cancellation, +[](GObject*, GAsyncResult* result, gpointer data) {
        const std::unique_ptr<std::shared_ptr<Impl>> owner(static_cast<std::shared_ptr<Impl>*>(data));
        GError* error = nullptr;
        std::unique_ptr<Impl::Preparation> preparation(static_cast<Impl::Preparation*>(g_task_propagate_pointer(G_TASK(result), &error)));
        if (!preparation) {
            try {
                preparation = std::make_unique<Impl::Preparation>();
                preparation->error = error ? error->message : "Game preparation failed";
            } catch (...) {
                g_clear_error(&error);
                (*owner)->release();
                return;
            }
        }
        g_clear_error(&error);
        (*owner)->prepared(std::move(preparation));
    }, callback_owner.release());
    g_task_set_check_cancellable(task, FALSE);
    g_task_set_task_data(task, request.release(), +[](gpointer data) { delete static_cast<Impl::Request*>(data); });
    impl_->notify();
    g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable* cancellation) {
        auto& request = *static_cast<Impl::Request*>(data);
        try {
            auto result = std::make_unique<Impl::Preparation>();
            try {
                result->prepared = prepare_game(request.game, request.settings, request.cache, request.data,
                    request.rebuild, cancellation);
            } catch (const ConversionError& error) {
                result->error = error.what();
                result->prepared.game_id = request.game.id;
                result->prepared.conversion_log_path = error.log_path;
            } catch (const std::exception& error) {
                result->error = error.what();
            }
            g_task_return_pointer(task, result.release(), +[](gpointer data) { delete static_cast<Impl::Preparation*>(data); });
        } catch (...) {
            g_task_return_new_error(task, g_quark_from_static_string("framerift-session"), 1, "Game preparation failed");
        }
    });
    g_object_unref(task);
}

void GameSession::stop(const std::string& game_id) {
    if (!impl_->state.busy()) throw std::runtime_error("No managed game session is active");
    if (game_id != impl_->state.game_id) throw std::runtime_error("The game ID does not match the managed session");
    impl_->request_stop();
}

void GameSession::set_bridge(BridgeServer* bridge) { impl_->bridge = bridge; }

const GameSessionStatus& GameSession::status() const { return impl_->state; }

std::string GameSession::status_json() const {
    auto node = status_node(impl_->state);
    return encoded(node.get());
}

std::string GameSession::games_json() const {
    auto node = object_node();
    auto* object = json_node_get_object(node.get());
    auto* games = json_array_new();
    json_object_set_array_member(object, "games", games);
    for (const auto& game : impl_->library.visible_games()) {
        auto entry = object_node();
        auto* value = json_node_get_object(entry.get());
        json_object_set_string_member(value, "game_id", game.id.c_str());
        json_object_set_string_member(value, "title", game.title.c_str());
        json_object_set_boolean_member(value, "available", available(game));
        json_object_set_boolean_member(value, "is_demo", game.is_demo);
        json_object_set_string_member(value, "state", game.id == impl_->state.game_id ? impl_->state.state.c_str() : "idle");
        json_array_add_element(games, entry.release());
    }
    auto state = status_node(impl_->state);
    json_object_set_member(object, "session", state.release());
    return encoded(node.get());
}

std::string GameSession::log_json(std::size_t lines) const {
    if (!lines || lines > 200) throw std::runtime_error("Log lines must be between 1 and 200");
    const auto& state = impl_->state;
    auto path = state.prepared.log_path;
    std::string source = "game";
    std::error_code file_error;
    if (state.rebuilding || (state.state == "failed" && !std::filesystem::is_regular_file(path, file_error))
        || path.empty()) {
        path = state.prepared.conversion_log_path;
        source = "conversion";
    }
    auto node = object_node();
    auto* object = json_node_get_object(node.get());
    json_object_set_string_member(object, "game_id", state.game_id.c_str());
    json_object_set_string_member(object, "path", path.string().c_str());
    json_object_set_string_member(object, "source", source.c_str());
    auto* values = json_array_new();
    json_object_set_array_member(object, "lines", values);
    bool truncated = false;
    bool readable = false;
    std::string error;
    if (path.empty()) {
        error = "No log is available for the managed session";
    } else {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() < 0) {
            error = "The managed session log is not available yet";
        } else {
            constexpr std::streamoff byte_limit = 256 * 1024;
            const auto size = static_cast<std::streamoff>(input.tellg());
            const auto count = std::min(size, byte_limit);
            input.seekg(size - count);
            std::string tail(static_cast<std::size_t>(count), '\0');
            input.read(tail.data(), count);
            tail.resize(static_cast<std::size_t>(input.gcount()));
            readable = true;
            truncated = size > count;
            if (truncated) {
                const auto newline = tail.find('\n');
                if (newline != std::string::npos && newline + 1 < tail.size()) tail.erase(0, newline + 1);
            }
            std::vector<std::string> entries;
            for (std::size_t begin = 0; begin < tail.size();) {
                const auto end = tail.find('\n', begin);
                auto line = tail.substr(begin, end == std::string::npos ? end : end - begin);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                entries.push_back(valid_log_text(line));
                if (end == std::string::npos) break;
                begin = end + 1;
            }
            truncated = truncated || entries.size() > lines;
            const auto first = entries.size() > lines ? entries.size() - lines : 0;
            for (std::size_t i = first; i < entries.size(); ++i)
                json_array_add_string_element(values, entries[i].c_str());
        }
    }
    json_object_set_boolean_member(object, "truncated", truncated);
    json_object_set_boolean_member(object, "available", readable);
    json_object_set_string_member(object, "error", error.c_str());
    return encoded(node.get());
}

} // namespace framerift
