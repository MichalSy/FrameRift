// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "launch.hpp"
#include "log.hpp"
#include "elf_input.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#ifdef G_OS_UNIX
#include <signal.h>
#endif

namespace framerift {
namespace {

using Process = std::unique_ptr<GSubprocess, decltype(&g_object_unref)>;

void check_cancelled(GCancellable* cancellable) {
    if (cancellable && g_cancellable_is_cancelled(cancellable))
        throw std::runtime_error("Game preparation was cancelled");
}

void stop_converter(GSubprocess* process) {
    // Conversion owns no running game scope. Allow a brief graceful stop, then reap.
    auto* context = g_main_context_new();
    auto* loop = g_main_loop_new(context, FALSE);
    auto* timeout = g_timeout_source_new(1000);
    g_source_set_callback(timeout, +[](gpointer data) -> gboolean {
        g_subprocess_force_exit(G_SUBPROCESS(data));
        return G_SOURCE_REMOVE;
    }, process, nullptr);
    g_source_attach(timeout, context);
    g_main_context_push_thread_default(context);
    g_subprocess_wait_async(process, nullptr, +[](GObject* source, GAsyncResult* result, gpointer data) {
        GError* error = nullptr;
        g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
        g_clear_error(&error);
        g_main_loop_quit(static_cast<GMainLoop*>(data));
    }, loop);
#ifdef G_OS_UNIX
    g_subprocess_send_signal(process, SIGTERM);
#else
    g_subprocess_force_exit(process);
#endif
    g_main_loop_run(loop);
    g_main_context_pop_thread_default(context);
    g_source_destroy(timeout);
    g_source_unref(timeout);
    g_main_loop_unref(loop);
    g_main_context_unref(context);
}

std::filesystem::path existing_directory(const std::filesystem::path& directory, const char* description) {
    std::error_code error;
    const auto resolved = std::filesystem::canonical(directory, error);
    if (error || !std::filesystem::is_directory(resolved)) {
        throw std::runtime_error(std::string(description) + " directory does not exist: " + directory.string());
    }
    return resolved;
}

bool native_elf(const std::filesystem::path& filename, bool executable) {
    std::ifstream input(filename, std::ios::binary);
    std::array<unsigned char, 20> header{};
    input.read(reinterpret_cast<char*>(header.data()), header.size());
    const auto type = header[16] | (header[17] << 8);
    const auto machine = header[18] | (header[19] << 8);
    const std::array<unsigned char, 7> magic{0x7f, 'E', 'L', 'F', 2, 1, 1};
    return input && std::equal(magic.begin(), magic.end(), header.begin()) &&
        (header[7] == 0 || header[7] == 3) && machine == 62 &&
        (type == 3 || (executable && type == 2));
}

void check_elf(const std::filesystem::path& filename, bool executable) {
    if (!std::filesystem::is_regular_file(filename)) {
        throw std::runtime_error("Native file does not exist: " + filename.string());
    }
    if (!native_elf(filename, executable)) {
        throw std::runtime_error("Expected a native Linux x86-64 ELF file: " + filename.string());
    }
    if (executable && !g_file_test(filename.string().c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
        throw std::runtime_error("Generated game file is not executable: " + filename.string());
    }
}

bool within_directory(const std::filesystem::path& path, const std::filesystem::path& directory) {
    return std::mismatch(directory.begin(), directory.end(), path.begin(), path.end()).first == directory.end();
}

void check_code_directory(const std::filesystem::path& code, const std::filesystem::path& cache_root,
    const std::filesystem::path& original, const std::filesystem::path& runtime, const std::filesystem::path& data_root) {
    const auto resolved = std::filesystem::weakly_canonical(code);
    if (resolved != std::filesystem::weakly_canonical(cache_root) / "compiled-games" / code.filename() || std::filesystem::is_symlink(code)) {
        throw std::runtime_error("The managed game code path contains an unexpected symbolic link");
    }
    for (const auto& protected_directory : {original, runtime, std::filesystem::weakly_canonical(data_root / "saves"),
            std::filesystem::weakly_canonical(data_root / "logs")}) {
        if (within_directory(resolved, protected_directory) || within_directory(protected_directory, resolved)) {
            throw std::runtime_error("The generated code directory overlaps game, runtime or persistent data");
        }
    }
}

void check_work_directory(const std::filesystem::path& work, const std::filesystem::path& data_root,
    const std::filesystem::path& original, const std::filesystem::path& runtime, const std::filesystem::path& code) {
    const auto resolved = std::filesystem::weakly_canonical(work);
    if (resolved != std::filesystem::weakly_canonical(data_root) / "saves" / work.filename() ||
        std::filesystem::is_symlink(work)) {
        throw std::runtime_error("The persistent game directory contains an unexpected symbolic link");
    }
    for (const auto& protected_directory : {original, runtime, std::filesystem::weakly_canonical(code),
            std::filesystem::weakly_canonical(data_root / "logs")}) {
        if (within_directory(resolved, protected_directory) || within_directory(protected_directory, resolved)) {
            throw std::runtime_error("The persistent game directory overlaps game, runtime, generated code or logs");
        }
    }
    if (std::filesystem::exists(work)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(work)) {
            if (std::filesystem::is_symlink(entry.symlink_status())) {
                throw std::runtime_error("Persistent runtime data contains an unexpected symbolic link: " + entry.path().string());
            }
        }
    }
}

bool runtime_directory(const std::string& name) {
    return name == "shader_cache" || name == "ShaderCache" || name == "shaders" || name == "_sd" || name == "_sd_mem" ||
        name == "devlog" || name == "temp0" || name == "download0" || name == "saves" || name == "logs";
}

void remove_generated_code(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> generated;
    for (auto entry = std::filesystem::recursive_directory_iterator(directory); entry != std::filesystem::recursive_directory_iterator(); ++entry) {
        const auto relative = entry->path().lexically_relative(directory);
        const auto first = relative.begin()->string();
        const auto status = entry->symlink_status();
        if (runtime_directory(first)) {
            if (std::filesystem::is_directory(status)) entry.disable_recursion_pending();
            continue;
        }
        const auto name = entry->path().filename().string();
        const bool executable = relative == "game.elf";
        if (std::filesystem::is_symlink(status)) {
            if (first == "app0" || executable) throw std::runtime_error("Generated code contains an unexpected symbolic link");
            continue;
        }
        if (relative == "app0" && !std::filesystem::is_directory(status)) {
            throw std::runtime_error("The generated app0 path is not a directory");
        }
        if (!std::filesystem::is_regular_file(status)) continue;
        if (executable || name.ends_with(".guest.prx") ||
            ((first == "app0" || entry->path().parent_path() == directory) &&
                (entry->path().extension() == ".prx" || entry->path().extension() == ".elf")) ||
            (entry->path().extension() == ".prx" && native_elf(entry->path(), false)) ||
            (entry->path().parent_path() == directory && (name == "game.registry.json" ||
                (name.starts_with("game.") && name.ends_with(".prx.registry.json"))))) {
            generated.push_back(entry->path());
        }
    }
    std::filesystem::remove(directory / "game.elf");
    for (const auto& path : generated) if (path != directory / "game.elf") std::filesystem::remove(path);
}

std::string unique_id() {
    const std::unique_ptr<gchar, decltype(&g_free)> id(g_uuid_string_random(), g_free);
    return id.get();
}

Process spawn_process(const std::vector<std::string>& command, const std::filesystem::path& directory,
    const std::filesystem::path& log_path = {}, const std::filesystem::path& memory_log_path = {},
    const BridgeLaunch& bridge = {}, const std::string& game_id = {}) {
    if (command.empty()) throw std::runtime_error("The launch command is empty");
    const std::unique_ptr<GSubprocessLauncher, decltype(&g_object_unref)> launcher(
        g_subprocess_launcher_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDERR_MERGE |
            (log_path.empty() ? G_SUBPROCESS_FLAGS_STDOUT_PIPE : G_SUBPROCESS_FLAGS_NONE))), g_object_unref);
    g_subprocess_launcher_unsetenv(launcher.get(), "STEAMGRIDDB_API_KEY");
    for (const auto* name : {"FRAMERIFT_BRIDGE_HOST", "FRAMERIFT_BRIDGE_PORT", "FRAMERIFT_BRIDGE_SESSION", "FRAMERIFT_GAME_ID"}) {
        g_subprocess_launcher_unsetenv(launcher.get(), name);
    }
    if (bridge.port != 0) {
        if (bridge.session_id.empty() || game_id.empty()) throw std::runtime_error("The bridge launch identity is missing");
        g_subprocess_launcher_setenv(launcher.get(), "FRAMERIFT_BRIDGE_HOST", "127.0.0.1", true);
        g_subprocess_launcher_setenv(launcher.get(), "FRAMERIFT_BRIDGE_PORT", std::to_string(bridge.port).c_str(), true);
        g_subprocess_launcher_setenv(launcher.get(), "FRAMERIFT_BRIDGE_SESSION", bridge.session_id.c_str(), true);
        g_subprocess_launcher_setenv(launcher.get(), "FRAMERIFT_GAME_ID", game_id.c_str(), true);
    }
    if (!memory_log_path.empty()) {
        g_subprocess_launcher_setenv(launcher.get(), "ANYPS5_MEMORY_LOG", memory_log_path.string().c_str(), true);
    }
    g_subprocess_launcher_set_cwd(launcher.get(), directory.string().c_str());
    if (!log_path.empty()) g_subprocess_launcher_set_stdout_file_path(launcher.get(), log_path.string().c_str());
    std::vector<const gchar*> arguments;
    arguments.reserve(command.size() + 1);
    for (const auto& argument : command) arguments.push_back(argument.c_str());
    arguments.push_back(nullptr);
    GError* error = nullptr;
    Process process(g_subprocess_launcher_spawnv(launcher.get(), arguments.data(), &error), g_object_unref);
    if (!process) {
        const std::string message = error ? error->message : "Could not start the process";
        g_clear_error(&error);
        throw std::runtime_error(message);
    }
    return process;
}

