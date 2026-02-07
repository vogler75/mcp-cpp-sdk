#pragma once

#include <stdexcept>
#include <string>

#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// McpError
// =============================================================================

/// Exception type wrapping an MCP ErrorData.
///
/// Inherits from std::runtime_error and carries the full ErrorData
/// so callers can inspect the code, message, and optional data payload.
class McpError : public std::runtime_error {
public:
    explicit McpError(ErrorData error)
        : std::runtime_error(error.message), error_(std::move(error)) {}

    McpError(ErrorCode code, std::string message, std::optional<json> data = std::nullopt)
        : McpError(ErrorData(code, std::move(message), std::move(data))) {}

    const char* what() const noexcept override { return error_.message.c_str(); }

    const ErrorData& error_data() const { return error_; }

private:
    ErrorData error_;
};

} // namespace mcp
