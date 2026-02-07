#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

namespace mcp {

namespace asio = boost::asio;
using json = nlohmann::json;

// =============================================================================
// OAuth Metadata (RFC 8414)
// =============================================================================

/// OAuth Authorization Server Metadata as defined in RFC 8414.
/// Discovered via well-known endpoints from the MCP server URL.
struct OAuthMetadata {
    std::string issuer;
    std::string authorization_endpoint;
    std::string token_endpoint;
    std::optional<std::string> registration_endpoint;
    std::optional<std::string> revocation_endpoint;
    std::optional<std::vector<std::string>> scopes_supported;
    std::optional<std::vector<std::string>> response_types_supported;
    std::optional<std::vector<std::string>> grant_types_supported;
    std::optional<std::vector<std::string>> code_challenge_methods_supported;

    friend void to_json(json& j, const OAuthMetadata& m);
    friend void from_json(const json& j, OAuthMetadata& m);
};

// =============================================================================
// Token Types
// =============================================================================

/// OAuth Token Response as returned from the token endpoint.
struct OAuthTokenResponse {
    std::string access_token;
    std::string token_type;
    std::optional<int64_t> expires_in;
    std::optional<std::string> refresh_token;
    std::optional<std::string> scope;

    /// Time when the token was obtained
    std::chrono::steady_clock::time_point obtained_at = std::chrono::steady_clock::now();

    /// Check if the token is expired (with a 30-second safety buffer).
    /// Returns false if no expires_in was provided (assumes non-expiring).
    bool is_expired() const;

    friend void to_json(json& j, const OAuthTokenResponse& t);
    friend void from_json(const json& j, OAuthTokenResponse& t);
};

// =============================================================================
// PKCE (Proof Key for Code Exchange)
// =============================================================================

/// PKCE code verifier and challenge generation (RFC 7636).
/// Uses the S256 challenge method with a 32-byte random verifier.
struct PkceVerifier {
    std::string value;

    /// Generate a cryptographically random PKCE code verifier.
    /// Produces 32 random bytes, base64url-encoded (no padding).
    static PkceVerifier generate();

    /// Compute the S256 code challenge from this verifier.
    /// Returns SHA-256(verifier) base64url-encoded (no padding).
    std::string challenge() const;
};

// =============================================================================
// Credential Storage
// =============================================================================

/// Persisted OAuth credentials: client registration and tokens.
struct StoredCredentials {
    std::string client_id;
    std::optional<std::string> client_secret;
    std::optional<OAuthTokenResponse> token_response;

    friend void to_json(json& j, const StoredCredentials& c);
    friend void from_json(const json& j, StoredCredentials& c);
};

/// Abstract credential store for persisting OAuth client credentials and tokens.
class CredentialStore {
public:
    virtual ~CredentialStore() = default;

    /// Load previously stored credentials, or nullopt if none exist.
    virtual std::optional<StoredCredentials> load() = 0;

    /// Save credentials (overwrites any previously stored value).
    virtual void save(const StoredCredentials& credentials) = 0;

    /// Clear all stored credentials.
    virtual void clear() = 0;
};

/// In-memory credential store (not persisted across process restarts).
class InMemoryCredentialStore : public CredentialStore {
public:
    std::optional<StoredCredentials> load() override;
    void save(const StoredCredentials& credentials) override;
    void clear() override;

private:
    std::optional<StoredCredentials> credentials_;
};

// =============================================================================
// State Storage (PKCE state)
// =============================================================================

/// Authorization state stored during the PKCE flow, keyed by CSRF token.
struct StoredAuthorizationState {
    std::string pkce_verifier;
    std::string csrf_token;
    uint64_t created_at; // Unix timestamp (seconds since epoch)
};

/// Abstract state store for tracking in-flight authorization requests.
class StateStore {
public:
    virtual ~StateStore() = default;

    /// Save authorization state, keyed by CSRF token.
    virtual void save(
        const std::string& csrf_token, const StoredAuthorizationState& state) = 0;

    /// Load authorization state for the given CSRF token.
    virtual std::optional<StoredAuthorizationState> load(const std::string& csrf_token) = 0;