std::filesystem::path select_build(const std::filesystem::path& project, const char* python,
    GCancellable* cancellable) {
    // Reuse AnyPS5's manifests and build validation without its runtime staging.
    const char* discovery = R"(import os, sys
from pathlib import Path
os.environ.pop('ANYPS5_BUILD_DIR', None)
project = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(project / 'tools'))
try:
    from stage_guest_runtime import build_library_names, select_build
    builds = []
    for candidate in project.iterdir():
        if not candidate.is_dir():
            continue
        try:
            build = select_build(project, candidate)
            libraries = build / 'core/libs/libs'
            patcher = build / 'core/libs/nid_patcher'
            libc = libraries / 'unpatched/libc.prx'
            artifacts = [build / 'core/relinker/relinker', patcher]
            for name in build_library_names(build):
                patched = libraries / name
                unpatched = libraries / 'unpatched' / name
                if not patched.is_file() or not patched.stat().st_size:
                    raise RuntimeError('Missing patched library')
                dependencies = [patcher]
                if unpatched.is_file():
                    dependencies.append(unpatched)
                    if name != 'libc.prx':
                        if not libc.is_file():
                            raise RuntimeError('Missing libc export reference')
                        dependencies.append(libc)
                if any(path.stat().st_mtime_ns > patched.stat().st_mtime_ns for path in dependencies):
                    raise RuntimeError('Patched library is out of date')
                artifacts.append(patched)
            builds.append((max(path.stat().st_mtime_ns for path in artifacts), str(build)))
        except (RuntimeError, OSError):
            continue
    if not builds:
        raise RuntimeError('Build the relinker and libs targets in the AnyPS5 checkout before starting a game; no complete, current patched build was found')
    print(max(builds)[1])
