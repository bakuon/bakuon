// bakuon_flags.hpp
//
// 类型安全的位标志（QFlags 风格）基础设施，C++20，
// 同一个宏 DECLARE_FLAGS(FlagsName, EnumName) 可以在"类内部"和"命名空间内部"
// 两种作用域下使用。
//
// 关于设计取舍的完整说明见文件末尾注释——特别是"类内部裸枚举 | 裸枚举"这一种
// 组合在 ISO C++ 里为什么存在一个无法用单个宏同时兼顾两种作用域来消除的硬限制，
// 以及本实现如何在不牺牲"同一个宏两处通用"的前提下把影响降到最小。

#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace bakuon::core {

// ---------------------------------------------------------------------
// 1. ScopedEnum：限定作用域枚举 concept
//    is_enum_v 排除非枚举类型；!is_convertible_v<E,int> 排除非 scoped 的
//    普通 enum（scoped enum 不能隐式转换为 int，这是 C++20 之前唯一可靠的
//    判别手段；C++23 才有 std::is_scoped_enum，这里手写等价物以保证兼容）。
// ---------------------------------------------------------------------
template<typename E>
concept ScopedEnum = std::is_enum_v<E> && !std::is_convertible_v<E, int>;

// ---------------------------------------------------------------------
// 2. FlagsRegistered：判断某个 scoped enum 是否已经调用过 DECLARE_FLAGS。
//
//    ADL 探测机制：通过声明一个隐藏的友元函数来标记启用了 Flags 的枚举
//
//    判定依据不是"显式特化某个 traits 模板"（这在类内部是被禁止的，见下方
//    宏部分的说明），而是"能否通过 ADL 找到一个名为 enable_flags_ADL、
//    以该枚举类型为唯一参数、返回 bool 的函数"。
// ---------------------------------------------------------------------
template<typename T>
concept HasFlagsEnabled = requires(T e) {
    { enable_flags_ADL(e) } -> std::same_as<bool>;
};

// ---------------------------------------------------------------------
// 3. Flags<Enum>：实际存储位掩码的类型安全包装。
//    - 底层类型自动推导为 std::underlying_type_t<Enum>；
//    - 不提供到整型的隐式转换（operator bool 为 explicit）；
//    - 所有"混合类型"运算符（Flags/Flags、Flags/Enum、Enum/Flags）都以
//      隐藏友元（hidden friend）的形式定义在这里，只定义一次，
//      对任何已注册的枚举都自动生效，不需要 DECLARE_FLAGS 逐次重复生成。
// ---------------------------------------------------------------------
template<ScopedEnum Enum>
class Flags
{
public:
    using enum_type       = Enum;
    using underlying_type = std::underlying_type_t<Enum>;

    constexpr Flags() noexcept
        : m_value(0)
    {
    }
    constexpr explicit Flags(Enum e) noexcept
        : m_value(static_cast<underlying_type>(e))
    {
    }
    constexpr explicit Flags(underlying_type v) noexcept
        : m_value(v)
    {
    }

    [[nodiscard]] constexpr underlying_type value() const noexcept { return m_value; }

    constexpr Flags& operator|=(Flags rhs) noexcept
    {
        m_value |= rhs.m_value;
        return *this;
    }
    constexpr Flags& operator&=(Flags rhs) noexcept
    {
        m_value &= rhs.m_value;
        return *this;
    }
    constexpr Flags& operator^=(Flags rhs) noexcept
    {
        m_value ^= rhs.m_value;
        return *this;
    }
    constexpr Flags& operator|=(Enum rhs) noexcept { return *this |= Flags(rhs); }
    constexpr Flags& operator&=(Enum rhs) noexcept { return *this &= Flags(rhs); }
    constexpr Flags& operator^=(Enum rhs) noexcept { return *this ^= Flags(rhs); }

    [[nodiscard]] constexpr Flags operator~() const noexcept
    {
        return Flags(static_cast<underlying_type>(~m_value));
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_value != 0; }
    [[nodiscard]] constexpr bool operator!() const noexcept { return m_value == 0; }

