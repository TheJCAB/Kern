#pragma once

#include <nlohmann/json.hpp>

using json = nlohmann::json;

template <typename T>
auto GetOrDefault(json const& j, std::string_view key, T const& fallback)
    -> decltype(j.value(key, fallback))
{
    if (auto it = j.find(key); it != j.end() && !it->is_null())
    {
        return it->get<decltype(j.value(key, fallback))>();
    }
    return fallback;
}
