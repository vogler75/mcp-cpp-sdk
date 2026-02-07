#pragma once

#include <optional>
#include <type_traits>

#include "mcp/model/meta.hpp"
#include "mcp/model/types.hpp"
#include "mcp/service/cancellation_token.hpp"

namespace mcp {

// Forward declarations
template <typename R>
class Peer;

/// Parameters<T> wraps a deserialized parameters type.
/// This is used as an extractor in tool handlers.
template <typename T>
struct Parameters {
    T value;

    Parameters() = default;
    explicit Parameters(T v) : value(std::move(v)) {}

    T& operator*() { return value; }
    const T& operator*() const { return value; }
    T* operator->() { return &value; }
    const T* operator->() const { return &value; }
};

/// FromContextPart concept: types that can be extracted from a handler context.
///
/// In C++ we implement this as overloaded extract functions and templates
/// rather than Rust's FromContextPart trait.

/// Extract Parameters<T> from JSON arguments
template <typename T>
Parameters<T> extract_parameters(const std::optional<JsonObject>& args) {
    if (!args || args->empty()) {
        return Parameters<T>{T{}};
    }
    json j(*args);
    return Parameters<T>{j.get<T>()};
}

/// Extract a CancellationToken from a request context
inline CancellationToken extract_cancellation(const CancellationToken& token) {
    return token;
}

/// Extract Meta from Extensions
inline Meta extract_meta(const Extensions& ext) {
    const Meta* m = ext.get<Meta>();
    return m ? *m : Meta{};
}

} // namespace mcp