except Exception as error:
    print(str(error), file=sys.stderr)
    sys.exit(1)
)";
    auto discovery_process = spawn_process({python, "-B", "-c", discovery, project.string()}, project);
    gchar* output = nullptr;
    GError* error = nullptr;
    if (!g_subprocess_communicate_utf8(discovery_process.get(), nullptr, cancellable, &output, nullptr, &error)) {
        const bool cancelled = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        const std::string message = error ? error->message : "Could not select an AnyPS5 build";
        g_clear_error(&error);
        g_free(output);
        if (cancelled) stop_converter(discovery_process.get());
        throw std::runtime_error(message);
    }
    const std::unique_ptr<gchar, decltype(&g_free)> captured(output, g_free);
    const std::string result = captured ? captured.get() : "";
    if (!g_subprocess_get_successful(discovery_process.get())) {
        throw std::runtime_error("Could not select an AnyPS5 build: " + result);
    }
    if (result.empty() || result.back() != '\n') throw std::runtime_error("AnyPS5 build discovery returned no path");
    return existing_directory(result.substr(0, result.size() - 1), "AnyPS5 build");
}

struct StagingDirectory {
    std::filesystem::path path;
    ~StagingDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void migrate_runtime_data(const std::filesystem::path& code, const std::filesystem::path& work) {
    std::vector<std::filesystem::path> pending;
    for (const auto& entry : std::filesystem::directory_iterator(code)) {
        const auto name = entry.path().filename().string();
        const auto status = entry.symlink_status();
        if (name == "app0" || name.starts_with(".convert-") || name == "game.elf.png" || name == "game.elf.desktop") continue;
        // AnyPS5 reads these relative to the executable, rather than the working directory.
        if (name == "shader_cache" || name == "anyps5-fonts" || name == "anyps5-entitlements.ini") continue;
        if (!runtime_directory(name) && (name == "game.elf" || entry.path().extension() == ".elf" ||
            entry.path().extension() == ".prx" || name == "game.registry.json" ||
            (name.starts_with("game.") && name.ends_with(".prx.registry.json")))) continue;
        if (std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status))) {
            throw std::runtime_error("Legacy runtime data contains an unsupported entry: " + entry.path().string());
        }
        if (std::filesystem::is_directory(status)) {
            for (const auto& child : std::filesystem::recursive_directory_iterator(entry.path())) {
                const auto child_status = child.symlink_status();
                if (std::filesystem::is_symlink(child_status) ||
                    (!std::filesystem::is_regular_file(child_status) && !std::filesystem::is_directory(child_status))) {
                    throw std::runtime_error("Legacy runtime data contains an unsupported entry: " + child.path().string());
                }
            }
        }
        const auto destination = work / entry.path().filename();
        if (std::filesystem::exists(std::filesystem::symlink_status(destination))) {
            throw std::runtime_error("Legacy runtime data conflicts with persistent data; both were preserved: " +
                entry.path().string() + " and " + destination.string());
        }
        pending.push_back(entry.path());
    }
    for (const auto& source : pending) {
        const auto destination = work / source.filename();
        std::error_code error;
        std::filesystem::rename(source, destination, error);
        if (!error) continue;
        if (error != std::errc::cross_device_link) {
            throw std::filesystem::filesystem_error("Could not migrate runtime data", source, destination, error);
        }
        StagingDirectory staging{work / (".migrate-" + unique_id())};
        std::filesystem::create_directory(staging.path);
        const auto copied = staging.path / source.filename();
        std::filesystem::copy(source, copied, std::filesystem::copy_options::recursive);
        std::filesystem::rename(copied, destination);
        std::filesystem::remove_all(source);
    }
}

