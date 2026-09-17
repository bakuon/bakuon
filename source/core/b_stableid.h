#pragma once

#include <cstdint>
#include <functional>

namespace bakuon::core {

/**
 * @brief 稳定64位身份标识,永不回收：跨销毁/回收、跨保存、跨进程消息均安全可靠。
 * @details 零被保留为“无效”，因此默认构造的ID为空。
 */
class StableId
{
public:
    using value_type = std::uint64_t;

    constexpr StableId() = default;
    constexpr StableId(value_type value) noexcept
        : m_value(value)
    {
    }

    [[nodiscard]] constexpr value_type value() const noexcept { return m_value; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return m_value != 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

    [[nodiscard]] friend constexpr auto operator<=>(const StableId&, const StableId&) = default;
    [[nodiscard]] friend constexpr bool operator==(const StableId&, const StableId&)  = default;

private:
    value_type m_value{0}; // 0 为无效标识，不被回收。
};

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::StableId>
{
    [[nodiscard]] std::size_t operator()(bakuon::core::StableId id) const noexcept
    {
        return std::hash<bakuon::core::StableId::value_type>{}(id.value());
    }
};
