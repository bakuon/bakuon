#include "core/b_bakuon.h"

#include <string>

#include <entt/core/hashed_string.hpp>

namespace bakuon::core {

std::string_view versionString() noexcept
{
    return "0.1.0";
}

IdType stringHashed(std::string_view str) noexcept
{
    return entt::hashed_string(std::string(str).c_str(), str.size()).value();
}

std::uint64_t stringFnv1a(std::string_view str) noexcept
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : str) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace bakuon::core
