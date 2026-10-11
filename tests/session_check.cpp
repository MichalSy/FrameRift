// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "library/library.hpp"
#include "runtime/bridge.hpp"
#include "runtime/mcp.hpp"
#include "runtime/session.hpp"

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Unref { void operator()(gpointer object) const { if (object) g_object_unref(object); } };
template <typename T> using Object = std::unique_ptr<T, Unref>;

class Json {
public:
    explicit Json(const std::string& text) : parser(json_parser_new()) {
        GError* error = nullptr;
        if (!json_parser_load_from_data(parser.get(), text.c_str(), text.size(), &error)) {
            const std::string message = error ? error->message : "Invalid JSON response";
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
    require(json_object_has_member(object, name), "An expected response member is missing");
    const auto* value = json_object_get_string_member(object, name);
    require(value != nullptr, "An expected response member is not a string");
    return value;
}

JsonObject* result(const Json& response) {
    require(!json_object_has_member(response.object(), "error"), "MCP unexpectedly returned a JSON-RPC error");
    auto* value = json_object_get_object_member(response.object(), "result");
    require(value != nullptr, "MCP returned no tool result");
    return value;
}

JsonObject* data(const Json& response) {
    auto* value = result(response);
    require(!json_object_get_boolean_member(value, "isError"), "MCP game tool unexpectedly failed");
    auto* structured = json_object_get_object_member(value, "structuredContent");
    require(structured != nullptr, "MCP game tool omitted structured output");
    return structured;
}

template <typename Predicate>
void wait_until(Predicate ready, const char* failure) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!ready()) {
        require(std::chrono::steady_clock::now() < deadline, failure);
        while (g_main_context_iteration(nullptr, false)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

Json call(const framerift::McpServer& mcp, const std::string& name, const std::string& arguments = "{}") {
    const auto body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"" +
        name + "\",\"arguments\":" + arguments + "}}";
    std::string response;
    std::atomic<bool> finished{false};
    std::exception_ptr failure;
    Object<GCancellable> cancel(g_cancellable_new());
    std::thread client([&] {
        try {
            Object<SoupSession> session(soup_session_new());
            Object<SoupMessage> message(soup_message_new("POST", mcp.endpoint().c_str()));
            require(message != nullptr, "Could not create game MCP request");
            soup_message_headers_replace(soup_message_get_request_headers(message.get()), "Accept", "application/json, text/event-stream");
            auto* input = g_bytes_new(body.data(), body.size());
            soup_message_set_request_body_from_bytes(message.get(), "application/json", input);
            g_bytes_unref(input);
            GError* error = nullptr;
            auto* output = soup_session_send_and_read(session.get(), message.get(), cancel.get(), &error);
            if (!output) {
                const std::string description = error ? error->message : "Game MCP request failed";
                g_clear_error(&error);
                throw std::runtime_error(description);
            }
            gsize size = 0;
            const auto* bytes = static_cast<const char*>(g_bytes_get_data(output, &size));
            response.assign(bytes, size);
            g_bytes_unref(output);
            require(soup_message_get_status(message.get()) == 200, "Game MCP request failed over HTTP");
        } catch (...) { failure = std::current_exception(); }
        finished.store(true, std::memory_order_release);
    });
    try { wait_until([&] { return finished.load(std::memory_order_acquire); }, "Game MCP request did not finish"); }
    catch (...) { g_cancellable_cancel(cancel.get()); client.join(); throw; }
    client.join();
    if (failure) std::rethrow_exception(failure);
    return Json(response);
}

void write(const std::filesystem::path& path, const std::string& bytes, bool executable = false) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), bytes.size());
    output.close();
    require(static_cast<bool>(output), "Could not write a game session fixture");
    if (executable) std::filesystem::permissions(path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string elf(unsigned char type) {
    std::array<char, 64> bytes{};
    const std::array<char, 7> magic{0x7f, 'E', 'L', 'F', 2, 1, 1};
    std::copy(magic.begin(), magic.end(), bytes.begin());
    bytes[16] = static_cast<char>(type);
    bytes[18] = 62;
    return {bytes.data(), bytes.size()};
}

bool process_gone(const std::filesystem::path& pid_file) {
    const auto pid = std::stoi(read(pid_file));
    require(pid > 1, "Invalid fixture process ID");
    return ::kill(pid, 0) == -1 && errno == ESRCH;
}

} // namespace

int main() {
    const std::unique_ptr<gchar, decltype(&g_free)> id(g_uuid_string_random(), g_free);
    const auto directory = std::filesystem::temp_directory_path() / ("framerift session check " + std::string(id.get()));
    try {
        const auto project = directory / "Any PS5";
        const auto build = project / "build";
        const auto runtime = build / "core/libs/libs";
        const auto originals = directory / "Original Games";
        const auto cache = directory / "Cache";
        const auto persistent = directory / "Data";
        const auto first = originals / "folder name is not the title";
        const auto second = originals / "another folder";
        write(first / "eboot.bin", "Original first SELF");
        write(first / "sce_sys/param.json", R"({"titleId":"PPSA01111","titleName":"Fixture One"})");
        write(second / "eboot.bin", "Original second SELF");
        write(second / "sce_sys/param.json", R"({"titleId":"PPSA02222","titleName":"Fixture Two"})");
        write(runtime / "libc.prx", elf(3));
        write(runtime / "libkernel.prx", elf(3));
        write(build / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY:INTERNAL=" + project.string() + "\n");
        write(build / "build.ninja", "build core/libs/libs: phony core/libs/libs/libc.prx core/libs/libs/libkernel.prx\n");
        write(build / "core/libs/nid_patcher", "#!/bin/sh\nexit 99\n", true);
        write(project / "tools/stage_guest_runtime.py", R"(import os, re
from pathlib import Path
def build_library_names(build):
    manifest = build / 'build.ninja'
    return set(re.findall(r'core/libs/libs/([^/\s]+\.prx)', manifest.read_text())) if manifest.is_file() else set()
def select_build(project, override=None):
    assert 'ANYPS5_BUILD_DIR' not in os.environ
    assert override is not None
    build = Path(override).resolve()
    cache = build / 'CMakeCache.txt'
    if not cache.is_file() or cache.read_text().strip() != 'CMAKE_HOME_DIRECTORY:INTERNAL=' + str(project):
        raise RuntimeError('Not a local CMake build')
    if not {'libc.prx', 'libkernel.prx'}.issubset(build_library_names(build)):
        raise RuntimeError('Missing runtime manifest')
    for name in ('core/relinker/relinker', 'core/libs/nid_patcher'):
        if not os.access(build / name, os.X_OK):
            raise RuntimeError('Incomplete build')
    return build
)");
        write(project / "tools/run_with_memory_guard.py", R"(import os, pathlib, signal, subprocess, sys
assert sys.argv[1] == '--'
root = pathlib.Path.cwd()
(root / 'guard.pid').write_text(str(os.getpid()))
child = subprocess.Popen(sys.argv[2:], start_new_session=True)
(root / 'child.pid').write_text(str(child.pid))
def stop(signum, frame):
    os.killpg(child.pid, signal.SIGTERM)
signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)
code = child.wait()
print('guard stopped', flush=True)
sys.exit(code if code >= 0 else 128 - code)
)");
        write(project / "tools/run_game.py", R"(import argparse, os, pathlib, time
parser = argparse.ArgumentParser()
for name in ('runtime', 'game', 'executable', 'workdir'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
root = pathlib.Path.cwd()
assert root == pathlib.Path(args.workdir)
assert pathlib.Path(args.game, 'eboot.bin').is_file()
assert pathlib.Path(args.executable).is_file()
assert os.environ['FRAMERIFT_BRIDGE_HOST'] == '127.0.0.1'
assert int(os.environ['FRAMERIFT_BRIDGE_PORT']) > 0
assert os.environ['FRAMERIFT_BRIDGE_SESSION']
assert os.environ['FRAMERIFT_GAME_ID'] in ('PPSA01111', 'PPSA02222')
for index in range(10):
    print('game line ' + str(index).zfill(2), flush=True)
if (root / 'exit-seven').exists():
    print('natural failure', flush=True)
    raise SystemExit(7)
while True:
    if (root / 'write-large-log').exists():
        (root / 'write-large-log').unlink()
        print('L' * 300000, flush=True)
        print('large-end', flush=True)
    time.sleep(0.01)
)");
        write(build / "core/relinker/relinker", R"(#!/usr/bin/env python3
import os, pathlib, struct, sys, time
root = pathlib.Path(__file__).parents[3]
for index in range(8):
    print('conversion line ' + str(index).zfill(2), flush=True)
if (root / 'slow-conversion').exists():
    (root / 'converter.pid').write_text(str(os.getpid()))
    (root / 'conversion-ready').write_text('ready')
    while True:
        time.sleep(0.01)
if (root / 'fail-conversion').exists():
    sys.exit(19)
header = bytearray(64)
header[:7] = b'\x7fELF\x02\x01\x01'
struct.pack_into('<HH', header, 16, 2, 62)
pathlib.Path(sys.argv[-1]).write_bytes(header)
)", true);
        const auto timestamp = std::filesystem::file_time_type::clock::now();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(build)) {
            if (entry.is_regular_file()) std::filesystem::last_write_time(entry.path(), timestamp);
        }

        {
            framerift::Library library((directory / "library.sqlite").string());
            framerift::Game demo;
            demo.id = "sgdb-777";
            demo.title = "Preview only";
            demo.cover_path = demo.hero_path = demo.landscape_path = "fixture-artwork";
            demo.steamgriddb_id = 777;
            library.cache_demo({demo});
            library.set_game_folders({originals.string()});
            const auto scanned = framerift::scan_game_folders(library.game_folders());
            require(scanned.warnings.empty() && scanned.games.size() == 2, "Temporary metadata game folders were not scanned");
            library.cache_scanned(scanned.games);
            Object<GApplication> application(g_application_new("io.github.framerift.SessionCheck", G_APPLICATION_NON_UNIQUE));
            GError* registration_error = nullptr;
            require(g_application_register(application.get(), nullptr, &registration_error), "Could not register the test application");
            g_clear_error(&registration_error);
            framerift::BridgeServer bridge(0);
            std::vector<std::string> states;
            std::string captured;
            framerift::GameSession session(library, cache, persistent, application.get(), &bridge,
                [&](const framerift::GameSessionStatus& status) { states.push_back(status.state); },
                [&](std::string_view bytes) { captured.append(bytes); });
            framerift::McpServer mcp(bridge, 0, &session);

            const auto listed = call(mcp, "list_games");
            auto* games = json_object_get_array_member(data(listed), "games");
            require(games && json_array_get_length(games) == 2, "MCP did not list the configured scanned game library");
            bool first_found = false;
            for (guint index = 0; index < json_array_get_length(games); ++index) {
                auto* game = json_array_get_object_element(games, index);
                if (member(game, "game_id") == "PPSA01111") first_found = member(game, "title") == "Fixture One";
            }
            require(first_found, "MCP game listing replaced the metadata title with an installation folder name");
            require(json_object_get_boolean_member(result(call(mcp, "start_game", R"({"game_id":"missing-game"})")), "isError"),
                "MCP accepted an unknown game ID");
            require(json_object_get_boolean_member(result(call(mcp, "start_game", R"({"game_id":"sgdb-777"})")), "isError"),
                "MCP launched a preview game");
            const auto missing_settings = call(mcp, "start_game", R"({"game_id":"PPSA01111"})");
            if (!json_object_get_boolean_member(result(missing_settings), "isError")) {
                wait_until([&] { return !session.status().busy(); }, "A launch without runtime settings did not fail");
                require(session.status().state == "failed", "Missing runtime settings did not report a failed session");
            }
            require(!std::filesystem::exists(persistent / "saves/PPSA01111/guard.pid"), "Missing runtime settings started a game process");

            library.set_launch_settings({project.string()});
            write(project / "slow-conversion", "wait");
            data(call(mcp, "start_game", R"({"game_id":"PPSA01111"})"));
            wait_until([&] { return std::filesystem::exists(project / "conversion-ready"); }, "The asynchronous converter fixture did not start");
            require(session.status().state == "preparing", "Conversion blocked the main loop or skipped preparation status");
            require(json_object_get_boolean_member(result(call(mcp, "start_game", R"({"game_id":"PPSA02222"})")), "isError"),
                "A second game was started while conversion was active");
            data(call(mcp, "stop_game", R"({"game_id":"PPSA01111"})"));
            wait_until([&] { return !session.status().busy(); }, "Stopping preparation did not cancel and reap the converter");
            require(session.status().state == "stopped" && process_gone(project / "converter.pid") &&
                !std::filesystem::exists(persistent / "saves/PPSA01111/guard.pid"), "A cancelled conversion left a converter or launched the game");
            std::filesystem::remove(project / "slow-conversion");

            data(call(mcp, "start_game", R"({"game_id":"PPSA01111"})"));
            wait_until([&] { return session.status().state == "running" && captured.find("game line 09\n") != std::string::npos; },
                "The scanned game did not launch asynchronously and stream its output");
            const auto running = call(mcp, "get_game_status");
            require(member(data(running), "game_id") == "PPSA01111" && member(data(running), "title") == "Fixture One" &&
                json_object_get_boolean_member(data(running), "busy") && !member(data(running), "session_id").empty(),
                "MCP did not report the shared live game session");
            require(json_object_get_boolean_member(result(call(mcp, "start_game", R"({"game_id":"PPSA02222"})")), "isError") &&
                json_object_get_boolean_member(result(call(mcp, "stop_game", R"({"game_id":"PPSA02222"})")), "isError") &&
                session.status().game_id == "PPSA01111", "A competing start or wrong-ID stop changed the running game");
            const auto tail = call(mcp, "get_game_log");
            auto* lines = json_object_get_array_member(data(tail), "lines");
            require(member(data(tail), "source") == "game" && lines && json_array_get_length(lines) == 5 &&
                std::string(json_array_get_string_element(lines, 0)) == "game line 05" &&
                std::string(json_array_get_string_element(lines, 4)) == "game line 09", "The live game log did not default to its last five lines");
            const auto custom = call(mcp, "get_game_log", R"({"lines":2})");
            auto* custom_lines = json_object_get_array_member(data(custom), "lines");
            require(custom_lines && json_array_get_length(custom_lines) == 2 &&
                std::string(json_array_get_string_element(custom_lines, 0)) == "game line 08", "A requested live log tail length was ignored");
            write(persistent / "saves/PPSA01111/write-large-log", "write");
            wait_until([&] { return captured.find("large-end\n") != std::string::npos; }, "The long-line log fixture was not captured");
            const auto bounded = call(mcp, "get_game_log", R"({"lines":200})");
            auto* bounded_lines = json_object_get_array_member(data(bounded), "lines");
            require(bounded_lines && json_array_get_length(bounded_lines) <= 200 &&
                json_object_get_boolean_member(data(bounded), "truncated"), "A large game log was not reported as bounded");
            std::size_t tail_bytes = 0;
            for (guint index = 0; index < json_array_get_length(bounded_lines); ++index)
                tail_bytes += std::char_traits<char>::length(json_array_get_string_element(bounded_lines, index));
            require(tail_bytes <= 256 * 1024, "A game log response exceeded its byte budget");
            data(call(mcp, "stop_game", R"({"game_id":"PPSA01111"})"));
            wait_until([&] { return !session.status().busy(); }, "Stopping the running game did not finish");
            require(session.status().state == "stopped" && process_gone(persistent / "saves/PPSA01111/guard.pid") &&
                process_gone(persistent / "saves/PPSA01111/child.pid"), "Stopping a game left the guard or its child alive");

            write(project / "fail-conversion", "fail");
            data(call(mcp, "start_game", R"({"game_id":"PPSA02222"})"));
            wait_until([&] { return !session.status().busy(); }, "A failed conversion did not finish its session");
            require(session.status().state == "failed" && !session.status().error.empty(), "A conversion failure did not report its error");
            const auto failed_log = call(mcp, "get_game_log");
            auto* conversion_lines = json_object_get_array_member(data(failed_log), "lines");
            require(member(data(failed_log), "game_id") == "PPSA02222" && member(data(failed_log), "source") == "conversion" &&
                std::filesystem::path(member(data(failed_log), "path")).filename() == "conversion.log" &&
                conversion_lines && json_array_get_length(conversion_lines) == 5 &&
                std::string(json_array_get_string_element(conversion_lines, 4)) == "conversion line 07",
                "Failed conversion log polling returned the previous game's output");
            std::filesystem::remove(project / "fail-conversion");
            write(persistent / "saves/PPSA01111/exit-seven", "exit");
            data(call(mcp, "start_game", R"({"game_id":"PPSA01111"})"));
            wait_until([&] { return !session.status().busy(); }, "A naturally failing game did not finish its session");
            require(session.status().state == "failed" && session.status().exit_code == 7 && !session.status().error.empty(),
                "An unsuccessful natural game exit did not report a failure");
            const auto exit_log = call(mcp, "get_game_log");
            auto* exit_lines = json_object_get_array_member(data(exit_log), "lines");
            bool failure_in_log = false;
            if (exit_lines) {
                for (guint index = 0; index < json_array_get_length(exit_lines); ++index)
                    failure_in_log |= std::string(json_array_get_string_element(exit_lines, index)) == "natural failure";
            }
            require(member(data(exit_log), "game_id") == "PPSA01111" && member(data(exit_log), "source") == "game" && failure_in_log,
                "A natural game failure did not retain its readable current game log");
            require(std::find(states.begin(), states.end(), "preparing") != states.end() &&
                std::find(states.begin(), states.end(), "running") != states.end() &&
                std::find(states.begin(), states.end(), "stopped") != states.end(), "Shared session changes were not delivered to presentation callbacks");
            require(read(first / "eboot.bin") == "Original first SELF" && read(second / "eboot.bin") == "Original second SELF",
                "The session fixture changed original game files");
        }
        std::filesystem::remove_all(directory);
        std::cout << "Scanned game MCP launch, stop, preparation cancellation and bounded log checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