    /// Remove authorization state after it has been consumed.
    virtual void remove(const std::string& csrf_token) = 0;
};

/// In-memory state store (not persisted across process restarts).
class InMemoryStateStore : public StateStore {
public:
    void save(
        const std::string& csrf_token, const StoredAuthorizationState& state) override;
    std::optional<StoredAuthorizationState> load(const std::string& csrf_token) override;
    void remove(const std::string& csrf_token) override;

private:
    std::unordered_map<std::string, StoredAuthorizationState> states_;
};

// =============================================================================
// Authorization Manager
// =============================================================================

/// Configuration for the OAuth 2.1 AuthorizationManager.
struct AuthorizationConfig {
    /// The MCP server URL (used for metadata discovery).
    std::string server_url;

    /// Client name for dynamic registration.
    std::string client_name = "MCP C++ Client";

    /// Redirect URI for the authorization callback.
    std::string redirect_uri = "http://localhost:8888/callback";

    /// Requested OAuth scopes.
    std::vector<std::string> scopes;

    /// Credential store (required).
    std::shared_ptr<CredentialStore> credential_store;

    /// State store (required).
    std::shared_ptr<StateStore> state_store;
};

/// Result of authorization URL generation (returned by get_authorization_url).
struct AuthorizationUrlResult {
    /// The full authorization URL the user should open in a browser.
    std::string url;

    /// The CSRF token that ties the callback to this request.
    std::string csrf_token;
};

/// OAuth 2.1 Authorization Manager for the MCP protocol.
///
/// Handles the complete OAuth 2.1 authorization code flow with PKCE:
///   1. Discover OAuth metadata from the server (RFC 8414)
///   2. Register client dynamically (RFC 7591) if needed
///   3. Generate authorization URL with PKCE challenge
///   4. Exchange authorization code for tokens
///   5. Refresh tokens when they expire
class AuthorizationManager {
public:
    explicit AuthorizationManager(AuthorizationConfig config);

    /// Discover OAuth metadata from the server.
    ///
    /// Tries these well-known endpoints in order:
    ///   1. /.well-known/oauth-authorization-server/{path}
    ///   2. /.well-known/openid-configuration
    ///   3. /.well-known/oauth-authorization-server (fallback)
    asio::awaitable<OAuthMetadata> discover_metadata();

    /// Register the client dynamically with the authorization server.
    /// Requires that metadata.registration_endpoint is present.
    /// Returns the assigned client_id.
    asio::awaitable<std::string> register_client(const OAuthMetadata& metadata);

    /// Generate an authorization URL for the user to visit.
    /// Creates a PKCE verifier/challenge pair and CSRF token, stores the
    /// state, and returns the URL plus the CSRF token.
    AuthorizationUrlResult get_authorization_url(
        const OAuthMetadata& metadata, const std::string& client_id);

    /// Exchange an authorization code for tokens.
    /// Looks up the PKCE verifier from state_store using the csrf_token.
    asio::awaitable<OAuthTokenResponse> exchange_code(
        const OAuthMetadata& metadata,
        const std::string& client_id,
        const std::string& code,
        const std::string& csrf_token);

    /// Refresh an expired access token using a refresh token.
    asio::awaitable<OAuthTokenResponse> refresh_token(
        const OAuthMetadata& metadata,
        const std::string& client_id,
        const std::string& refresh_token_str);

    /// Get the current access token if authenticated and not expired.
    /// Does NOT automatically refresh; returns nullopt if expired or absent.
    std::optional<std::string> get_access_token();

    /// Get the full Authorization header value (e.g., "Bearer <token>").
    /// Returns nullopt if no valid token is available.
    std::optional<std::string> get_auth_header();

private:
    friend struct PkceVerifier;

    AuthorizationConfig config_;
    std::optional<OAuthMetadata> metadata_;

    /// Generate a random alphanumeric string of the given length.
    static std::string generate_random_string(size_t length);

    /// Percent-encode a string for use in URL query parameters.
    static std::string url_encode(const std::string& str);

    /// Base64url-encode a byte buffer (no padding, URL-safe alphabet).
    static std::string base64url_encode(const std::vector<uint8_t>& data);

    /// Compute SHA-256 hash of the input string.
    static std::vector<uint8_t> sha256(const std::string& input);
};

} // namespace mcp
