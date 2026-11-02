#include "Credentials.h"

#include <cstdlib>

std::optional<std::string> ReadEnvironmentVariable(std::string const& name)
{
#if _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name.c_str()) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string result{ value };
    free(value);
    return result;
#else
    char const* const value = std::getenv(name.c_str());
    if (value == nullptr)
    {
        return std::nullopt;
    }
    return std::string{ value };
#endif
}

std::optional<std::string> EnvironmentCredentialSource::Lookup(ServiceDefinition const& service) const
{
    if (service.apiKeyEnv.empty())
    {
        return std::nullopt;
    }
    return m_getter(service.apiKeyEnv);
}

std::vector<std::unique_ptr<CredentialSource>> DefaultCredentialSources()
{
    std::vector<std::unique_ptr<CredentialSource>> sources;
    // Future: sources.push_back(std::make_unique<OsCredentialStoreSource>()); goes first, so it takes priority over the environment.
    sources.push_back(std::make_unique<EnvironmentCredentialSource>());
    return sources;
}

std::optional<std::string> SanitizeApiKey(std::string_view key)
{
    auto const isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!key.empty() && isSpace(key.front())) key.remove_prefix(1);
    while (!key.empty() && isSpace(key.back()))  key.remove_suffix(1);

    if (key.empty())
    {
        return std::nullopt;
    }
    for (char const c : key)
    {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
        {
            return std::nullopt;
        }
    }
    return std::string{ key };
}

std::optional<std::string> FindApiKey(ServiceDefinition const& service, std::span<std::unique_ptr<CredentialSource> const> const sources)
{
    for (auto const& source : sources)
    {
        if (auto const raw = source->Lookup(service))
        {
            if (auto key = SanitizeApiKey(*raw))
            {
                return key;
            }
        }
    }
    return std::nullopt;
}