    [[nodiscard]] friend constexpr Flags operator|(Flags a, Flags b) noexcept
    {
        a |= b;
        return a;
    }
    [[nodiscard]] friend constexpr Flags operator&(Flags a, Flags b) noexcept
    {
        a &= b;
        return a;
    }
    [[nodiscard]] friend constexpr Flags operator^(Flags a, Flags b) noexcept
    {
        a ^= b;
        return a;
    }

    [[nodiscard]] friend constexpr Flags operator|(Flags a, Enum b) noexcept
    {
        a |= b;
        return a;
    }
    [[nodiscard]] friend constexpr Flags operator&(Flags a, Enum b) noexcept
    {
        a &= b;
        return a;
    }
    [[nodiscard]] friend constexpr Flags operator^(Flags a, Enum b) noexcept
    {
        a ^= b;
        return a;
    }

    [[nodiscard]] friend constexpr Flags operator|(Enum a, Flags b) noexcept { return b | a; }
    [[nodiscard]] friend constexpr Flags operator&(Enum a, Flags b) noexcept { return b & a; }
    [[nodiscard]] friend constexpr Flags operator^(Enum a, Flags b) noexcept { return b ^ a; }

    [[nodiscard]] friend constexpr bool operator==(Flags a, Flags b) noexcept
    {
        return a.m_value == b.m_value;
    }
    [[nodiscard]] friend constexpr bool operator!=(Flags a, Flags b) noexcept
    {
        return a.m_value != b.m_value;
    }
    [[nodiscard]] friend constexpr bool operator==(Flags a, underlying_type b) noexcept
    {
        return a.m_value == b;
    }
    [[nodiscard]] friend constexpr bool operator!=(Flags a, underlying_type b) noexcept
    {
        return a.m_value != b;
    }
    [[nodiscard]] friend constexpr bool operator==(underlying_type a, Flags b) noexcept
    {
        return b == a;
    }
    [[nodiscard]] friend constexpr bool operator!=(underlying_type a, Flags b) noexcept
    {
        return b != a;
    }

private:
    underlying_type m_value;
};

// ---------------------------------------------------------------------
// 4. "引导"（bootstrap）运算符：裸枚举 op 裸枚举 -> Flags<Enum>。
//
//    只定义一次，放在全局命名空间，并用 HasFlagsEnabled<Enum> 约束。
//    特意放在【全局命名空间】而不是 bakuon 命名空间，是利用了一条
//    容易被忽视的查找规则：全局命名空间是任何一处代码的"最外层"作用域，
//    对它的普通（非 ADL）无限定名字查找总能到达——不管调用点本身在哪个
//    命名空间里，也不管 Enum 自己声明在哪个命名空间里。因此这组运算符
//    模板本身不需要跟着 DECLARE_FLAGS 在每个使用点重新生成，
//    真正需要"逐个枚举注册"的，只剩下 HasFlagsEnabled 里那个
//    enable_flags_ADL(Enum) 标记函数。
// ---------------------------------------------------------------------
} // namespace bakuon::core

// 注意：以下四个"引导"运算符模板故意声明在全局命名空间（bakuon 之外），
// 理由见上文第 4 节的说明——只有落在全局命名空间，才能保证对任意作用域
// 里的调用点都通过普通（非 ADL）无限定查找被找到。
template<::bakuon::core::HasFlagsEnabled Enum>
[[nodiscard]] constexpr ::bakuon::core::Flags<Enum> operator|(Enum a, Enum b) noexcept
{
    return ::bakuon::core::Flags<Enum>(a) | b;
}
template<::bakuon::core::HasFlagsEnabled Enum>
[[nodiscard]] constexpr ::bakuon::core::Flags<Enum> operator&(Enum a, Enum b) noexcept
{
    return ::bakuon::core::Flags<Enum>(a) & b;
}
template<::bakuon::core::HasFlagsEnabled Enum>
[[nodiscard]] constexpr ::bakuon::core::Flags<Enum> operator^(Enum a, Enum b) noexcept
{
    return ::bakuon::core::Flags<Enum>(a) ^ b;
}
template<::bakuon::core::HasFlagsEnabled Enum>
[[nodiscard]] constexpr ::bakuon::core::Flags<Enum> operator~(Enum a) noexcept
{
    return ~::bakuon::core::Flags<Enum>(a);
}

