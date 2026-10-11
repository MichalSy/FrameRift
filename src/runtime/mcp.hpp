// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace framerift {

class BridgeServer;

// HTTP requests are served by the owning GLib main context.
class McpServer {
public:
    explicit McpServer(BridgeServer& bridge, std::uint16_t port = 47653);
    ~McpServer();
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    std::uint16_t port() const;
    std::string endpoint() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace framerift
