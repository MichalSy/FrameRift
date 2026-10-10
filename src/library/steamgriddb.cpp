// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "steamgriddb.hpp"

#include <curl/curl.h>
#include <json-glib/json-glib.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace framerift {
namespace {

constexpr std::size_t maximum_body = 20 * 1024 * 1024;
constexpr const char* api_base = "https://www.steamgriddb.com/api/v2";

struct CurlRuntime {
    CurlRuntime() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("Could not initialize SteamGridDB networking");
        }
    }
    ~CurlRuntime() { curl_global_cleanup(); }
};

struct Response {
    std::string body;
};

std::size_t receive(char* data, std::size_t size, std::size_t count, void* context) noexcept {
    auto& response = *static_cast<Response*>(context);
    if (size != 0 && count > maximum_body / size) return 0;
    const auto bytes = size * count;
    if (bytes > maximum_body - response.body.size()) return 0;
    try {
        response.body.append(data, bytes);
        return bytes;
    } catch (...) {
        return 0;
    }
}

bool allowed_url(const std::string& url, bool api) {
    const std::unique_ptr<GUri, decltype(&g_uri_unref)> uri(
        g_uri_parse(url.c_str(), G_URI_FLAGS_NONE, nullptr), g_uri_unref);
    if (!uri) return false;
    const auto* scheme = g_uri_get_scheme(uri.get());
    const auto* host = g_uri_get_host(uri.get());
    const int port = g_uri_get_port(uri.get());
    if (!scheme || !host || std::string_view(scheme) != "https" ||
        g_uri_get_userinfo(uri.get()) || (port != -1 && port != 443)) return false;
    if (api) return std::string_view(host) == "www.steamgriddb.com";
    return std::string_view(host) == "cdn.steamgriddb.com" || std::string_view(host) == "cdn2.steamgriddb.com";
}

std::string request(const std::string& url, const std::string& api_key = {}) {
    const bool api = !api_key.empty();
    if (!allowed_url(url, api)) throw std::runtime_error("SteamGridDB returned an unsupported artwork URL");
    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("Could not initialize a SteamGridDB request");
    Response response;
    const auto set = [&](CURLoption option, auto value) {
        if (curl_easy_setopt(curl.get(), option, value) != CURLE_OK) {
            throw std::runtime_error("Could not configure a SteamGridDB request");
        }
    };
    set(CURLOPT_URL, url.c_str());
    set(CURLOPT_PROTOCOLS_STR, "https");
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_CONNECTTIMEOUT, 10L);
    set(CURLOPT_TIMEOUT, 30L);
    set(CURLOPT_ACCEPT_ENCODING, "");
    set(CURLOPT_USERAGENT, "FrameRift/0.1");
    set(CURLOPT_WRITEFUNCTION, receive);
    set(CURLOPT_WRITEDATA, &response);
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
    if (api) {
        const auto authorization = "Authorization: Bearer " + api_key;
        headers.reset(curl_slist_append(nullptr, authorization.c_str()));
        if (!headers) throw std::runtime_error("Could not configure SteamGridDB authentication");
        set(CURLOPT_HTTPHEADER, headers.get());
    }
    const auto result = curl_easy_perform(curl.get());
    if (result != CURLE_OK) {
        throw std::runtime_error(std::string("SteamGridDB download failed: ") + curl_easy_strerror(result));
    }
    long status = 0;
    if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK || status != 200) {
        throw std::runtime_error("SteamGridDB request returned HTTP " + std::to_string(status));
    }
    if (response.body.empty()) throw std::runtime_error("SteamGridDB returned an empty response");
    return std::move(response.body);
}

JsonNode* member(JsonObject* object, const char* name) {
    return json_object_has_member(object, name) ? json_object_get_member(object, name) : nullptr;
}

bool value_type(JsonNode* node, GType type) {
    return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == type;
}

const char* text(JsonObject* object, const char* name) {
    auto* node = member(object, name);
    return value_type(node, G_TYPE_STRING) ? json_node_get_string(node) : nullptr;
}

std::int64_t integer(JsonObject* object, const char* name) {
    auto* node = member(object, name);
    return value_type(node, G_TYPE_INT64) ? json_node_get_int(node) : 0;
}

using Parser = std::unique_ptr<JsonParser, decltype(&g_object_unref)>;

Parser api_response(const std::string& endpoint, const std::string& key) {
    const auto body = request(std::string(api_base) + endpoint, key);
    Parser parser(json_parser_new(), g_object_unref);
    if (!json_parser_load_from_data(parser.get(), body.data(), static_cast<gssize>(body.size()), nullptr)) {
        throw std::runtime_error("SteamGridDB returned invalid JSON");
    }
    auto* root = json_parser_get_root(parser.get());
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) throw std::runtime_error("SteamGridDB returned an invalid response");
    auto* success = member(json_node_get_object(root), "success");
    if (!value_type(success, G_TYPE_BOOLEAN) || !json_node_get_boolean(success)) {
        throw std::runtime_error("SteamGridDB could not complete the request");
    }
    return parser;
}

JsonNode* response_data(const Parser& parser) {
    return member(json_node_get_object(json_parser_get_root(parser.get())), "data");
}

struct Asset {
    std::int64_t id;
    std::string url;
    std::string extension;
};