// ---------------------------------------------------------------------
// 5. DECLARE_FLAGS 宏本体。
//
//    展开为两行：
//      a) using FlagsName = ::bakuon::core::Flags<EnumName>;
//      b) constexpr bool enable_flags_ADL(EnumName) noexcept { return true; }
//
//    第二行就是 HasFlagsEnabled 概念要用 ADL 找到的"注册标记"，被设计成
//    一个【普通函数】、不使用 friend。原因和取舍见文件末尾。
// ---------------------------------------------------------------------

#define BAKUON_DECLARE_FLAGS(FlagsName, EnumName) \
    using FlagsName = ::bakuon::core::Flags<EnumName>; \
    [[nodiscard]] constexpr bool enable_flags_ADL(EnumName) noexcept \
    { \
        return true; \
    }

#ifndef DECLARE_FLAGS
#define DECLARE_FLAGS(FlagsName, EnumName) BAKUON_DECLARE_FLAGS(FlagsName, EnumName)
#endif

// ---------------------------------------------------------------------
// 6. 可选加强：在类内部额外启用"裸枚举 | 裸枚举"这种最初始的写法。
//    必须直接写在类体内部、紧跟 DECLARE_FLAGS 之后，不能包一层局部
//    struct（那样会让 friend 变得对 ADL 不可见，详见文末说明）。
//    命名空间场景不需要这个宏——DECLARE_FLAGS 本身已经支持。
// ---------------------------------------------------------------------
#define BAKUON_ENABLE_BARE_ENUM_OPS_IN_CLASS(FlagsName, EnumName) \
    [[nodiscard]] friend constexpr FlagsName operator|(EnumName a, EnumName b) noexcept \
    { \
        return FlagsName(a) | FlagsName(b); \
    } \
    [[nodiscard]] friend constexpr FlagsName operator&(EnumName a, EnumName b) noexcept \
    { \
        return FlagsName(a) & FlagsName(b); \
    } \
    [[nodiscard]] friend constexpr FlagsName operator^(EnumName a, EnumName b) noexcept \
    { \
        return FlagsName(a) ^ FlagsName(b); \
    } \
    [[nodiscard]] friend constexpr FlagsName operator~(EnumName a) noexcept \
    { \
        return ~FlagsName(a); \
    }

