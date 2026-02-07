#include "mcp/model/types.hpp"

namespace mcp {

// ProtocolVersion static constants
const ProtocolVersion ProtocolVersion::V_2025_06_18{std::string("2025-06-18")};
const ProtocolVersion ProtocolVersion::V_2025_03_26{std::string("2025-03-26")};
const ProtocolVersion ProtocolVersion::V_2024_11_05{std::string("2024-11-05")};
const ProtocolVersion ProtocolVersion::LATEST{std::string("2025-03-26")};

// ErrorCode static constants
const ErrorCode ErrorCode::RESOURCE_NOT_FOUND{-32002};
const ErrorCode ErrorCode::INVALID_REQUEST{-32600};
const ErrorCode ErrorCode::METHOD_NOT_FOUND{-32601};
const ErrorCode ErrorCode::INVALID_PARAMS{-32602};
const ErrorCode ErrorCode::INTERNAL_ERROR{-32603};
const ErrorCode ErrorCode::PARSE_ERROR{-32700};

// RequestTimeout added for completeness
const ErrorCode ErrorCode::REQUEST_TIMEOUT{-32001};

} // namespace mcp
