#pragma once

#include <optional>
#include <string>

#include "mcp/model/types.hpp"
#include "mcp/model/meta.hpp"
#include "mcp/model/capabilities.hpp"

namespace mcp {

// =============================================================================
// InitializeRequestParams
// =============================================================================

struct InitializeRequestParams {
    std::optional<Meta> meta;
    ProtocolVersion protocol_version;
    ClientCapabilities capabilities;
    Implementation client_info;

    InitializeRequestParams()
        : protocol_version(ProtocolVersion::LATEST)
        , client_info(Implementation::from_build_env()) {}

    InitializeRequestParams(
        ProtocolVersion pv,
        ClientCapabilities caps,
        Implementation info
    )
        : protocol_version(std::move(pv))
        , capabilities(std::move(caps))
        , client_info(std::move(info)) {}

    bool operator==(const InitializeRequestParams& other) const {
        return meta == other.meta && protocol_version == other.protocol_version
            && capabilities == other.capabilities && client_info == other.client_info;
    }

    friend void to_json(json& j, const InitializeRequestParams& p) {
        j = json{
            {"protocolVersion", p.protocol_version},
            {"capabilities", p.capabilities},
            {"clientInfo", p.client_info}
        };
        if (p.meta.has_value()) {
            j["_meta"] = *p.meta;
        }
    }

    friend void from_json(const json& j, InitializeRequestParams& p) {
        j.at("protocolVersion").get_to(p.protocol_version);
        j.at("capabilities").get_to(p.capabilities);
        j.at("clientInfo").get_to(p.client_info);
        if (j.contains("_meta") && !j["_meta"].is_null()) {
            p.meta = j["_meta"].get<Meta>();
        }
    }
};

// =============================================================================
// InitializeResult
// =============================================================================

struct InitializeResult {
    ProtocolVersion protocol_version;
    ServerCapabilities capabilities;
    Implementation server_info;
    std::optional<std::string> instructions;

    InitializeResult()
        : protocol_version(ProtocolVersion::LATEST)
        , server_info(Implementation::from_build_env()) {}

    InitializeResult(
        ProtocolVersion pv,
        ServerCapabilities caps,
        Implementation info,
        std::optional<std::string> instr = std::nullopt
    )
        : protocol_version(std::move(pv))
        , capabilities(std::move(caps))
        , server_info(std::move(info))
        , instructions(std::move(instr)) {}

    bool operator==(const InitializeResult& other) const {
        return protocol_version == other.protocol_version
            && capabilities == other.capabilities
            && server_info == other.server_info
            && instructions == other.instructions;
    }

    friend void to_json(json& j, const InitializeResult& r) {
        j = json{
            {"protocolVersion", r.protocol_version},
            {"capabilities", r.capabilities},
            {"serverInfo", r.server_info}
        };
        detail::set_opt(j, "instructions", r.instructions);
    }

    friend void from_json(const json& j, InitializeResult& r) {
        j.at("protocolVersion").get_to(r.protocol_version);
        j.at("capabilities").get_to(r.capabilities);
        j.at("serverInfo").get_to(r.server_info);
        detail::get_opt(j, "instructions", r.instructions);
    }
};

// =============================================================================
// Type aliases
// =============================================================================

using ServerInfo = InitializeResult;
using ClientInfo = InitializeRequestParams;

} // namespace mcp