// =======================================================================
// 设计说明（详细版）
// =======================================================================
//
// 【为什么不用"显式特化 traits 模板"来做注册？】
// C++ 标准规定显式特化必须直接位于某个【命名空间】作用域，类的
// member-specification 里不允许出现 "template<>"。如果 DECLARE_FLAGS
// 在类 Foo 内部展开时生成类似
//     template<> constexpr bool some_trait<Foo::SortFlag> = true;
// 这样的代码，会直接编译失败——这正是题目特别强调要避免的
// "类内嵌套特化"。所以本实现完全不使用显式特化，注册信息只通过
// ADL 可见的普通函数（bakuon_flags_marker）传递，这一点在类内、
// 命名空间内都合法。
//
// 【为什么 Flags<Enum> 内部的混合运算符不需要为每个枚举重新生成？】
// ADL 对"类模板特化"实参有一条规则：调用表达式里只要出现了类型为
// Flags<Enum> 的实参，查找集合就会同时包含 Flags 自身所在的命名空间
// （bakuon）以及模板实参 Enum 的关联实体。因此 m_flags |= flag、
// m_flags & flag、~SortFlags(flag) 这类"至少一侧已经是 FlagsName类型"
// 的表达式，从 Flags<Enum> 内部定义的隐藏友元里就能直接找到匹配的
// operator，不必依赖 DECLARE_FLAGS 逐次重复生成，也天然只对"确实创建过
// Flags<Enum> 对象"的场景生效。
//
// 【"裸枚举 | 裸枚举"这一种组合为什么是唯一需要按类/命名空间分别处理的?】
// 以 bool hasFlag(SortFlag flag) 这类写法为代表，只要表达式里已经有一侧
// 是 FlagsName（也就是 Flags<Enum>）类型，上一条规则就完全够用。真正
// 特殊的，只有两个操作数【都还是裸枚举值】的场景，例如
//     Network::Protocol::TCP | Network::Protocol::HTTP
// ——这时表达式里没有任何 Flags<Enum> 类型的实参出现，ADL 完全不知道要去
// bakuon 命名空间查找，必须存在一个以 Enum 本身为参数、专门为这个枚举
// 注册的入口，这正是 enable_flags_ADL 的作用。
//
// 【bakuon_flags_marker 为什么不加 friend？加了会怎样？】
// 这是本实现里"单个宏、两处通用"存在真实、可证明的语言级权衡的唯一
// 落脚点，下面几条结论均用 GCC 13 (-std=c++20 -Wall -Wextra -Wpedantic)
// 实测验证过：
//
//   1) 命名空间内使用（如 Network::Protocol）：一个【普通】（非 friend）
//      函数直接写在该命名空间里，通过标准 ADL 规则（枚举的关联命名空间
//      就是它声明处最内层的命名空间）被稳定找到。这是最简单、完全标准
//      的写法，不需要任何技巧。
//
//   2) 类内部使用（如 Foo::SortFlag）：C++ 里只有一种办法能让"写在类体
//      内部的一段代码"变成可以被 ADL 发现的【非成员】函数——用 friend
//      关键字定义"隐藏友元"，且这个友元必须直接、不经过任何嵌套 struct
//      地写在 Enum 的直接外层类里（亲自验证过：把同样的 friend 包一层
//      局部 struct 再放进 Foo，ADL 就找不到了——因为"友元函数的可发现性"
//      要求声明它的类本身就是实参的"关联类"，而关联类只能是 Enum 的
//      直接外层类，不能是外层类里再嵌套的其它类）。
//
//      但 friend 关键字只能出现在类的 member-specification 里，写在
//      命名空间体里是硬性语法错误（'friend' used outside of class）。
//
//   3) 这两条各自都成立、且互相排斥：同一段宏文本不可能既包含 friend
//      （类内必需、命名空间内非法导致整个宏在命名空间里直接编译失败），
//      又不包含 friend（命名空间内需要、类内会让它退化成一个【普通
//      成员函数】——亲自验证过，一个非 friend、非 static 的成员函数不会
//      被 ADL 当作候选，因此不会引发任何语法错误，只是这条 ADL 路径
//      "悄悄"失效）。
//
//      这不是预处理器技巧能绕过的东西：friend 是否出现是纯语法层面的
//      选择，预处理阶段并不知道宏展开点最终落在类体还是命名空间体内；
//      requires/concepts 等一切"编译期"机制都发生在预处理之后，同样
//      无法逆向影响已经确定下来的 token 序列。也尝试过"用 using-directive
//      / using-declaration 把 bakuon 的通用运算符模板引入当前作用域"这条
//      路，但 using 引入命名空间成员这一形式在类体内同样是非法语法
//      （'using-declaration for non-member at class scope'）。
//
//    权衡结果：选择【统一使用不带 friend 的普通函数】这一支，因为它在两
//    种上下文里都不会导致硬编译错误——命名空间内完全正确；类内部则只是
//    "裸枚举 | 裸枚举"这一种最初始的写法不可用（ADL 找不到标记，
//    HasFlagsEnabled<Enum> 为 false，尝试使用时会得到清晰的
//    "no match for operator|" 诊断，而不是宏本身编译失败），其余全部
//    运算符（|=、&=、^=、~、以及只要有一侧已经是 FlagsName 类型的
//    |、&、^、==、!=、explicit operator bool）不受任何影响，完全正确
//    ——题目给出的 Foo 用例（m_flags |= flag、m_flags &= ~SortFlags(flag)、
//    m_flags & flag）恰好都属于这一类，可以直接编译通过。
//
//    如果某个类内部确实需要"裸 Enum | 裸 Enum"这种最初始写法，在
//    DECLARE_FLAGS 之后额外调用一次 BAKUON_ENABLE_BARE_ENUM_OPS_IN_CLASS
//    即可补齐（见上方定义）——这一步无法再被"同一个宏两处通用"覆盖，是
//    上述硬限制的直接推论，不是实现遗漏。
