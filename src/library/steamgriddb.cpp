// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "steamgriddb.hpp"

#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <libsecret/secret.h>

#include <array>
#include <chrono>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace framerift {
namespace {

constexpr std::size_t maximum_body = 64 * 1024 * 1024;
constexpr const char* api_base = "https://www.steamgriddb.com/api/v2";

bool valid_api_key(const std::string& key) {
    return !key.empty() && key.find_first_of("\r\n") == std::string::npos &&
        key.find('\0') == std::string::npos && g_utf8_validate(key.c_str(), -1, nullptr);
}

const SecretSchema api_key_schema = [] {
    SecretSchema schema{};
    schema.name = "io.github.framerift.SteamGridDB";
    schema.flags = SECRET_SCHEMA_NONE;
    schema.attributes[0] = {"provider", SECRET_SCHEMA_ATTRIBUTE_STRING};
    return schema;
}();

// A private context keeps the cancellation deadline independent of the UI loop.
std::string access_keyring(const std::string* key) {
    const std::unique_ptr<GMainContext, decltype(&g_main_context_unref)> context(
        g_main_context_new(), g_main_context_unref);
    const std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)> loop(
        g_main_loop_new(context.get(), false), g_main_loop_unref);
    const std::unique_ptr<GCancellable, decltype(&g_object_unref)> cancellable(
        g_cancellable_new(), g_object_unref);
    const std::unique_ptr<GSource, decltype(&g_source_unref)> timeout(
        g_timeout_source_new(10000), g_source_unref);
    struct Request {
        GMainLoop* loop;
        bool saving;
        bool saved = false;
        std::unique_ptr<gchar, decltype(&secret_password_free)> password{nullptr, secret_password_free};
        GError* error = nullptr;
    } request{loop.get(), key != nullptr};

    g_source_set_callback(timeout.get(), +[](gpointer data) noexcept -> gboolean {
        g_cancellable_cancel(static_cast<GCancellable*>(data));
        return G_SOURCE_REMOVE;
    }, cancellable.get(), nullptr);
    g_source_attach(timeout.get(), context.get());
    g_main_context_push_thread_default(context.get());
    const auto complete = +[](GObject*, GAsyncResult* result, gpointer data) noexcept {
        auto& request = *static_cast<Request*>(data);
        if (request.saving) request.saved = secret_password_store_finish(result, &request.error);
        else request.password.reset(secret_password_lookup_finish(result, &request.error));
        g_main_loop_quit(request.loop);
    };
    if (key) {
        secret_password_store(&api_key_schema, SECRET_COLLECTION_DEFAULT, "FrameRift SteamGridDB API key",
            key->c_str(), cancellable.get(), complete, &request, "provider", "steamgriddb", nullptr);
    } else {
        secret_password_lookup(&api_key_schema, cancellable.get(), complete, &request,
            "provider", "steamgriddb", nullptr);
    }
    g_main_loop_run(loop.get());
    g_main_context_pop_thread_default(context.get());
    g_source_destroy(timeout.get());
    const bool failed = request.error || (key && !request.saved);
    g_clear_error(&request.error);
    if (failed) {
        if (key) throw std::runtime_error("Could not save the SteamGridDB API key in the system keyring");
        return {};
    }
    return request.password ? request.password.get() : std::string{};
}

struct CurlRuntime {
    CurlRuntime() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("Could not initialize SteamGridDB networking");
        }
    }
    ~CurlRuntime() { curl_global_cleanup(); }
};

void initialize_network() {
    static const CurlRuntime runtime;
}

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