std::optional<Asset> read_asset(JsonNode* node, std::string_view kind) {
    if (!node || !JSON_NODE_HOLDS_OBJECT(node)) return std::nullopt;
    auto* object = json_node_get_object(node);
    const auto id = integer(object, "id");
    const auto width = integer(object, "width");
    const auto height = integer(object, "height");
    const auto* url = text(object, "url");
    const auto* mime = text(object, "mime");
    if (id <= 0 || width <= 0 || height <= 0 || !url || !mime || !allowed_url(url, false)) return std::nullopt;
    for (const char* flag : {"nsfw", "humor", "animated"}) {
        auto* value = member(object, flag);
        if (value && (!value_type(value, G_TYPE_BOOLEAN) || json_node_get_boolean(value))) return std::nullopt;
    }
    if (kind == "cover" && (width != 600 || height != 900)) return std::nullopt;
    if (kind == "landscape" && !((width == 460 && height == 215) || (width == 920 && height == 430))) {
        return std::nullopt;
    }
    std::string extension;
    if (std::string_view(mime) == "image/png") extension = ".png";
    else if (std::string_view(mime) == "image/jpeg") extension = ".jpg";
    else if (std::string_view(mime) == "image/webp") extension = ".webp";
    else return std::nullopt;
    return Asset{id, url, std::move(extension)};
}

Asset choose_asset(const Parser& parser, std::string_view kind, std::int64_t preferred) {
    auto* data = response_data(parser);
    if (!data || !JSON_NODE_HOLDS_ARRAY(data)) throw std::runtime_error("SteamGridDB returned an invalid artwork list");
    auto* array = json_node_get_array(data);
    std::optional<Asset> first;
    for (guint index = 0; index < json_array_get_length(array); ++index) {
        auto asset = read_asset(json_array_get_element(array, index), kind);
        if (!asset) continue;
        if (asset->id == preferred) return *asset;
        if (!first) first = std::move(asset);
    }
    if (!first) throw std::runtime_error("SteamGridDB has no supported static " + std::string(kind) + " artwork");
    return *first;
}

std::string download_asset(const Asset& asset, const std::filesystem::path& directory, std::string_view kind) {
    const auto destination = directory / (std::string(kind) + "-" + std::to_string(asset.id) + asset.extension);
    if (std::filesystem::is_regular_file(destination) && std::filesystem::file_size(destination) > 0) {
        return destination.string();
    }
    const auto body = request(asset.url);
    const auto filename = destination.string();
    // GLib writes a temporary file and renames it atomically into the cache.
    if (!g_file_set_contents_full(filename.c_str(), body.data(), static_cast<gssize>(body.size()),
        G_FILE_SET_CONTENTS_CONSISTENT, 0644, nullptr)) {
        throw std::runtime_error("Could not save SteamGridDB artwork in the cache");
    }
    return destination.string();
}

struct DemoGame {
    std::int64_t id;
    std::array<std::int64_t, 4> preferred;
};

constexpr std::array demo_games{
    DemoGame{5454475, {483425, 475821, 114583, 114362}},
    DemoGame{5266059, {196423, 333073, 34289, 79153}},
    DemoGame{5274173, {538131, 300899, 73867, 76816}},
    DemoGame{5270663, {154729, 116445, 23612, 40859}},
    DemoGame{5261245, {134597, 296994, 33132, 15845}},
    DemoGame{5354206, {118820, 265687, 64076, 46042}},
};

} // namespace

std::vector<Game> download_demo_games(const std::string& api_key, const std::filesystem::path& artwork_directory) {
    if (api_key.empty() || api_key.find_first_of("\r\n") != std::string::npos) {
        throw std::runtime_error("A valid SteamGridDB API key is required");
    }
    static const CurlRuntime runtime;
    const auto directory = std::filesystem::absolute(artwork_directory).lexically_normal();
    std::filesystem::create_directories(directory);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::vector<Game> result;
    result.reserve(demo_games.size());
    for (std::size_t index = 0; index < demo_games.size(); ++index) {
        const auto& demo = demo_games[index];
        const auto id = std::to_string(demo.id);
        const auto metadata = api_response("/games/id/" + id, api_key);
        auto* data = response_data(metadata);
        if (!data || !JSON_NODE_HOLDS_OBJECT(data)) throw std::runtime_error("SteamGridDB returned invalid game metadata");
        auto* object = json_node_get_object(data);
        const auto* name = text(object, "name");
        if (integer(object, "id") != demo.id || !name || !*name) {
            throw std::runtime_error("SteamGridDB returned incomplete game metadata");
        }
        Game game;
        game.id = "steamgrid-" + id;
        game.steamgriddb_id = demo.id;
        game.title = name;
        game.subtitle = "SteamGridDB · Demo entry";
        game.source_url = "https://www.steamgriddb.com/game/" + id;
        game.is_demo = true;
        game.last_played = index < 5 ? now - static_cast<std::int64_t>(index + 1) * 7200 : 0;
        const auto portrait = api_response("/grids/game/" + id + "?dimensions=600x900&types=static&nsfw=false&humor=false", api_key);
        game.cover_path = download_asset(choose_asset(portrait, "cover", demo.preferred[0]), directory, "cover");
        const auto landscape = api_response("/grids/game/" + id + "?dimensions=460x215,920x430&types=static&nsfw=false&humor=false", api_key);
        game.landscape_path = download_asset(choose_asset(landscape, "landscape", demo.preferred[1]), directory, "landscape");
        const auto hero = api_response("/heroes/game/" + id + "?types=static&nsfw=false&humor=false", api_key);
        game.hero_path = download_asset(choose_asset(hero, "hero", demo.preferred[2]), directory, "hero");
        try {
            const auto logos = api_response("/logos/game/" + id + "?types=static&nsfw=false&humor=false", api_key);
            game.logo_path = download_asset(choose_asset(logos, "logo", demo.preferred[3]), directory, "logo");
        } catch (const std::runtime_error&) {
            // The UI can render the actual title when no usable logo is available.
        }
        result.push_back(std::move(game));
    }
    return result;
}

} // namespace framerift
