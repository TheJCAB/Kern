#include "SessionId.h"

#include <random>
#include <string>

std::string GenerateId()
{
    static constexpr char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(kAlphabet) - 2));

    std::string id;
    id.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        id.push_back(kAlphabet[dist(rng)]);
    }

    return id;
}
