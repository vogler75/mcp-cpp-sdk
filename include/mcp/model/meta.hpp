#pragma once

#include <any>
#include <memory>
#include <typeindex>
#include <unordered_map>

#include "mcp/model/types.hpp"

namespace mcp {

// =============================================================================
// Extensions (type-erased map, like http::Extensions)
// =============================================================================

class Extensions {
public:
    Extensions() = default;

    template <typename T>
    void insert(T value) {
        map_[std::type_index(typeid(T))] = std::make_shared<std::any>(std::move(value));
    }

    template <typename T>
    T* get() {
        auto it = map_.find(std::type_index(typeid(T)));
        if (it == map_.end()) return nullptr;
        return std::any_cast<T>(it->second.get());
    }

    template <typename T>
    const T* get() const {
        auto it = map_.find(std::type_index(typeid(T)));
        if (it == map_.end()) return nullptr;
        return std::any_cast<T>(it->second.get());
    }

    template <typename T>
    T& get_or_insert_default() {
        auto key = std::type_index(typeid(T));
        auto it = map_.find(key);
        if (it == map_.end()) {
            auto ptr = std::make_shared<std::any>(T{});
            map_[key] = ptr;
            return *std::any_cast<T>(ptr.get());
        }
        return *std::any_cast<T>(it->second.get());
    }

    template <typename T>
    bool contains() const {
        return map_.find(std::type_index(typeid(T))) != map_.end();
    }

    template <typename T>
    bool remove() {
        return map_.erase(std::type_index(typeid(T))) > 0;
    }

    bool empty() const { return map_.empty(); }
    size_t size() const { return map_.size(); }

private:
    std::unordered_map<std::type_index, std::shared_ptr<std::any>> map_;
};

// =============================================================================
// Meta (JSON object wrapper for _meta fields)
// =============================================================================

class Meta {
public:
    Meta() : data_(json::object()) {}
    explicit Meta(JsonObject obj) : data_(std::move(obj)) {}
    explicit Meta(json j) : data_(std::move(j)) {
        if (!data_.is_object()) data_ = json::object();
    }

    static const Meta& empty() {
        static const Meta e;
        return e;
    }

    // Progress token accessors
    std::optional<ProgressToken> get_progress_token() const {
        auto it = data_.find("progressToken");
        if (it == data_.end()) return std::nullopt;
        if (it->is_string()) {
            return ProgressToken(NumberOrString(it->get<std::string>()));
        }
        if (it->is_number_integer()) {
            return ProgressToken(NumberOrString(it->get<int64_t>()));
        }
        return std::nullopt;
    }

    void set_progress_token(const ProgressToken& token) {
        if (token.value.is_number()) {
            data_["progressToken"] = token.value.as_number();
        } else {
            data_["progressToken"] = token.value.as_string();
        }
    }

    void extend(const Meta& other) {
        if (other.data_.is_object()) {
            for (auto& [k, v] : other.data_.items()) {
                data_[k] = v;
            }
        }
    }

    // JSON object accessors
    json& operator[](const std::string& key) { return data_[key]; }
    const json& at(const std::string& key) const { return data_.at(key); }
    bool contains(const std::string& key) const { return data_.contains(key); }
    bool empty_object() const { return data_.empty(); }

    const json& data() const { return data_; }
    json& data() { return data_; }

    bool operator==(const Meta& other) const { return data_ == other.data_; }

    friend void to_json(json& j, const Meta& m) { j = m.data_; }
    friend void from_json(const json& j, Meta& m) {
        if (j.is_object()) {
            m.data_ = j;
        } else {
            m.data_ = json::object();
        }
    }

private:
    json data_;
};

// =============================================================================
// GetExtensions / GetMeta interfaces (via CRTP or virtual)
// =============================================================================

// We use a simpler approach in C++: just include extensions as a member
// and provide accessor helpers via a mixin template.

template <typename Derived>
struct HasExtensions {
    Extensions& extensions() {
        return static_cast<Derived*>(this)->extensions_;
    }
    const Extensions& extensions() const {
        return static_cast<const Derived*>(this)->extensions_;
    }
};

} // namespace mcp
