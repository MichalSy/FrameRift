// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "runtime/launch.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write(const std::filesystem::path& path, const std::string& data, bool executable = false) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(data.data(), data.size());
    output.close();
    require(static_cast<bool>(output), "Could not write launch fixture");
    if (executable) std::filesystem::permissions(path, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
}

std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string elf(unsigned char type) {
    std::array<char, 64> header{};
    const std::array<char, 7> magic{0x7f, 'E', 'L', 'F', 2, 1, 1};
    std::copy(magic.begin(), magic.end(), header.begin());
    header[16] = static_cast<char>(type);
    header[18] = 62;
    return {header.data(), header.size()};
}

template <typename Action>
void rejected(Action action) {
    try {
        action();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("An invalid launch was accepted");
}

void iterate_until(bool& finished) {
    bool timed_out = false;
    const auto timeout = g_timeout_add_seconds(5, +[](gpointer data) -> gboolean {
        *static_cast<bool*>(data) = true;
        return G_SOURCE_REMOVE;
    }, &timed_out);
    while (!finished && !timed_out) g_main_context_iteration(nullptr, true);
    if (!timed_out) g_source_remove(timeout);
    require(!timed_out, "Asynchronous game capture did not finish");
}

} // namespace

int main() {
    const auto* existing_key = g_getenv("STEAMGRIDDB_API_KEY");
    const bool had_key = existing_key != nullptr;
    const std::string previous_key = existing_key ? existing_key : "";
    g_setenv("STEAMGRIDDB_API_KEY", "fixture must not reach child", true);
    const std::unique_ptr<gchar, decltype(&g_free)> id(g_uuid_string_random(), g_free);
    const auto directory = std::filesystem::temp_directory_path() / ("framerift launch " + std::string(id.get()));
    try {
        const auto project = directory / "Any PS5";
        const auto runtime = directory / "Native Runtime";
        const auto original = directory / "Original Game";
        const auto cache = directory / "Cache Root";
        const auto data = directory / "Data Root";
        write(original / "eboot.bin", "Original SELF data");
        write(original / "media/asset.bin", "Original resource");
        write(runtime / "libc.prx", elf(3));
        write(runtime / "libkernel.prx", elf(3));
        const auto guard = project / "tools/run_with_memory_guard.py";
        write(guard, R"(import os, pathlib, sys
assert 'STEAMGRIDDB_API_KEY' not in os.environ
assert sys.argv[1] == '--' and sys.argv[3] == '-B'
assert pathlib.Path(sys.argv[4]) == pathlib.Path(__file__).with_name('run_game.py')
log = pathlib.Path(os.environ['ANYPS5_MEMORY_LOG'])
assert log.is_absolute() and log.name == 'memory.jsonl'
with log.open('x') as output:
    output.write('{"fixture":true}\n')
os.environ['FRAMERIFT_MEMORY_GUARD_CHECK'] = 'active'
os.execv(sys.argv[2], sys.argv[2:])
)");
        write(project / "tools/run_game.py", R"(import argparse, os, pathlib, sys, time
assert 'STEAMGRIDDB_API_KEY' not in os.environ
assert os.environ.get('FRAMERIFT_MEMORY_GUARD_CHECK') == 'active'
p = argparse.ArgumentParser()
for name in ('runtime', 'game', 'executable', 'workdir'):
    p.add_argument('--' + name, required=True)
args = p.parse_args()
assert pathlib.Path.cwd() == pathlib.Path(args.workdir)
assert pathlib.Path(args.executable).parent != pathlib.Path(args.workdir)
assert pathlib.Path(args.runtime, 'libkernel.prx').is_file()
assert pathlib.Path(args.game, 'media/asset.bin').read_text() == 'Original resource'
save = pathlib.Path('_sd/live-save.bin')
save.parent.mkdir(exist_ok=True)
save.write_text('Live save')
memory = pathlib.Path('_sd_mem/u1/slot0.bin')
memory.parent.mkdir(parents=True, exist_ok=True)
memory.write_text('Memory save')
shaders = pathlib.Path(args.executable).parent / 'shader_cache'
shaders.mkdir(exist_ok=True)
(shaders / 'runtime.bin').write_text('Executable-relative shader cache')
print('original=' + args.game)
print('code=' + args.executable)
print('work=' + args.workdir)
print('inherited=' + os.environ.get('FRAMERIFT_LAUNCH_CHECK', ''))
print('child stderr', file=sys.stderr)
sys.stdout.flush()
sys.stderr.flush()
payload = b'\x1b[36mconsole\x1b[0m\n' + b'A' * 65535 + '🎮'.encode() + b'\rprogress 10%\rprogress 100%\n'
os.write(1, payload)
os.write(2, b'\x1b[31mmerged stderr\x1b[0m\n')
os.write(1, b'\xe2\x82')
os.write(2, b'\xac\rmixed\n\x00\xffawait-output\n')
deadline = time.monotonic() + 2
while not pathlib.Path(args.workdir, 'output-ready').exists():
    assert time.monotonic() < deadline, 'Output was not delivered while the child was running'
    time.sleep(0.01)
os.write(1, b'T' * 130000 + b'\nfinished\n')
sys.exit(7)
)");
        const auto relinker = project / "relinker";
        write(relinker, R"(#!/usr/bin/env python3
import os, pathlib, struct, sys
assert 'STEAMGRIDDB_API_KEY' not in os.environ
root = pathlib.Path(__file__).parent
with (root / 'conversion-count').open('a') as count:
    count.write('converted\n')
conversion = (root / 'conversion-count').read_text().count('converted\n')
assert len(sys.argv) == 5 and sys.argv[1:3] == ['--to-intel', '--recursive-module-search']
source, output = map(pathlib.Path, sys.argv[3:])
assert source.read_text() == 'Original SELF data'
assert output.parent == pathlib.Path.cwd()
header = bytearray(64)
header[:7] = b'\x7fELF\x02\x01\x01'
header[8] = conversion
struct.pack_into('<HH', header, 16, 2, 62)
output.write_bytes(header)
module = output.parent / 'app0/sce_module/test.prx.guest.prx'
module.parent.mkdir(parents=True)
module.write_text('Generated module ' + str(conversion))
print('converter stdout')
print('converter stderr', file=sys.stderr)
if (root / 'fail-conversion').exists():
    sys.exit(19)
)", true);
        framerift::Game game;
        game.id = "PPSA01234";
        game.title = "Metadata Title";
        game.installation_path = original.string();
        game.is_demo = false;
        const framerift::LaunchSettings settings{project.string(), runtime.string(), relinker.string()};
        const auto code = cache / "games/PPSA01234";
        const auto work = data / "saves/PPSA01234";
        const std::array legacy{std::filesystem::path("_sd/legacy.bin"), std::filesystem::path("_sd_mem/u2/slot0.bin"),
            std::filesystem::path("controller.json")};
        for (const auto& path : legacy) write(code / path, "Legacy runtime data");
        write(code / "shader_cache/shader.prx.guest.prx", "Native shader cache");
        write(code / "anyps5-fonts/font.ttf", "Executable-relative font");
        write(code / "anyps5-entitlements.ini", "Executable-relative entitlements");
        const auto prepared = framerift::prepare_game(game, settings, cache, data);
        require(prepared.game_id == game.id && prepared.code_directory == code && prepared.working_directory == work,
            "Generated code and persistent working data must use separate directories with a stable game ID");
        for (const auto& path : legacy) {
            require(read(work / path) == "Legacy runtime data" && !std::filesystem::exists(code / path),
                "Legacy saves and configuration were not migrated to persistent storage");
        }
        require(read(code / "shader_cache/shader.prx.guest.prx") == "Native shader cache" &&
            !std::filesystem::exists(work / "shader_cache") &&
            read(code / "anyps5-fonts/font.ttf") == "Executable-relative font" &&
            read(code / "anyps5-entitlements.ini") == "Executable-relative entitlements",
            "Executable-relative AnyPS5 shader caches and support files must remain beside generated code");
        require(prepared.log_path.parent_path().parent_path() == data / "logs/PPSA01234",
            "Session logs must stay beneath the shared data directory");
        require(read(prepared.conversion_log_path).find("converter stderr") != std::string::npos,
            "Conversion output was not redirected to its session log");
        require(!std::filesystem::exists(prepared.log_path.parent_path() / "memory.jsonl"),
            "Conversion must not run through the game's memory guard");
        require(read(code / "app0/sce_module/test.prx.guest.prx") == "Generated module 1",
            "Generated modules were not published beside the executable");
        require(!std::filesystem::exists(code / "media") && !std::filesystem::exists(code / "eboot.bin"),
            "Original resources were copied into the code cache");
        g_setenv("FRAMERIFT_LAUNCH_CHECK", "inherited value", true);
        const auto expected = "child stderr\noriginal=" + original.string() + "\ncode=" +
            (code / "game.elf").string() + "\nwork=" + work.string() +
            "\ninherited=inherited value\n\x1b[36mconsole\x1b[0m\n" + std::string(65535, 'A') +
            "\xf0\x9f\x8e\xae\rprogress 10%\rprogress 100%\n\x1b[31mmerged stderr\x1b[0m\n\xe2\x82\xac\rmixed\n" +
            std::string("\0\xff", 2) + "await-output\n" + std::string(130000, 'T') + "\nfinished\n";
        std::string captured;
        std::string completion_error;
        std::string completed_log;
        int exit_status = -1;
        int completions = 0;
        bool delivered_live = false;
        bool finished = false;
        framerift::launch_game(prepared, [&](std::string_view bytes) {
            require(bytes.size() <= 64 * 1024, "Game output chunks must be bounded");
            captured.append(bytes);
            if (!delivered_live && captured.find("await-output\n") != std::string::npos) {
                delivered_live = true;
                write(prepared.working_directory / "output-ready", "ready");
            }
        }, [&](GSubprocess* process, const std::string& error) {
            completion_error = error;
            exit_status = g_subprocess_get_if_exited(process) ? g_subprocess_get_exit_status(process) : -1;
            completed_log = read(prepared.log_path);
            ++completions;
            finished = true;
        });
        iterate_until(finished);
        require(completion_error.empty() && exit_status == 7 && completions == 1, "Child exit status or capture completion is wrong");
        const auto log = read(prepared.log_path);
        require(delivered_live && captured == expected && log == expected && completed_log == expected,
            "Live merged output and the closed session log must preserve every raw byte before completion");
        require(read(prepared.log_path.parent_path() / "memory.jsonl") == "{\"fixture\":true}\n" &&
            !std::filesystem::exists(work / "memory.jsonl"),
            "Memory guard measurements must stay in the game session log directory");
        require(read(work / "_sd/live-save.bin") == "Live save" && read(work / "_sd_mem/u1/slot0.bin") == "Memory save" &&
            !std::filesystem::exists(code / "_sd") && !std::filesystem::exists(code / "_sd_mem"),
            "The game's relative save files must be written beneath the shared persistent data directory");
        write(work / "shader_cache/shader.prx.guest.prx", "Previously migrated shader cache");
        const auto reused = framerift::prepare_game(game, settings, cache, data);
        require(reused.log_path != prepared.log_path && read(project / "conversion-count") == "converted\n",
            "A valid generated executable should be reused with a fresh session log");
        require(read(code / "shader_cache/runtime.bin") == "Executable-relative shader cache" &&
            read(code / "shader_cache/shader.prx.guest.prx") == "Native shader cache" &&
            read(work / "shader_cache/shader.prx.guest.prx") == "Previously migrated shader cache",
            "Repeated preparation must preserve both shader caches without migrating or conflicting");
        std::filesystem::rename(guard, guard.string() + ".disabled");
        bool missing_guard = false;
        try {
            framerift::prepare_game(game, settings, cache, data, true);
        } catch (const std::runtime_error& error) {
            missing_guard = std::string(error.what()).find("run_with_memory_guard.py") != std::string::npos;
        }
        require(missing_guard && read(project / "conversion-count") == "converted\n" && read(code / "game.elf")[8] == 1,
            "A missing memory guard must fail clearly before conversion or an unguarded launch");
        std::filesystem::rename(guard.string() + ".disabled", guard);
        const std::array stale{code / "app0/Media/Plugins/stale.prx.guest.prx", code / "app0/Media/Modules/broken.prx",
            code / "app0/old.elf", code / "libStale.prx", code / "game.registry.json", code / "game.stale.prx.guest.prx.registry.json"};
        for (const auto& path : stale) write(path, "Old generated code");
        const std::array preserved{code / "shader_cache/shader.prx.guest.prx", code / "shader_cache/runtime.bin",
            work / "shader_cache/shader.prx.guest.prx", work / "_sd/save.bin",
            work / "_sd_mem/u1/slot1.bin", work / "devlog/output.log", work / "temp0/resource.bin", work / "assets.registry.json",
            data / "saves/PPSA01234/save.bin"};
        for (const auto& path : preserved) write(path, "Preserved data");
        const auto rebuilt = framerift::prepare_game(game, settings, cache, data, true);
        require(read(project / "conversion-count") == "converted\nconverted\n" &&
            read(code / "game.elf")[8] == 2 && read(code / "app0/sce_module/test.prx.guest.prx") == "Generated module 2",
            "Rebuild must replace the executable and modules through a second conversion");
        for (const auto& path : stale) require(!std::filesystem::exists(path), "An obsolete generated code artifact survived rebuild");
        for (const auto& path : preserved) require(read(path) == "Preserved data", "Rebuild removed persistent or nongenerated data");
        require(!std::filesystem::exists(rebuilt.log_path) && read(prepared.log_path) == expected &&
            read(runtime / "libc.prx") == elf(3) && read(runtime / "libkernel.prx") == elf(3),
            "Rebuild must preserve old logs and the shared runtime without launching a game");
        const auto rebuilt_executable = read(code / "game.elf");
        auto missing_relinker = settings;
        missing_relinker.relinker_path = (project / "missing-relinker").string();
        rejected([&] { framerift::prepare_game(game, missing_relinker, cache, data, true); });
        require(read(code / "game.elf") == rebuilt_executable &&
            read(code / "app0/sce_module/test.prx.guest.prx") == "Generated module 2",
            "Invalid rebuild configuration changed the existing generated code");
        framerift::prepare_game(game, missing_relinker, cache, data);
        require(read(project / "conversion-count") == "converted\nconverted\n", "A cached launch unexpectedly required a relinker");
        write(code / "_sd/conflict.bin", "Legacy conflict");
        write(code / "unmigrated.json", "Unmigrated data");
        rejected([&] { framerift::prepare_game(game, settings, cache, data, true); });
        require(read(code / "_sd/conflict.bin") == "Legacy conflict" && read(work / "_sd/live-save.bin") == "Live save" &&
            read(code / "unmigrated.json") == "Unmigrated data" && !std::filesystem::exists(work / "unmigrated.json") &&
            read(code / "game.elf") == rebuilt_executable,
            "A migration conflict must preserve both save trees and avoid partial migration or rebuild");
        std::filesystem::remove_all(code / "_sd");
        std::filesystem::remove(code / "unmigrated.json");
        std::filesystem::create_directory_symlink(original, code / "_sd");
        rejected([&] { framerift::prepare_game(game, settings, cache, data, true); });
        require(read(code / "game.elf") == rebuilt_executable && read(original / "eboot.bin") == "Original SELF data",
            "A legacy save link must not be followed or trigger code cleanup");
        std::filesystem::remove(code / "_sd");
        std::filesystem::create_directory_symlink(original, work / "_sd/original-link");
        rejected([&] { framerift::prepare_game(game, settings, cache, data, true); });
        require(read(code / "game.elf") == rebuilt_executable && read(original / "eboot.bin") == "Original SELF data",
            "A persistent save link must be rejected before launch or code cleanup");
        std::filesystem::remove(work / "_sd/original-link");
        rejected([&] { framerift::prepare_game(game, settings, original, data, true); });
        rejected([&] { framerift::prepare_game(game, settings, runtime, data, true); });
        require(!std::filesystem::exists(original / "games") && !std::filesystem::exists(runtime / "games"),
            "A code cache was created inside the original game or shared runtime");
        const auto linked_cache = directory / "Linked Cache";
        std::filesystem::create_directory(linked_cache);
        std::filesystem::create_directory_symlink(original, linked_cache / "games");
        rejected([&] { framerift::prepare_game(game, settings, linked_cache, data, true); });
        require(!std::filesystem::exists(original / game.id), "An ancestor cache link wrote into the original game");
        const auto linked_data = directory / "Linked Data";
        std::filesystem::create_directories(linked_data / "logs");
        std::filesystem::create_directory_symlink(original, linked_data / "logs" / game.id);
        rejected([&] { framerift::prepare_game(game, settings, cache, linked_data, true); });
        for (const auto& entry : std::filesystem::directory_iterator(original)) {
            require(entry.path().filename() == "eboot.bin" || entry.path().filename() == "media",
                "A session log link wrote into the original game");
        }
        require(read(code / "game.elf") == rebuilt_executable, "An unsafe log path changed the existing generated code");
        const auto linked_saves = directory / "Linked Saves";
        std::filesystem::create_directories(linked_saves);
        std::filesystem::create_directory_symlink(original, linked_saves / "saves");
        rejected([&] { framerift::prepare_game(game, settings, cache, linked_saves, true); });
        require(!std::filesystem::exists(original / game.id) && read(code / "game.elf") == rebuilt_executable,
            "An ancestor persistent-directory link wrote into the original game or triggered code cleanup");
        rejected([&] { framerift::prepare_game(game, settings, cache, original, true); });
        rejected([&] { framerift::prepare_game(game, settings, cache, runtime, true); });
        require(!std::filesystem::exists(original / "saves") && !std::filesystem::exists(runtime / "saves"),
            "Persistent data was created inside the original game or shared runtime");
        std::filesystem::create_directory_symlink(original, code / "app0/original-link");
        rejected([&] { framerift::prepare_game(game, settings, cache, data, true); });
        require(read(code / "game.elf") == rebuilt_executable, "An unsafe generated-code link triggered partial cleanup");
        std::filesystem::remove(code / "app0/original-link");
        require(read(original / "eboot.bin") == "Original SELF data" && read(original / "media/asset.bin") == "Original resource",
            "Original game files were changed");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(code)) {
            require(!entry.is_symlink() && !entry.path().filename().string().starts_with(".convert-"),
                "A conversion staging directory or symbolic link was left in the cache");
        }
        auto invalid = game;
        invalid.id = "../unsafe";
        rejected([&] { framerift::prepare_game(invalid, settings, cache, data); });
        invalid = game;
        invalid.is_demo = true;
        rejected([&] { framerift::prepare_game(invalid, settings, cache, data); });
        write(code / "game.elf", "Partial output", true);
        rejected([&] { framerift::prepare_game(game, settings, cache, data); });
        write(code / "game.elf", elf(2));
        std::filesystem::permissions(code / "game.elf", std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
        rejected([&] { framerift::prepare_game(game, settings, cache, data); });
        write(code / "game.elf", elf(2), true);
        write(project / "fail-conversion", "fail");
        bool conversion_failed = false;
        try {
            framerift::prepare_game(game, settings, cache, data, true);
        } catch (const framerift::ConversionError& error) {
            conversion_failed = true;
            require(error.log_path.filename() == "conversion.log" && error.log_path.parent_path().parent_path() == data / "logs" / game.id &&
                read(error.log_path).find("converter stderr") != std::string::npos &&
                std::string(error.what()).find(error.log_path.string()) != std::string::npos,
                "A conversion failure must expose its readable session log and retain the existing error message");
        }
        require(conversion_failed, "A failed conversion did not report ConversionError");
        require(!std::filesystem::exists(code / "game.elf") &&
            !std::filesystem::exists(code / "app0/sce_module/test.prx.guest.prx"), "A failed rebuild left reusable generated code");
        for (const auto& path : preserved) require(read(path) == "Preserved data", "A failed rebuild removed persistent data");
        for (const auto& entry : std::filesystem::directory_iterator(code)) {
            require(!entry.path().filename().string().starts_with(".convert-"), "Failed conversion staging was not removed");
        }
        write(code / "game.elf", elf(2), true);
        auto callback_failure = framerift::prepare_game(game, settings, cache, data);
        bool failure_finished = false;
        int output_calls = 0;
        std::string failure_error;
        framerift::launch_game(callback_failure, [&](std::string_view) {
            ++output_calls;
            throw std::runtime_error("Output fixture failed");
        }, [&](GSubprocess* process, const std::string& error) {
            failure_error = error;
            exit_status = g_subprocess_get_if_exited(process) ? g_subprocess_get_exit_status(process) : -1;
            failure_finished = true;
        });
        iterate_until(failure_finished);
        require(output_calls == 1 && failure_error == "Output fixture failed" && exit_status == 7 &&
            read(callback_failure.log_path) == expected, "A display failure must still drain the child and preserve the full raw log");
        auto log_failure = framerift::prepare_game(game, settings, cache, data);
        std::filesystem::create_symlink("/dev/full", log_failure.log_path);
        failure_finished = false;
        failure_error.clear();
        captured.clear();
        framerift::launch_game(log_failure, [&](std::string_view bytes) { captured.append(bytes); },
            [&](GSubprocess* process, const std::string& error) {
                failure_error = error;
                exit_status = g_subprocess_get_if_exited(process) ? g_subprocess_get_exit_status(process) : -1;
                failure_finished = true;
            });
        iterate_until(failure_finished);
        require(!failure_error.empty() && exit_status == 7 && captured == expected,
            "A log write failure must be reported after the output is fully drained");
        auto rejected_launch = prepared;
        rejected_launch.log_path = directory / "missing/log/game.log";
        bool unexpectedly_finished = false;
        rejected([&] {
            framerift::launch_game(rejected_launch, {}, [&](GSubprocess*, const std::string&) { unexpectedly_finished = true; });
        });
        rejected_launch.log_path = prepared.log_path.parent_path() / "spawn-failure.log";
        rejected_launch.command = {"/missing/framerift-launch-fixture"};
        rejected([&] {
            framerift::launch_game(rejected_launch, {}, [&](GSubprocess*, const std::string&) { unexpectedly_finished = true; });
        });
        require(!unexpectedly_finished, "Synchronous open/spawn failures must not take asynchronous ownership");
        if (std::filesystem::is_directory("/dev/shm")) {
            const auto other_data = std::filesystem::path("/dev/shm") / ("framerift-save-check-" + std::string(id.get()));
            try {
                auto cross_device_game = game;
                cross_device_game.id = "PPSA54321";
                const auto old_code = cache / "games" / cross_device_game.id;
                write(old_code / "game.elf", elf(2), true);
                write(old_code / "_sd/save.bin", "Cross-device save");
                const auto migrated = framerift::prepare_game(cross_device_game, settings, cache, other_data);
                require(read(migrated.working_directory / "_sd/save.bin") == "Cross-device save" &&
                    !std::filesystem::exists(old_code / "_sd") && read(old_code / "game.elf") == elf(2),
                    "Migration must preserve saves when cache and data use different filesystems");
                for (const auto& entry : std::filesystem::directory_iterator(migrated.working_directory)) {
                    require(!entry.path().filename().string().starts_with(".migrate-"), "Migration staging was not removed");
                }
                std::filesystem::remove_all(other_data);
            } catch (...) {
                std::filesystem::remove_all(other_data);
                throw;
            }
        }
        g_unsetenv("FRAMERIFT_LAUNCH_CHECK");
        if (had_key) g_setenv("STEAMGRIDDB_API_KEY", previous_key.c_str(), true);
        else g_unsetenv("STEAMGRIDDB_API_KEY");
        std::filesystem::remove_all(directory);
        std::cout << "Conversion, selective rebuild, cached launch and live merged output checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        g_unsetenv("FRAMERIFT_LAUNCH_CHECK");
        if (had_key) g_setenv("STEAMGRIDDB_API_KEY", previous_key.c_str(), true);
        else g_unsetenv("STEAMGRIDDB_API_KEY");
        std::filesystem::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
