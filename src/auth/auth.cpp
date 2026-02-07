#include "mcp/auth/auth.hpp"
#include "mcp/model/types.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <spdlog/spdlog.h>

// SHA-256 implementation: use platform-native crypto where available
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#elif defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
// Fallback: minimal embedded SHA-256 for portability.
// On Linux with OpenSSL available, you may replace this with
// #include <openssl/sha.h> and link against -lcrypto.
namespace {

// Minimal SHA-256 implementation (FIPS 180-4) for platforms without
// CommonCrypto or BCrypt. Only used internally for PKCE challenges.
namespace sha256_impl {

static constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

std::vector<uint8_t> compute(const uint8_t* data, size_t len) {
    // Initial hash values
    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    // Pre-processing: add padding
    uint64_t bit_len = static_cast<uint64_t>(len) * 8;
    // Message needs to be padded to (len + 1 + padding + 8) % 64 == 0
    size_t padded_len = len + 1;
    while (padded_len % 64 != 56) {
        padded_len++;
    }
    padded_len += 8;

    std::vector<uint8_t> msg(padded_len, 0);
    std::memcpy(msg.data(), data, len);
    msg[len] = 0x80;

    // Append length in bits as big-endian 64-bit
    for (int i = 0; i < 8; i++) {
        msg[padded_len - 1 - i] = static_cast<uint8_t>(bit_len >> (i * 8));
    }

    // Process each 64-byte block
    for (size_t offset = 0; offset < padded_len; offset += 64) {
        uint32_t w[64];

        // Prepare message schedule
        for (int i = 0; i < 16; i++) {
            w[i] = (static_cast<uint32_t>(msg[offset + i * 4]) << 24)
                | (static_cast<uint32_t>(msg[offset + i * 4 + 1]) << 16)
                | (static_cast<uint32_t>(msg[offset + i * 4 + 2]) << 8)
                | (static_cast<uint32_t>(msg[offset + i * 4 + 3]));
        }
        for (int i = 16; i < 64; i++) {
            w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
        }

        // Initialize working variables
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        // Compression
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + sigma1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = sigma0(a) + maj(a, b, c);
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    // Produce the final hash (big-endian)
    std::vector<uint8_t> result(32);
    for (int i = 0; i < 8; i++) {
        result[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        result[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        result[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        result[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    return result;
}

} // namespace sha256_impl

} // anonymous namespace
#endif

namespace mcp {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

// =============================================================================
// OAuthMetadata serialization
// =============================================================================

void to_json(json& j, const OAuthMetadata& m) {
    j = json::object();
    j["issuer"] = m.issuer;
    j["authorization_endpoint"] = m.authorization_endpoint;
    j["token_endpoint"] = m.token_endpoint;
    detail::set_opt(j, "registration_endpoint", m.registration_endpoint);
    detail::set_opt(j, "revocation_endpoint", m.revocation_endpoint);
    detail::set_opt(j, "scopes_supported", m.scopes_supported);
    detail::set_opt(j, "response_types_supported", m.response_types_supported);
    detail::set_opt(j, "grant_types_supported", m.grant_types_supported);
    detail::set_opt(
        j, "code_challenge_methods_supported", m.code_challenge_methods_supported);
}

void from_json(const json& j, OAuthMetadata& m) {
    j.at("issuer").get_to(m.issuer);
    j.at("authorization_endpoint").get_to(m.authorization_endpoint);
    j.at("token_endpoint").get_to(m.token_endpoint);
    detail::get_opt(j, "registration_endpoint", m.registration_endpoint);
    detail::get_opt(j, "revocation_endpoint", m.revocation_endpoint);
    detail::get_opt(j, "scopes_supported", m.scopes_supported);
    detail::get_opt(j, "response_types_supported", m.response_types_supported);
    detail::get_opt(j, "grant_types_supported", m.grant_types_supported);
    detail::get_opt(
        j, "code_challenge_methods_supported", m.code_challenge_methods_supported);
}

// =============================================================================
// OAuthTokenResponse
// =============================================================================

bool OAuthTokenResponse::is_expired() const {
    if (!expires_in.has_value()) {
        // No expiry information: assume token is still valid
        return false;
    }
    auto elapsed = std::chrono::steady_clock::now() - obtained_at;
    auto elapsed_secs =
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    // Use a 30-second buffer so we refresh slightly before actual expiry
    return elapsed_secs >= (*expires_in - 30);
}

void to_json(json& j, const OAuthTokenResponse& t) {
    j = json::object();
    j["access_token"] = t.access_token;
    j["token_type"] = t.token_type;
    detail::set_opt(j, "expires_in", t.expires_in);
    detail::set_opt(j, "refresh_token", t.refresh_token);
    detail::set_opt(j, "scope", t.scope);
}

void from_json(const json& j, OAuthTokenResponse& t) {
    j.at("access_token").get_to(t.access_token);
    j.at("token_type").get_to(t.token_type);
    detail::get_opt(j, "expires_in", t.expires_in);
    detail::get_opt(j, "refresh_token", t.refresh_token);
    detail::get_opt(j, "scope", t.scope);
    // Reset obtained_at to now when deserializing
    t.obtained_at = std::chrono::steady_clock::now();
}

// =============================================================================
// StoredCredentials serialization
// =============================================================================

void to_json(json& j, const StoredCredentials& c) {
    j = json::object();
    j["client_id"] = c.client_id;
    detail::set_opt(j, "client_secret", c.client_secret);
    detail::set_opt(j, "token_response", c.token_response);
}

void from_json(const json& j, StoredCredentials& c) {
    j.at("client_id").get_to(c.client_id);
    detail::get_opt(j, "client_secret", c.client_secret);
    detail::get_opt(j, "token_response", c.token_response);
}

// =============================================================================
// InMemoryCredentialStore
// =============================================================================

std::optional<StoredCredentials> InMemoryCredentialStore::load() {
    return credentials_;
}

void InMemoryCredentialStore::save(const StoredCredentials& credentials) {
    credentials_ = credentials;
}

void InMemoryCredentialStore::clear() {
    credentials_.reset();
}

// =============================================================================
// InMemoryStateStore
// =============================================================================

void InMemoryStateStore::save(
    const std::string& csrf_token, const StoredAuthorizationState& state) {
    states_[csrf_token] = state;
}

std::optional<StoredAuthorizationState> InMemoryStateStore::load(
    const std::string& csrf_token) {
    auto it = states_.find(csrf_token);
    if (it == states_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void InMemoryStateStore::remove(const std::string& csrf_token) {
    states_.erase(csrf_token);
}

// =============================================================================
// Helper functions (static, private to AuthorizationManager)
// =============================================================================

std::string AuthorizationManager::generate_random_string(size_t length) {
    static constexpr char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static constexpr size_t charset_size = sizeof(charset) - 1;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, charset_size - 1);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result.push_back(charset[dist(gen)]);
    }
    return result;
}

std::string AuthorizationManager::url_encode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (unsigned char c : str) {
        // Keep unreserved characters (RFC 3986 section 2.3)
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::uppercase << std::setw(2)
                    << static_cast<int>(c) << std::nouppercase;
        }
    }
    return encoded.str();
}

std::string AuthorizationManager::base64url_encode(const std::vector<uint8_t>& data) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    size_t i = 0;
    size_t len = data.size();

    while (i < len) {
        uint32_t octet_a = i < len ? data[i++] : 0;
        uint32_t octet_b = i < len ? data[i++] : 0;
        uint32_t octet_c = i < len ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        size_t remaining = len - (i - 3);
        result.push_back(table[(triple >> 18) & 0x3F]);
        result.push_back(table[(triple >> 12) & 0x3F]);
        if (remaining > 1) {
            result.push_back(table[(triple >> 6) & 0x3F]);
        }
        if (remaining > 2) {
            result.push_back(table[triple & 0x3F]);
        }
    }

    // Convert from standard base64 to base64url: replace + with -, / with _
    for (auto& ch : result) {
        if (ch == '+') {
            ch = '-';
        } else if (ch == '/') {
            ch = '_';
        }
    }
    // No padding (omit '=' characters) per base64url spec
    return result;
}

std::vector<uint8_t> AuthorizationManager::sha256(const std::string& input) {
    std::vector<uint8_t> result(32);

#ifdef __APPLE__
    // macOS: use CommonCrypto
    CC_SHA256(
        reinterpret_cast<const void*>(input.data()),
        static_cast<CC_LONG>(input.size()),
        result.data());
#elif defined(_WIN32)
    // Windows: use BCrypt
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(
        hHash,
        reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
        static_cast<ULONG>(input.size()),
        0);
    BCryptFinishHash(hHash, result.data(), static_cast<ULONG>(result.size()), 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
#else
    // Portable fallback: embedded SHA-256 implementation
    auto hash = sha256_impl::compute(
        reinterpret_cast<const uint8_t*>(input.data()), input.size());
    result = std::move(hash);
#endif

    return result;
}

// =============================================================================
// PKCE
// =============================================================================

PkceVerifier PkceVerifier::generate() {
    // Generate 32 random bytes
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);

    std::vector<uint8_t> bytes(32);
    for (auto& b : bytes) {
        b = static_cast<uint8_t>(dist(gen));
    }

    PkceVerifier v;
    v.value = AuthorizationManager::base64url_encode(bytes);
    return v;
}

std::string PkceVerifier::challenge() const {
    auto hash = AuthorizationManager::sha256(value);
    return AuthorizationManager::base64url_encode(hash);
}

// =============================================================================
// AuthorizationManager
// =============================================================================

AuthorizationManager::AuthorizationManager(AuthorizationConfig config)
    : config_(std::move(config)) {
    if (!config_.credential_store) {
        spdlog::warn(
            "AuthorizationManager: no credential_store provided, "
            "using InMemoryCredentialStore");
        config_.credential_store = std::make_shared<InMemoryCredentialStore>();
    }
    if (!config_.state_store) {
        spdlog::warn(
            "AuthorizationManager: no state_store provided, "
            "using InMemoryStateStore");
        config_.state_store = std::make_shared<InMemoryStateStore>();
    }
}

// -----------------------------------------------------------------------------
// URL parsing helper (local to this translation unit)
// -----------------------------------------------------------------------------

namespace {

struct UrlParts {
    std::string scheme; // "http" or "https"
    std::string host;
    std::string port;
    std::string path; // includes leading '/'
};

UrlParts parse_server_url(const std::string& url) {
    UrlParts parts;

    // Extract scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        parts.scheme = "https";
        scheme_end = 0;
    } else {
        parts.scheme = url.substr(0, scheme_end);
        scheme_end += 3; // skip "://"
    }

    // Find path start
    auto path_start = url.find('/', scheme_end);
    std::string authority;
    if (path_start == std::string::npos) {
        authority = url.substr(scheme_end);
        parts.path = "/";
    } else {
        authority = url.substr(scheme_end, path_start - scheme_end);
        parts.path = url.substr(path_start);
    }

    // Split authority into host:port
    auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        parts.host = authority.substr(0, colon);
        parts.port = authority.substr(colon + 1);
    } else {
        parts.host = authority;
        parts.port = (parts.scheme == "https") ? "443" : "80";
    }

    return parts;
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// discover_metadata
// -----------------------------------------------------------------------------

asio::awaitable<OAuthMetadata> AuthorizationManager::discover_metadata() {
    spdlog::info("Discovering OAuth metadata from {}", config_.server_url);

    auto parts = parse_server_url(config_.server_url);

    // Build the list of well-known paths to try, per MCP spec:
    //   1. /.well-known/oauth-authorization-server/{path}
    //   2. /.well-known/openid-configuration
    //   3. /.well-known/oauth-authorization-server  (fallback, no path suffix)
    std::vector<std::string> well_known_paths;

    // Path-aware discovery: append the server path if non-trivial
    std::string server_path = parts.path;
    // Remove trailing slash for path suffix
    if (!server_path.empty() && server_path.back() == '/') {
        server_path.pop_back();
    }
    if (!server_path.empty() && server_path != "/") {
        well_known_paths.push_back(
            "/.well-known/oauth-authorization-server" + server_path);
    }
    well_known_paths.push_back("/.well-known/openid-configuration");
    well_known_paths.push_back("/.well-known/oauth-authorization-server");

    auto executor = co_await asio::this_coro::executor;

    for (const auto& path : well_known_paths) {
        spdlog::debug("Trying OAuth metadata endpoint: {}", path);

        try {
            // Resolve host
            tcp::resolver resolver(executor);
            auto results = co_await resolver.async_resolve(
                parts.host, parts.port, asio::use_awaitable);

            // Connect
            beast::tcp_stream stream(executor);
            co_await stream.async_connect(results, asio::use_awaitable);

            // Build GET request
            http::request<http::empty_body> req{http::verb::get, path, 11};
            req.set(http::field::host, parts.host);
            req.set(http::field::accept, "application/json");
            req.set(http::field::user_agent, "MCP-CPP-Client/0.1.0");

            // Send request
            co_await http::async_write(stream, req, asio::use_awaitable);

            // Read response
            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            co_await http::async_read(stream, buffer, res, asio::use_awaitable);

            // Gracefully close
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);

            if (res.result() == http::status::ok) {
                auto metadata = json::parse(res.body()).get<OAuthMetadata>();
                metadata_ = metadata;
                spdlog::info(
                    "Discovered OAuth metadata from {}: issuer={}",
                    path,
                    metadata.issuer);
                co_return metadata;
            }

            spdlog::debug(
                "OAuth metadata endpoint {} returned status {}",
                path,
                static_cast<int>(res.result_int()));
        } catch (const std::exception& e) {
            spdlog::debug(
                "OAuth metadata discovery failed for {}: {}", path, e.what());
        }
    }

    throw std::runtime_error(
        "Failed to discover OAuth metadata from any well-known endpoint");
}

// -----------------------------------------------------------------------------
// register_client (RFC 7591 Dynamic Client Registration)
// -----------------------------------------------------------------------------

asio::awaitable<std::string> AuthorizationManager::register_client(
    const OAuthMetadata& metadata) {
    if (!metadata.registration_endpoint.has_value()) {
        throw std::runtime_error(
            "OAuth metadata does not include a registration_endpoint");
    }

    spdlog::info(
        "Registering OAuth client at {}", *metadata.registration_endpoint);

    auto parts = parse_server_url(*metadata.registration_endpoint);

    auto executor = co_await asio::this_coro::executor;

    // Resolve host
    tcp::resolver resolver(executor);
    auto results = co_await resolver.async_resolve(
        parts.host, parts.port, asio::use_awaitable);

    // Connect
    beast::tcp_stream stream(executor);
    co_await stream.async_connect(results, asio::use_awaitable);

    // Build registration request body
    json reg_body = {
        {"client_name", config_.client_name},
        {"redirect_uris", json::array({config_.redirect_uri})},
        {"grant_types", json::array({"authorization_code", "refresh_token"})},
        {"response_types", json::array({"code"})},
        {"token_endpoint_auth_method", "none"},
    };

    if (!config_.scopes.empty()) {
        std::string scope_str;
        for (size_t i = 0; i < config_.scopes.size(); ++i) {
            if (i > 0) scope_str += " ";
            scope_str += config_.scopes[i];
        }
        reg_body["scope"] = scope_str;
    }

    std::string body_str = reg_body.dump();

    // Build POST request
    http::request<http::string_body> req{
        http::verb::post, parts.path, 11};
    req.set(http::field::host, parts.host);
    req.set(http::field::content_type, "application/json");
    req.set(http::field::accept, "application/json");
    req.set(http::field::user_agent, "MCP-CPP-Client/0.1.0");
    req.body() = body_str;
    req.prepare_payload();

    // Send request
    co_await http::async_write(stream, req, asio::use_awaitable);

    // Read response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(stream, buffer, res, asio::use_awaitable);

    // Gracefully close
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    if (res.result() != http::status::created
        && res.result() != http::status::ok) {
        throw std::runtime_error(
            "Dynamic client registration failed with status "
            + std::to_string(res.result_int()) + ": " + res.body());
    }

    auto response_json = json::parse(res.body());
    auto client_id = response_json.at("client_id").get<std::string>();

    std::optional<std::string> client_secret;
    if (response_json.contains("client_secret")
        && !response_json["client_secret"].is_null()) {
        client_secret = response_json["client_secret"].get<std::string>();
    }

    // Store credentials
    StoredCredentials creds;
    creds.client_id = client_id;
    creds.client_secret = client_secret;
    config_.credential_store->save(creds);

    spdlog::info("Registered OAuth client with id: {}", client_id);
    co_return client_id;
}

// -----------------------------------------------------------------------------
// get_authorization_url
// -----------------------------------------------------------------------------

AuthorizationUrlResult AuthorizationManager::get_authorization_url(
    const OAuthMetadata& metadata, const std::string& client_id) {
    // Generate PKCE verifier and challenge
    auto verifier = PkceVerifier::generate();
    auto challenge = verifier.challenge();

    // Generate CSRF token
    auto csrf_token = generate_random_string(32);

    // Store the authorization state
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    StoredAuthorizationState state;
    state.pkce_verifier = verifier.value;
    state.csrf_token = csrf_token;
    state.created_at = static_cast<uint64_t>(now);
    config_.state_store->save(csrf_token, state);

    // Build the authorization URL with query parameters
    std::ostringstream url;
    url << metadata.authorization_endpoint;
    url << "?response_type=code";
    url << "&client_id=" << url_encode(client_id);
    url << "&redirect_uri=" << url_encode(config_.redirect_uri);

    // Add scope if configured
    if (!config_.scopes.empty()) {
        std::string scope_str;
        for (size_t i = 0; i < config_.scopes.size(); ++i) {
            if (i > 0) scope_str += " ";
            scope_str += config_.scopes[i];
        }
        url << "&scope=" << url_encode(scope_str);
    }

    url << "&state=" << url_encode(csrf_token);
    url << "&code_challenge=" << url_encode(challenge);
    url << "&code_challenge_method=S256";

    spdlog::debug("Generated authorization URL for client {}", client_id);

    return AuthorizationUrlResult{
        .url = url.str(),
        .csrf_token = csrf_token,
    };
}

// -----------------------------------------------------------------------------
// exchange_code
// -----------------------------------------------------------------------------

asio::awaitable<OAuthTokenResponse> AuthorizationManager::exchange_code(
    const OAuthMetadata& metadata,
    const std::string& client_id,
    const std::string& code,
    const std::string& csrf_token) {
    spdlog::info("Exchanging authorization code for tokens");

    // Look up the stored PKCE verifier
    auto state_opt = config_.state_store->load(csrf_token);
    if (!state_opt.has_value()) {
        throw std::runtime_error(
            "No stored authorization state found for CSRF token: " + csrf_token);
    }
    auto& state = *state_opt;

    // Build the token request body (application/x-www-form-urlencoded)
    std::ostringstream body;
    body << "grant_type=authorization_code";
    body << "&code=" << url_encode(code);
    body << "&redirect_uri=" << url_encode(config_.redirect_uri);
    body << "&client_id=" << url_encode(client_id);
    body << "&code_verifier=" << url_encode(state.pkce_verifier);

    auto body_str = body.str();

    auto parts = parse_server_url(metadata.token_endpoint);
    auto executor = co_await asio::this_coro::executor;

    // Resolve host
    tcp::resolver resolver(executor);
    auto results = co_await resolver.async_resolve(
        parts.host, parts.port, asio::use_awaitable);

    // Connect
    beast::tcp_stream stream(executor);
    co_await stream.async_connect(results, asio::use_awaitable);

    // Build POST request
    http::request<http::string_body> req{
        http::verb::post, parts.path, 11};
    req.set(http::field::host, parts.host);
    req.set(
        http::field::content_type, "application/x-www-form-urlencoded");
    req.set(http::field::accept, "application/json");
    req.set(http::field::user_agent, "MCP-CPP-Client/0.1.0");
    req.body() = body_str;
    req.prepare_payload();

    // Send request
    co_await http::async_write(stream, req, asio::use_awaitable);

    // Read response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(stream, buffer, res, asio::use_awaitable);

    // Gracefully close
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    if (res.result() != http::status::ok) {
        throw std::runtime_error(
            "Token exchange failed with status "
            + std::to_string(res.result_int()) + ": " + res.body());
    }

    auto token_response = json::parse(res.body()).get<OAuthTokenResponse>();

    // Clean up the consumed state
    config_.state_store->remove(csrf_token);

    // Update stored credentials with the new token
    auto creds_opt = config_.credential_store->load();
    StoredCredentials creds;
    if (creds_opt.has_value()) {
        creds = *creds_opt;
    } else {
        creds.client_id = client_id;
    }
    creds.token_response = token_response;
    config_.credential_store->save(creds);

    spdlog::info("Successfully exchanged authorization code for tokens");
    co_return token_response;
}

// -----------------------------------------------------------------------------
// refresh_token
// -----------------------------------------------------------------------------

asio::awaitable<OAuthTokenResponse> AuthorizationManager::refresh_token(
    const OAuthMetadata& metadata,
    const std::string& client_id,
    const std::string& refresh_token_str) {
    spdlog::info("Refreshing access token");

    // Build the token request body (application/x-www-form-urlencoded)
    std::ostringstream body;
    body << "grant_type=refresh_token";
    body << "&refresh_token=" << url_encode(refresh_token_str);
    body << "&client_id=" << url_encode(client_id);

    auto body_str = body.str();

    auto parts = parse_server_url(metadata.token_endpoint);
    auto executor = co_await asio::this_coro::executor;

    // Resolve host
    tcp::resolver resolver(executor);
    auto results = co_await resolver.async_resolve(
        parts.host, parts.port, asio::use_awaitable);

    // Connect
    beast::tcp_stream stream(executor);
    co_await stream.async_connect(results, asio::use_awaitable);

    // Build POST request
    http::request<http::string_body> req{
        http::verb::post, parts.path, 11};
    req.set(http::field::host, parts.host);
    req.set(
        http::field::content_type, "application/x-www-form-urlencoded");
    req.set(http::field::accept, "application/json");
    req.set(http::field::user_agent, "MCP-CPP-Client/0.1.0");
    req.body() = body_str;
    req.prepare_payload();

    // Send request
    co_await http::async_write(stream, req, asio::use_awaitable);

    // Read response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(stream, buffer, res, asio::use_awaitable);

    // Gracefully close
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    if (res.result() != http::status::ok) {
        throw std::runtime_error(
            "Token refresh failed with status "
            + std::to_string(res.result_int()) + ": " + res.body());
    }

    auto token_response = json::parse(res.body()).get<OAuthTokenResponse>();

    // Update stored credentials with the refreshed token
    auto creds_opt = config_.credential_store->load();
    StoredCredentials creds;
    if (creds_opt.has_value()) {
        creds = *creds_opt;
    } else {
        creds.client_id = client_id;
    }
    creds.token_response = token_response;
    config_.credential_store->save(creds);

    spdlog::info("Successfully refreshed access token");
    co_return token_response;
}

// -----------------------------------------------------------------------------
// get_access_token / get_auth_header
// -----------------------------------------------------------------------------

std::optional<std::string> AuthorizationManager::get_access_token() {
    auto creds_opt = config_.credential_store->load();
    if (!creds_opt.has_value()) {
        return std::nullopt;
    }
    auto& creds = *creds_opt;

    if (!creds.token_response.has_value()) {
        return std::nullopt;
    }
    auto& token = *creds.token_response;

    if (token.is_expired()) {
        spdlog::debug("Access token is expired");
        return std::nullopt;
    }

    return token.access_token;
}

std::optional<std::string> AuthorizationManager::get_auth_header() {
    auto token = get_access_token();
    if (!token.has_value()) {
        return std::nullopt;
    }
    return "Bearer " + *token;
}

} // namespace mcp
