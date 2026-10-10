// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "log.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace framerift {

GameLog::GameLog(const std::filesystem::path& path, GameOutput output, GameFinished finished)
    : file_(path, std::ios::binary | std::ios::trunc), output_(std::move(output)), finished_(std::move(finished)) {
    if (!file_) throw std::runtime_error("Could not open game log: " + path.string());
}

GameLog::~GameLog() {
    if (process_) g_object_unref(process_);
}

void GameLog::start(GSubprocess* process) noexcept {
    process_ = process;
    g_subprocess_wait_async(process_, nullptr, +[](GObject*, GAsyncResult* result, gpointer data) {
        static_cast<GameLog*>(data)->wait_ready(result);
    }, this);
    read_next();
}

void GameLog::read_next() noexcept {
    g_input_stream_read_async(g_subprocess_get_stdout_pipe(process_), buffer_.data(), buffer_.size(),
        G_PRIORITY_DEFAULT_IDLE, nullptr, +[](GObject* stream, GAsyncResult* result, gpointer data) {
            static_cast<GameLog*>(data)->read_ready(G_INPUT_STREAM(stream), result);
        }, this);
}

void GameLog::fail(const char* message) noexcept {
    failed_ = true;
    try {
        if (error_.empty()) error_ = message;
    } catch (...) {
    }
}

void GameLog::read_ready(GInputStream* stream, GAsyncResult* result) noexcept {
    GError* error = nullptr;
    const auto count = g_input_stream_read_finish(stream, result, &error);
    if (count > 0) {
        try {
            file_.write(buffer_.data(), count);
            file_.flush();
            if (!file_) fail("Could not write the game session log");
        } catch (...) {
            fail("Could not write the game session log");
        }
        if (output_) {
            try {
                output_(std::string_view(buffer_.data(), static_cast<std::size_t>(count)));
            } catch (const std::exception& failure) {
                fail(failure.what());
                output_ = nullptr;
            } catch (...) {
                fail("Could not display game output");
                output_ = nullptr;
            }
        }
        read_next();
        return;
    }
    if (error) fail(error->message);
    g_clear_error(&error);
    if (!g_input_stream_close(stream, nullptr, &error)) fail(error ? error->message : "Could not close game output");
    g_clear_error(&error);
    output_done_ = true;
    complete();
}

void GameLog::wait_ready(GAsyncResult* result) noexcept {
    GError* error = nullptr;
    if (!g_subprocess_wait_finish(process_, result, &error)) fail(error ? error->message : "Could not wait for the game process");
    g_clear_error(&error);
    wait_done_ = true;
    complete();
}

void GameLog::complete() noexcept {
    if (!output_done_ || !wait_done_) return;
    const std::unique_ptr<GameLog> owned(this);
    try {
        file_.close();
        if (!file_) fail("Could not close the game session log");
    } catch (...) {
        fail("Could not close the game session log");
    }
    try {
        if (finished_) finished_(process_, failed_ && error_.empty() ? fallback_error_ : error_);
    } catch (...) {
        g_warning("Could not run the game completion callback");
    }
}

} // namespace framerift