std::optional<Asset> read_asset(JsonNode* node, std::string_view kind, bool animated_hero = false) {
    if (!node || !JSON_NODE_HOLDS_OBJECT(node)) return std::nullopt;
    auto* object = json_node_get_object(node);
    const auto id = integer(object, "id");
    const auto width = integer(object, "width");
    const auto height = integer(object, "height");
    const auto* url = text(object, "url");
    const auto* mime = text(object, "mime");
    if (id <= 0 || width <= 0 || height <= 0 || !url || !mime || !allowed_url(url, false)) return std::nullopt;
    for (const char* flag : {"nsfw", "humor"}) {
        auto* value = member(object, flag);
        if (value && (!value_type(value, G_TYPE_BOOLEAN) || json_node_get_boolean(value))) return std::nullopt;
    }
    auto* animated = member(object, "animated");
    if (animated && (!value_type(animated, G_TYPE_BOOLEAN) || json_node_get_boolean(animated) != animated_hero)) {
        return std::nullopt;
    }
    if (animated_hero && (kind != "hero" || std::string_view(mime) != "image/webp")) return std::nullopt;
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

std::optional<Asset> choose_asset(const Parser& parser, std::string_view kind, std::int64_t preferred,
    bool animated_hero = false) {
    auto* data = response_data(parser);
    if (!data || !JSON_NODE_HOLDS_ARRAY(data)) throw std::runtime_error("SteamGridDB returned an invalid artwork list");
    auto* array = json_node_get_array(data);
    std::optional<Asset> first;
    for (guint index = 0; index < json_array_get_length(array); ++index) {
        auto asset = read_asset(json_array_get_element(array, index), kind, animated_hero);
        if (!asset) continue;
        if (asset->id == preferred) return *asset;
        if (!first) first = std::move(asset);
    }
    return first;
}

bool image_data(std::string_view data) {
    return data.starts_with(std::string_view("\x89PNG\r\n\x1a\n", 8)) ||
        data.starts_with(std::string_view("\xff\xd8\xff", 3)) ||
        (data.size() >= 12 && data.substr(0, 4) == "RIFF" && data.substr(8, 4) == "WEBP");
}

bool cached_artwork(const std::string& filename) {
    std::error_code error;
    if (filename.empty() || !std::filesystem::is_regular_file(filename, error)) return false;
    const auto size = std::filesystem::file_size(filename, error);
    if (error || size < 12 || size > maximum_body) return false;
    std::ifstream input(filename, std::ios::binary);
    std::array<char, 12> header{};
    input.read(header.data(), header.size());
    return input && image_data(std::string_view(header.data(), header.size()));
}

std::string download_asset(const Asset& asset, const std::filesystem::path& directory, std::string_view kind) {
    const auto destination = directory / (std::string(kind) + "-" + std::to_string(asset.id) + asset.extension);
    if (cached_artwork(destination.string())) return destination.string();
    const auto body = request(asset.url);
    if (!image_data(body)) throw std::runtime_error("SteamGridDB returned invalid artwork data");
    const auto filename = destination.string();
    // GLib writes a temporary file and renames it atomically into the cache.
    if (!g_file_set_contents_full(filename.c_str(), body.data(), static_cast<gssize>(body.size()),
        G_FILE_SET_CONTENTS_CONSISTENT, 0644, nullptr)) {
        throw std::runtime_error("Could not save SteamGridDB artwork in the cache");
    }
    return destination.string();
}

std::string normalized_title(const std::string& title) {
    if (!g_utf8_validate(title.c_str(), -1, nullptr)) return {};
    std::string cleaned;
    for (const auto* cursor = title.c_str(); *cursor; cursor = g_utf8_next_char(cursor)) {
        const auto character = g_utf8_get_char(cursor);
        if (character == 0x2122 || character == 0x00ae || character == 0x00a9) continue;
        cleaned.append(cursor, g_utf8_next_char(cursor) - cursor);
    }
    const std::unique_ptr<gchar, decltype(&g_free)> normalized(
        g_utf8_normalize(cleaned.c_str(), -1, G_NORMALIZE_ALL_COMPOSE), g_free);
    const std::unique_ptr<gchar, decltype(&g_free)> folded(
        normalized ? g_utf8_casefold(normalized.get(), -1) : nullptr, g_free);
    if (!folded) return {};
    std::string result;
    for (const auto* cursor = folded.get(); *cursor; cursor = g_utf8_next_char(cursor)) {
        const auto character = g_utf8_get_char(cursor);
        if (g_unichar_isalnum(character)) {
            if (!result.empty() && result.back() == ' ') {
                const auto* previous = g_utf8_find_prev_char(result.data(), result.data() + result.size() - 1);
                const auto before_space = previous ? g_utf8_get_char(previous) : 0;
                if ((g_unichar_isalpha(before_space) && g_unichar_isdigit(character)) ||
                    (g_unichar_isdigit(before_space) && g_unichar_isalpha(character))) result.pop_back();
            }
            result.append(cursor, g_utf8_next_char(cursor) - cursor);
        } else if (character != '\'' && character != 0x2019 && !result.empty() && result.back() != ' ') {
            result.push_back(' ');
        }
    }
    if (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

std::int64_t matching_game(const Parser& parser, const std::string& title) {
    auto* data = response_data(parser);
    if (!data || !JSON_NODE_HOLDS_ARRAY(data)) throw std::runtime_error("SteamGridDB returned an invalid game list");
    const auto expected = normalized_title(title);
    auto* array = json_node_get_array(data);
    std::int64_t matched = 0;
    for (guint index = 0; index < json_array_get_length(array); ++index) {
        auto* node = json_array_get_element(array, index);
        if (!node || !JSON_NODE_HOLDS_OBJECT(node)) continue;
        auto* object = json_node_get_object(node);
        const auto id = integer(object, "id");
        const auto* name = text(object, "name");
        if (id <= 0 || !name || expected.empty() || normalized_title(name) != expected) continue;
        if (matched && matched != id) throw std::runtime_error("SteamGridDB has several games with the same title");
        matched = id;
    }
    return matched;
}

std::int64_t search_game(const std::string& title, const std::string& key) {
    const auto cleaned = normalized_title(title);
    if (cleaned.empty()) throw std::runtime_error("The game title cannot be searched");
    for (const auto& query : {cleaned, title}) {
        const std::unique_ptr<gchar, decltype(&g_free)> escaped(
            g_uri_escape_string(query.c_str(), nullptr, false), g_free);
        if (!escaped) throw std::runtime_error("Could not prepare the game search");
        const auto response = api_response("/search/autocomplete/" + std::string(escaped.get()), key);
        if (const auto id = matching_game(response, title)) return id;
        if (title == cleaned) break;
    }
    throw std::runtime_error("SteamGridDB has no exact title match");
}

struct ArtworkKind {
    const char* name;
    const char* endpoint;
    const char* filters;
    std::string Game::* path;
};

constexpr std::array artwork_kinds{
    ArtworkKind{"cover", "grids", "dimensions=600x900&", &Game::cover_path},
    ArtworkKind{"landscape", "grids", "dimensions=460x215,920x430&", &Game::landscape_path},
    ArtworkKind{"hero", "heroes", "", &Game::hero_path},
    ArtworkKind{"logo", "logos", "", &Game::logo_path},
};

void load_artwork(Game& game, const std::string& key, const std::filesystem::path& directory,
    const std::array<std::int64_t, 4>& preferred, std::vector<std::string>* warnings,
    bool refresh_heroes = false) {
    const auto id = std::to_string(game.steamgriddb_id);
    game.source_url = "https://www.steamgriddb.com/game/" + id;
    for (std::size_t index = 0; index < artwork_kinds.size(); ++index) {
        const auto& kind = artwork_kinds[index];
        auto& path = game.*kind.path;
        const bool hero = kind.path == &Game::hero_path;
        const bool cached = cached_artwork(path);
        if (cached && !(hero && refresh_heroes)) continue;
        if (!cached) path.clear();
        try {
            const auto response = api_response("/" + std::string(kind.endpoint) + "/game/" + id + "?" +
                kind.filters + "types=" + (hero ? "animated" : "static") + "&nsfw=false&humor=false", key);
            auto asset = choose_asset(response, kind.name, preferred[index], hero);
            if (!asset && hero) {
                if (cached) continue;
                const auto fallback = api_response("/heroes/game/" + id + "?types=static&nsfw=false&humor=false", key);
                asset = choose_asset(fallback, kind.name, preferred[index]);
            }
            if (!asset) throw std::runtime_error("SteamGridDB has no supported static " + std::string(kind.name) + " artwork");
            path = download_asset(*asset, directory, kind.name);
        } catch (const std::runtime_error& error) {
            if (warnings) warnings->push_back(game.title + " (" + kind.name + "): " + error.what());
            else if (index != 3) throw;
        }
    }
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

std::string read_steamgrid_api_key() {
    if (const auto* key = g_getenv("STEAMGRIDDB_API_KEY"); key && *key) {
        return valid_api_key(key) ? key : std::string{};
    }
    auto key = access_keyring(nullptr);
    return valid_api_key(key) ? key : std::string{};
}

void save_steamgrid_api_key(const std::string& api_key) {
    if (!valid_api_key(api_key)) throw std::runtime_error("Enter a valid SteamGridDB API key");
    access_keyring(&api_key);
}

std::vector<Game> download_demo_games(const std::string& api_key, const std::filesystem::path& artwork_directory) {
    if (!valid_api_key(api_key)) {
        throw std::runtime_error("A valid SteamGridDB API key is required");
    }
    initialize_network();
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
        game.is_demo = true;
        game.last_played = index < 5 ? now - static_cast<std::int64_t>(index + 1) * 7200 : 0;
        load_artwork(game, api_key, directory, demo.preferred, nullptr);
        result.push_back(std::move(game));
    }
    return result;
}

std::vector<Game> download_game_artwork(const std::vector<Game>& games, const std::string& api_key,
    const std::filesystem::path& artwork_directory, std::vector<std::string>& warnings, bool refresh_heroes) {
    auto result = games;
    bool needs_download = refresh_heroes && !result.empty();
    for (auto& game : result) {
        for (const auto& kind : artwork_kinds) {
            auto& path = game.*kind.path;
            if (cached_artwork(path)) continue;
            path.clear();
            needs_download = true;
        }
    }
    if (!needs_download) return result;
    if (!valid_api_key(api_key)) {
        warnings.push_back("A valid SteamGridDB API key is required to load or refresh artwork");
        return result;
    }
    initialize_network();
    const auto directory = std::filesystem::absolute(artwork_directory).lexically_normal();
    for (auto& game : result) {
        bool complete = true;
        for (const auto& kind : artwork_kinds) complete = complete && cached_artwork(game.*kind.path);
        if (complete && !refresh_heroes) continue;
        try {
            if (game.id.empty() || game.id.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos) {
                throw std::runtime_error("The game ID is not suitable for an artwork cache directory");
            }
            if (game.steamgriddb_id <= 0) game.steamgriddb_id = search_game(game.title, api_key);
            const auto game_directory = directory / game.id;
            std::filesystem::create_directories(game_directory);
            load_artwork(game, api_key, game_directory, {}, &warnings, refresh_heroes);
        } catch (const std::runtime_error& error) {
            warnings.push_back(game.title + ": " + error.what());
        }
    }
    return result;
}

} // namespace framerift
