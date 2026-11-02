#include "Credentials.h"
#include "Endpoint.h"
#include "Services.h"

#include "TestUtilities.h"

#include <map>
#include <print>
#include <stdexcept>
#include <string>

namespace
{

template <typename F>
bool Throws(F&& f)
{
    try
    {
        f();
    }
    catch (std::exception const&)
    {
        return true;
    }
    return false;
}

std::vector<std::unique_ptr<CredentialSource>> FakeEnvironment(std::map<std::string, std::string> variables)
{
    std::vector<std::unique_ptr<CredentialSource>> sources;
    sources.push_back(std::make_unique<EnvironmentCredentialSource>(
        [variables = std::move(variables)](std::string const& name) -> std::optional<std::string>
        {
            auto const it = variables.find(name);
            if (it == variables.end()) return std::nullopt;
            return it->second;
        }));
    return sources;
}

} // namespace

int main()
{
    std::println("Test ParseEndpoint");
    {
        auto const http = ParseEndpoint("http://127.0.0.1:8080/v1/chat/completions");
        ExpectEqual(http.host, "127.0.0.1", "host");
        ExpectEqual(http.port, 8080u, "port");
        ExpectEqual(http.path, "/v1/chat/completions", "path");
        Expect(!http.secure, "http is not secure");

        auto const https = ParseEndpoint("https://api.example.com/v1/chat/completions");
        Expect(https.secure, "https is secure");
        ExpectEqual(https.port, 443u, "https default port");

        ExpectEqual(ParseEndpoint("http://localhost").path, "/", "default path");
        ExpectEqual(ParseEndpoint("http://localhost").port, 80u, "http default port");

        auto const v6 = ParseEndpoint("http://[::1]:9000/x");
        ExpectEqual(v6.host, "::1", "IPv6 host");
        ExpectEqual(v6.port, 9000u, "IPv6 port");

        Expect(Throws([] { ParseEndpoint("ftp://example.com"); }), "bad scheme");
        Expect(Throws([] { ParseEndpoint("https://user:pass@example.com/"); }), "credentials in URL");
        Expect(Throws([] { ParseEndpoint("http://example.com:99999/"); }), "port out of range");
        Expect(Throws([] { ParseEndpoint("http://example.com:abc/"); }), "port not a number");
        Expect(Throws([] { ParseEndpoint("http:///path"); }), "missing host");
    }

    std::println("Test IsLoopbackHost");
    Expect(IsLoopbackHost("localhost"), "localhost");
    Expect(IsLoopbackHost("127.0.0.1"), "127.0.0.1");
    Expect(IsLoopbackHost("::1"), "::1");
    Expect(!IsLoopbackHost("127.evil.com"), "lookalike hostname");
    Expect(!IsLoopbackHost("192.168.1.5"), "LAN address");

    std::println("Test ParseServicesConfig");
    {
        auto const services = ParseServicesConfig(R"({
            "services": {
                "openai": { "endpoint": "https://api.openai.com/v1/chat/completions", "apiKeyEnv": "OPENAI_API_KEY", "model": "gpt" },
                "lan":    { "endpoint": "http://192.168.1.5:8080/v1/chat/completions", "allowInsecure": true }
            }
        })");
        ExpectEqual(services.size(), 2u, "service count");
        ExpectEqual(services[0].name, "lan", "services are ordered by name");
        Expect(services[0].allowInsecure, "allowInsecure parsed");
        ExpectEqual(services[1].apiKeyEnv, "OPENAI_API_KEY", "apiKeyEnv parsed");
        ExpectEqual(services[1].model, "gpt", "model parsed");

        Expect(Throws([] { ParseServicesConfig("not json"); }), "invalid JSON");
        Expect(Throws([] { ParseServicesConfig(R"({"services":{"a":{}}})"); }), "missing endpoint");
        Expect(Throws([] { ParseServicesConfig(R"({"services":{"a":{"endpoint":"https://x","apiKey":"sk-123"}}})"); }), "inline key rejected");
        Expect(Throws([] { ParseServicesConfig(R"({"services":{"a":{"endpoint":"https://x","apiKeyEnv":"sk-123abc"}}})"); }), "key in place of variable name");
        Expect(Throws([] { ParseServicesConfig(R"({"services":{"a":{"endpoint":"https://x","bogus":1}}})"); }), "unknown key");
        Expect(Throws([] { ParseServicesConfig(R"({"services":{"a":{"endpoint":"https://x","allowInsecure":"yes"}}})"); }), "allowInsecure must be boolean");
    }

    std::println("Test FindService");
    {
        auto const services = BuiltInServices();
        ExpectEqual(FindService(services, "ollama").name, "ollama", "built-in found");
        Expect(Throws([&] { FindService(services, "nope"); }), "unknown service");
    }

    std::println("Test SanitizeApiKey");
    ExpectEqual(SanitizeApiKey("  sk-abc\n").value_or(""), "sk-abc", "trimmed");
    Expect(!SanitizeApiKey("   ").has_value(), "blank rejected");
    Expect(!SanitizeApiKey("sk-abc\r\nX-Evil: 1").has_value(), "header injection rejected");

    std::println("Test ResolveService");
    {
        ServiceDefinition const remote{ .name = "remote", .endpoint = "https://api.example.com/v1/chat/completions", .apiKeyEnv = "REMOTE_KEY", .model = {}, .allowInsecure = false };

        auto const withKey = ResolveService(remote, FakeEnvironment({ { "REMOTE_KEY", " secret\n" } }));
        ExpectEqual(withKey.apiKey, "secret", "key from environment");
        Expect(withKey.secure, "https");

        Expect(Throws([&] { ResolveService(remote, FakeEnvironment({})); }), "missing key is an error");

        ServiceDefinition const local{ .name = "local", .endpoint = "http://127.0.0.1:8080/v1/chat/completions", .apiKeyEnv = {}, .model = {}, .allowInsecure = false };
        Expect(ResolveService(local, FakeEnvironment({})).apiKey.empty(), "keyless service needs no key");

        ServiceDefinition lan{ .name = "lan", .endpoint = "http://192.168.1.5:8080/v1/chat/completions", .apiKeyEnv = "LAN_KEY", .model = {}, .allowInsecure = false };
        Expect(Throws([&] { ResolveService(lan, FakeEnvironment({ { "LAN_KEY", "k" } })); }), "remote http refused by default");
        lan.allowInsecure = true;
        ExpectEqual(ResolveService(lan, FakeEnvironment({ { "LAN_KEY", "k" } })).apiKey, "k", "remote http allowed when declared");
    }

    std::println("All tests passed!");
    return 0;
}
