#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>

#include <bakuon/core/Flags.h>

// ===================== 类内部 =====================
class Foo
{
public:
    enum class SortFlag : std::uint8_t {
        None = 0,
        Name = 1 << 0,
        Time = 1 << 1,
        Size = 1 << 2,
        Type = 1 << 3
    };

    DECLARE_FLAGS(SortFlags, SortFlag)

private:
    SortFlags m_flags{SortFlag::None};

public:
    Foo() = default;
    void setFlags(SortFlags flags) { m_flags = flags; }

    void setFlag(SortFlag flag, bool on = true)
    {
        if (on) {
            m_flags |= flag;
        } else {
            m_flags &= ~SortFlags(flag);
        }
    }

    SortFlags getFlags() const { return m_flags; }
    bool hasFlag(SortFlag flag) const { return (m_flags & flag) != 0; }
};

// ===================== 命名空间内部 ================
namespace Network {

enum class Protocol : std::uint8_t { TCP = 1 << 0, UDP = 1 << 1, HTTP = 1 << 2 };
DECLARE_FLAGS(ProtocolFlags, Protocol)

} // namespace Network

// ===================== 额外验证：底层类型推导 =====================
static_assert(std::is_same_v<Foo::SortFlags::underlying_type, std::uint8_t>);
static_assert(std::is_same_v<Network::ProtocolFlags::underlying_type, std::uint8_t>);

// ===================== 额外验证：类内部用可选宏补齐"裸枚举|裸枚举" =====================
class Bar
{
public:
    enum class Perm : std::uint8_t { Read = 1, Write = 2, Exec = 4 };
    DECLARE_FLAGS(PermFlags, Perm)
    BAKUON_ENABLE_BARE_ENUM_OPS_IN_CLASS(PermFlags, Perm)
};

TEST(FlagsTest, DeclaredFlagsOperator)
{
    Foo foo;
    foo.setFlag(Foo::SortFlag::Name, true);
    foo.setFlag(Foo::SortFlag::Time, true);

    EXPECT_TRUE(foo.hasFlag(Foo::SortFlag::Name));
    EXPECT_TRUE(foo.hasFlag(Foo::SortFlag::Time));
    EXPECT_FALSE(foo.hasFlag(Foo::SortFlag::Size));
}

TEST(FlagsTest, DeclaredFlagsBareOperator)
{
    Network::ProtocolFlags flags = Network::Protocol::TCP | Network::Protocol::HTTP;
    EXPECT_TRUE(flags & Network::Protocol::TCP);

    Network::ProtocolFlags pf = Network::Protocol::TCP | Network::Protocol::UDP;
    pf &= ~Network::ProtocolFlags(Network::Protocol::UDP);

    EXPECT_TRUE(static_cast<bool>(pf & Network::Protocol::TCP));
    EXPECT_FALSE(static_cast<bool>(pf & Network::Protocol::UDP));

    Bar::PermFlags pflags = Bar::Perm::Read | Bar::Perm::Write;
    pflags |= Bar::Perm::Exec;
    EXPECT_EQ(static_cast<int>(pflags.value()), 7);
}
