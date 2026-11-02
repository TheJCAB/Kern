#pragma once

#include "Services.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A place where API keys can be kept. Sources are consulted in order and the first hit wins.
// To add the OS credential store (Credential Manager / libsecret), implement this interface
// and add it to DefaultCredentialSources(); nothing else needs to change.
class CredentialSource
{
public:
    virtual ~CredentialSource() = default;

    // Short human-readable name, for diagnostics only.
    virtual std::string_view Name() const = 0;

    // Returns the key for the service, or nullopt if this source doesn't have one.
    // Implementations must never log or print the key.
    virtual std::optional<std::string> Lookup(ServiceDefinition const& service) const = 0;
};

// Value of an environment variable, or nullopt if unset.
std::optional<std::string> ReadEnvironmentVariable(std::string const& name);

// Reads the variable named by ServiceDefinition::apiKeyEnv.
class EnvironmentCredentialSource final : public CredentialSource
{
public:
    using Getter = std::function<std::optional<std::string>(std::string const&)>;

    // The getter can be replaced for testing.
    explicit EnvironmentCredentialSource(Getter getter = ReadEnvironmentVariable) : m_getter{ std::move(getter) } {}

    std::string_view Name() const override { return "environment variable"; }

    std::optional<std::string> Lookup(ServiceDefinition const& service) const override;

private:
    Getter m_getter;
};

// In priority order.
std::vector<std::unique_ptr<CredentialSource>> DefaultCredentialSources();

// Trims surrounding whitespace and rejects empty keys and keys with control characters
// (which could otherwise be used to inject HTTP headers). Returns nullopt if the key is unusable.
std::optional<std::string> SanitizeApiKey(std::string_view key);

// The first usable key found in the sources, or nullopt.
std::optional<std::string> FindApiKey(ServiceDefinition const& service, std::span<std::unique_ptr<CredentialSource> const> sources);
