#pragma once

/// @file
/// Umbrella header for the MCP C++ SDK.
/// Include this single file to get all MCP types, transports, and handlers.

// Model types
#include "mcp/model/types.hpp"
#include "mcp/model/error.hpp"
#include "mcp/model/meta.hpp"
#include "mcp/model/resource.hpp"
#include "mcp/model/content.hpp"
#include "mcp/model/tool.hpp"
#include "mcp/model/prompt.hpp"
#include "mcp/model/capabilities.hpp"
#include "mcp/model/sampling.hpp"
#include "mcp/model/elicitation.hpp"
#include "mcp/model/task.hpp"
#include "mcp/model/logging.hpp"
#include "mcp/model/completion.hpp"
#include "mcp/model/roots.hpp"
#include "mcp/model/pagination.hpp"
#include "mcp/model/notifications.hpp"
#include "mcp/model/init.hpp"
#include "mcp/model/jsonrpc.hpp"
#include "mcp/model/unions.hpp"

// Service layer
#include "mcp/service/service_role.hpp"
#include "mcp/service/cancellation_token.hpp"
#include "mcp/service/context.hpp"
#include "mcp/service/peer.hpp"
#include "mcp/service/running_service.hpp"
#include "mcp/service/service.hpp"

// Transport layer
#include "mcp/transport/transport.hpp"
#include "mcp/transport/into_transport.hpp"
#include "mcp/transport/stdio_transport.hpp"
#ifdef MCP_CHILD_PROCESS_TRANSPORT
#include "mcp/transport/child_process_transport.hpp"
#endif
#include "mcp/transport/async_rw_transport.hpp"

// Handler layer
#include "mcp/handler/server_handler.hpp"
#include "mcp/handler/client_handler.hpp"
#include "mcp/handler/tool_router.hpp"
#include "mcp/handler/prompt_router.hpp"
#include "mcp/handler/context_parts.hpp"

// Macros
#include "mcp/macros/tool_macros.hpp"
#include "mcp/macros/prompt_macros.hpp"

// Task manager
#include "mcp/task_manager/operation_processor.hpp"

// HTTP transports (if enabled)
#ifdef MCP_HTTP_TRANSPORT
#include "mcp/transport/streamable_http_client.hpp"
#include "mcp/transport/streamable_http_server.hpp"
#endif

// Auth (OAuth 2.1)
#include "mcp/auth/auth.hpp"
