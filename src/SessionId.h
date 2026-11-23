#pragma once

#include <string>

// A fresh random identifier for one session.
std::string GenerateId();

// Who the session is: a stable id, the session it was spawned from, its role and an optional tag.
struct SessionIdentity
{
    std::string id = GenerateId(); // Unique per session; also the prefix of both artifacts.
    std::string parentId{};   // Empty for a root session.
    std::string role;
    std::string tag{};        // Omitted from the manifest when empty.
};
