#pragma once

#include "Endpoint.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A service as declared by the user (or built in). See README for the config file format.
struct ServiceDefinition
{
    std::string name;
    std::string endpoint;           // http:// or https:// URL of the chat completions API
    std::string apiKeyEnv;          // name of the environment variable that holds the API key; empty if none
    std::string model;              // default model name; empty if unspecified
    bool        allowInsecure = false; // permits plain http to a non-loopback host
    unsigned    maxTokens = 0;         // per-response output limit sent as max_tokens; 0 = don't send one
};

class CredentialSource;

// Services that exist without any configuration (local servers, no API key).
std::vector<ServiceDefinition> BuiltInServices();

// Location of the user-level services file, e.g. ~/.config/Kern/services.json. Empty if it can't be determined.
std::filesystem::path GetServicesConfigPath();

// Throws std::runtime_error with a descriptive message if the JSON is malformed or invalid.
std::vector<ServiceDefinition> ParseServicesConfig(std::string_view jsonText);

// Built-in services plus those in the config file (if it exists). Config entries replace built-ins of the same name.
std::vector<ServiceDefinition> LoadServices();

// Throws std::runtime_error if there's no such service (the message lists the available ones).
ServiceDefinition const& FindService(std::span<ServiceDefinition const> services, std::string_view name);

// Validates the service, looks up its API key in the given sources (in order) and builds the endpoint to use.
// Throws std::runtime_error if the URL is invalid, a required key can't be found,
// or the endpoint is plain http to a non-loopback host that isn't marked allowInsecure.
Endpoint ResolveService(ServiceDefinition const& service, std::span<std::unique_ptr<CredentialSource> const> credentialSources);
