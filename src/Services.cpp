#include "Services.h"

#include "Credentials.h"
#include "FileUtilities.h"
#include "JsonUtilities.h"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace
{

bool IsValidEnvironmentVariableName(std::string_view const name)
{
    return !name.empty() && std::ranges::all_of(name, [](char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::string ReadString(json const& object, std::string const& service, char const* const key)
{
    auto const it = object.find(key);
    if (it == object.end())
    {
        return {};
    }
    if (!it->is_string())
    {
        throw std::runtime_error(std::format("Service '{}': '{}' must be a string", service, key));
    }
    return it->get<std::string>();
}

} // namespace

std::vector<ServiceDefinition> BuiltInServices()
{
    return {
        { .name = "llama.cpp", .endpoint = "http://127.0.0.1:8080/v1/chat/completions", .apiKeyEnv = {}, .model = {}, .allowInsecure = false },
        { .name = "ollama",    .endpoint = "http://127.0.0.1:11434/api/chat",           .apiKeyEnv = {}, .model = {}, .allowInsecure = false },
    };
}

std::filesystem::path GetServicesConfigPath()
{
#if _WIN32
    if (auto const appData = ReadEnvironmentVariable("APPDATA"); appData && !appData->empty())
    {
        return std::filesystem::path{ *appData } / "Kern" / "services.json";
    }
#else
    if (auto const xdg = ReadEnvironmentVariable("XDG_CONFIG_HOME"); xdg && !xdg->empty())
    {
        return std::filesystem::path{ *xdg } / "Kern" / "services.json";
    }
    if (auto const home = ReadEnvironmentVariable("HOME"); home && !home->empty())
    {
        return std::filesystem::path{ *home } / ".config" / "Kern" / "services.json";
    }
#endif
    return {};
}

std::vector<ServiceDefinition> ParseServicesConfig(std::string_view const jsonText)
{
    json const root = json::parse(jsonText, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        throw std::runtime_error("Not a valid JSON object");
    }

    for (auto const& [key, value] : root.items())
    {
        if (key != "services")
        {
            throw std::runtime_error(std::format("Unknown top-level key '{}'", key));
        }
    }

    auto const servicesIt = root.find("services");
    if (servicesIt == root.end() || !servicesIt->is_object())
    {
        throw std::runtime_error("Missing 'services' object");
    }

    std::vector<ServiceDefinition> result;
    for (auto const& [name, entry] : servicesIt->items())
    {
        if (name.empty())
        {
            throw std::runtime_error("Service names can't be empty");
        }
        if (!entry.is_object())
        {
            throw std::runtime_error(std::format("Service '{}' must be an object", name));
        }

        ServiceDefinition service{ .name = name, .endpoint = {}, .apiKeyEnv = {}, .model = {}, .allowInsecure = false };
        for (auto const& [key, value] : entry.items())
        {
            if (key == "apiKey")
            {
                // Catch the most dangerous mistake explicitly.
                throw std::runtime_error(std::format("Service '{}': API keys must not be stored in this file; use 'apiKeyEnv' to name an environment variable", name));
            }
            if (key != "endpoint" && key != "apiKeyEnv" && key != "model" && key != "allowInsecure")
            {
                throw std::runtime_error(std::format("Service '{}': unknown key '{}'", name, key));
            }
        }

        service.endpoint  = ReadString(entry, name, "endpoint");
        service.apiKeyEnv = ReadString(entry, name, "apiKeyEnv");
        service.model     = ReadString(entry, name, "model");

        if (service.endpoint.empty())
        {
            throw std::runtime_error(std::format("Service '{}': missing 'endpoint'", name));
        }
        if (!service.apiKeyEnv.empty() && !IsValidEnvironmentVariableName(service.apiKeyEnv))
        {
            throw std::runtime_error(std::format("Service '{}': 'apiKeyEnv' must be the NAME of an environment variable, not a key", name));
        }
        if (auto const it = entry.find("allowInsecure"); it != entry.end())
        {
            if (!it->is_boolean())
            {
                throw std::runtime_error(std::format("Service '{}': 'allowInsecure' must be true or false", name));
            }
            service.allowInsecure = it->get<bool>();
        }

        result.push_back(std::move(service));
    }
    return result;
}

std::vector<ServiceDefinition> LoadServices()
{
    std::vector<ServiceDefinition> services = BuiltInServices();

    auto const path = GetServicesConfigPath();
    if (path.empty() || !std::filesystem::is_regular_file(path))
    {
        return services;
    }

    try
    {
        for (auto& configured : ParseServicesConfig(RawReadTextFile(path)))
        {
            auto const existing = std::ranges::find(services, configured.name, &ServiceDefinition::name);
            if (existing != services.end())
            {
                *existing = std::move(configured);
            }
            else
            {
                services.push_back(std::move(configured));
            }
        }
    }
    catch (std::exception const& e)
    {
        throw std::runtime_error(std::format("Invalid services file {}: {}", path.string(), e.what()));
    }
    return services;
}

ServiceDefinition const& FindService(std::span<ServiceDefinition const> const services, std::string_view const name)
{
    if (auto const it = std::ranges::find(services, name, &ServiceDefinition::name); it != services.end())
    {
        return *it;
    }

    std::string available;
    for (auto const& service : services)
    {
        available += available.empty() ? "" : ", ";
        available += service.name;
    }
    throw std::runtime_error(std::format("Unknown service '{}'. Available services: {}", name, available));
}

Endpoint ResolveService(ServiceDefinition const& service, std::span<std::unique_ptr<CredentialSource> const> const credentialSources)
{
    Endpoint endpoint;
    try
    {
        endpoint = ParseEndpoint(service.endpoint);
    }
    catch (std::exception const& e)
    {
        throw std::runtime_error(std::format("Service '{}': invalid endpoint ({})", service.name, e.what()));
    }

    if (!endpoint.secure && !IsLoopbackHost(endpoint.host) && !service.allowInsecure)
    {
        throw std::runtime_error(std::format(
            "Service '{}': refusing to use unencrypted http:// to remote host '{}'. Use https://, or set \"allowInsecure\": true for this service if you trust the network",
            service.name, endpoint.host));
    }

    if (auto key = FindApiKey(service, credentialSources))
    {
        endpoint.apiKey = std::move(*key);
    }
    else if (!service.apiKeyEnv.empty())
    {
        throw std::runtime_error(std::format("Service '{}': no usable API key found. Set the environment variable {}", service.name, service.apiKeyEnv));
    }

    return endpoint;
}