void publish_generated(const std::filesystem::path& source, const std::filesystem::path& destination) {
    const auto status = std::filesystem::symlink_status(source);
    if (std::filesystem::is_symlink(status)) throw std::runtime_error("Conversion produced an unexpected symbolic link");
    if (std::filesystem::is_directory(status)) {
        if (std::filesystem::is_symlink(destination)) {
            throw std::runtime_error("The generated code directory contains an unexpected symbolic link");
        }
        std::filesystem::create_directories(destination);
        for (const auto& entry : std::filesystem::directory_iterator(source)) {
            publish_generated(entry.path(), destination / entry.path().filename());
        }
    } else if (std::filesystem::is_regular_file(status)) {
        std::filesystem::rename(source, destination);
    } else {
        throw std::runtime_error("Conversion produced an unsupported cache entry");
    }
}

} // namespace

PreparedGame prepare_game(const Game& game, const LaunchSettings& settings,
    const std::filesystem::path& cache_root, const std::filesystem::path& data_root, bool rebuild,
    GCancellable* cancellable) {
#ifndef __linux__
    throw std::runtime_error("Shared-runtime launching is supported on Linux only");
#endif
    check_cancelled(cancellable);
    if (game.is_demo || game.installation_path.empty()) throw std::runtime_error("This game has no local installation");
    if (game.id.empty() || game.id.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos) {
        throw std::runtime_error("The game ID cannot be used for a managed cache directory");
    }
    const auto original = existing_directory(game.installation_path, "Original game");
    if (!std::filesystem::is_regular_file(original / "eboot.bin")) {
        throw std::runtime_error("The original game has no eboot.bin");
    }
    if (!std::ifstream(original / "eboot.bin", std::ios::binary)) {
        throw std::runtime_error("The original eboot.bin cannot be read");
    }
    const auto anyps5 = existing_directory(settings.anyps5_directory, "AnyPS5");
    const auto script = anyps5 / "tools/run_game.py";
    if (!std::filesystem::is_regular_file(script)) throw std::runtime_error("AnyPS5 has no tools/run_game.py launcher");
    const auto guard = anyps5 / "tools/run_with_memory_guard.py";
    if (!std::filesystem::is_regular_file(guard) || !std::ifstream(guard)) {
        throw std::runtime_error("AnyPS5 has no readable tools/run_with_memory_guard.py; unguarded game launch is disabled");
    }
    const std::unique_ptr<gchar, decltype(&g_free)> python(g_find_program_in_path("python3"), g_free);
    if (!python) throw std::runtime_error("Python 3 is required to start AnyPS5 games");
    const auto build = select_build(anyps5, python.get(), cancellable);
    const auto runtime = existing_directory(build / "core/libs/libs", "Runtime");
    if (runtime.string().find_first_of(":$") != std::string::npos) {
        throw std::runtime_error("Runtime paths cannot contain ':' or '$'");
    }
    check_elf(runtime / "libc.prx", false);
    check_elf(runtime / "libkernel.prx", false);

    PreparedGame prepared;
    prepared.game_id = game.id;
    prepared.code_directory = std::filesystem::absolute(cache_root / "compiled-games" / game.id).lexically_normal();
    prepared.working_directory = std::filesystem::absolute(data_root / "saves" / game.id).lexically_normal();
    check_code_directory(prepared.code_directory, cache_root, original, runtime, data_root);
    check_work_directory(prepared.working_directory, data_root, original, runtime, prepared.code_directory);
    const auto session = std::filesystem::absolute(data_root / "logs" / game.id / unique_id()).lexically_normal();
    const auto resolved_session = std::filesystem::weakly_canonical(session);
    if (resolved_session != std::filesystem::weakly_canonical(data_root / "logs") / game.id / session.filename() ||
        within_directory(resolved_session, original) || within_directory(resolved_session, runtime)) {
        throw std::runtime_error("The session log path overlaps the original game or runtime, or contains an unexpected symbolic link");
    }
    prepared.log_path = session / "game.log";
    prepared.conversion_log_path = session / "conversion.log";
    const auto executable = prepared.code_directory / "game.elf";
    const bool converting = rebuild || !std::filesystem::exists(executable);
    std::filesystem::path relinker;
    if (converting) {
        std::error_code error;
        relinker = std::filesystem::canonical(build / "core/relinker/relinker", error);
        if (error || !std::filesystem::is_regular_file(relinker) ||
            !g_file_test(relinker.string().c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
            throw std::runtime_error("Build the AnyPS5 relinker target before converting this game");
        }
    } else {
        if (std::filesystem::is_symlink(executable)) throw std::runtime_error("The generated executable cannot be a symbolic link");
        check_elf(executable, true);
    }
    std::filesystem::create_directories(prepared.code_directory);
    check_cancelled(cancellable);
    std::filesystem::create_directories(prepared.working_directory);
    migrate_runtime_data(prepared.code_directory, prepared.working_directory);
    std::filesystem::create_directories(session);
    if (converting) {
        StagingDirectory staging{prepared.code_directory / (".convert-" + unique_id())};
        std::filesystem::create_directory(staging.path);
        std::ofstream conversion_log(prepared.conversion_log_path);
        if (!conversion_log) throw std::runtime_error("Could not create the conversion log");
        conversion_log.close();
        check_cancelled(cancellable);
        if (rebuild) remove_generated_code(prepared.code_directory);
        const auto generated = staging.path / "game.elf";
        const auto inputs = staging.path / ".inputs";
        const auto default_modules = std::filesystem::exists(original / "sce_modules") && !std::filesystem::exists(original / "sce_module")
            ? "sce_modules" : "sce_module";
        std::filesystem::create_directories(inputs / default_modules);
        prepare_elf_input(original / "eboot.bin", inputs / "eboot.bin");
        std::set<std::filesystem::path> module_directories;
        for (auto iterator = std::filesystem::recursive_directory_iterator(original); iterator != std::filesystem::recursive_directory_iterator(); ++iterator) {
            check_cancelled(cancellable);
            const auto& entry = *iterator;
            if (entry.is_directory() && (entry.path() == original / "fakelib" || entry.path() == original / "sce_sys")) {
                iterator.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file() || (entry.path().extension() != ".prx" && entry.path().extension() != ".sprx")) continue;
            prepare_elf_input(entry.path(), inputs / entry.path().lexically_relative(original));
            const auto relative = entry.path().parent_path().lexically_relative(original);
            if (relative == "sce_module" || relative == "sce_modules" || relative == "prx") continue;
            module_directories.insert(relative);
        }
        std::vector<std::string> conversion{relinker.string(), "--to-intel"};
        for (const auto& directory : module_directories) {
            conversion.push_back("--module-dir");
            conversion.push_back(directory.generic_string());
        }
        conversion.push_back((inputs / "eboot.bin").string());
        conversion.push_back(generated.string());
        auto converter = spawn_process(conversion, staging.path, prepared.conversion_log_path);
        GError* conversion_error = nullptr;
        if (!g_subprocess_wait_check(converter.get(), cancellable, &conversion_error)) {
            const std::string message = conversion_error ? conversion_error->message : "Conversion failed";
            const bool cancelled = g_error_matches(conversion_error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
            g_clear_error(&conversion_error);
            if (cancelled) stop_converter(converter.get());
            throw ConversionError(message + ". Conversion log: " + prepared.conversion_log_path.string(), prepared.conversion_log_path);
        }
        std::filesystem::remove_all(inputs);
        check_cancelled(cancellable);
        if (std::filesystem::is_symlink(generated)) throw std::runtime_error("Conversion produced an unexpected executable link");
        std::filesystem::permissions(generated, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
        check_elf(generated, true);
        for (const auto& entry : std::filesystem::directory_iterator(staging.path)) {
            if (entry.path() != generated) publish_generated(entry.path(), prepared.code_directory / entry.path().filename());
        }
        std::filesystem::rename(generated, executable);
    } else {
        std::ofstream(prepared.conversion_log_path) << "Using cached native executable\n";
    }
    prepared.command = {python.get(), "-B", guard.string(), "--", python.get(), "-B", script.string(),
        "--runtime", runtime.string(), "--game", original.string(),
        "--executable", executable.string(), "--workdir", prepared.working_directory.string()};
    check_cancelled(cancellable);
    return prepared;
}

void launch_game(const PreparedGame& game, GameOutput output, GameFinished finished,
    const BridgeLaunch& bridge, GSubprocess** managed_process) {
    if (managed_process) *managed_process = nullptr;
    auto log = std::make_unique<GameLog>(game.log_path, std::move(output), std::move(finished));
    auto process = spawn_process(game.command, game.working_directory, {}, game.log_path.parent_path() / "memory.jsonl", bridge, game.game_id);
    auto* capture = log.release();
    if (managed_process) *managed_process = G_SUBPROCESS(g_object_ref(process.get()));
    capture->start(process.release());
}

} // namespace framerift
