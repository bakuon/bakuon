#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "core/b_container.h"
#include "core/b_entity.h"

namespace bakuon::core {

/// 域生命周期/服务表的契约被违反时抛出（重复注册、名字碰撞、空名字……）。
class DomainError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief 域标识：命名域 = FNV-1a 64(name) | 最高位；匿名域 = Workspace 分配的计数器值。
 * @note 最高位把两类 id 空间彻底隔开，命名域之间的哈希碰撞由 Workspace 在创建时
 *       比对名字检出。名字大小写敏感。0 为无效值。
 */
class DomainId
{
public:
    using value_type = std::uint64_t;

    constexpr DomainId() noexcept = default;
    explicit constexpr DomainId(value_type raw) noexcept
        : m_value(raw)
    {
    }

    [[nodiscard]] static constexpr DomainId fromName(std::string_view name) noexcept
    {
        if (name.empty()) {
            return {};
        }
        value_type hash = 0xcbf29ce484222325ULL;
        for (const char c : name) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 0x100000001b3ULL;
        }
        return DomainId{hash | kNamedBit};
    }

    [[nodiscard]] constexpr value_type value() const noexcept { return m_value; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return m_value != 0; }
    [[nodiscard]] constexpr bool isNamed() const noexcept { return (m_value & kNamedBit) != 0; }

    friend constexpr auto operator<=>(const DomainId&, const DomainId&) = default;
    friend constexpr bool operator==(const DomainId&, const DomainId&)  = default;

private:
    static constexpr value_type kNamedBit = value_type{1} << 63;
    value_type m_value{0};
};

/// 弱句柄：id + serial。域被销毁（哪怕随后同名重建）后 Workspace::resolve() 一律返回 nullptr。
class DomainHandle
{
public:
    constexpr DomainHandle() noexcept = default;

    [[nodiscard]] constexpr DomainId id() const noexcept { return m_id; }
    [[nodiscard]] constexpr std::uint64_t serial() const noexcept { return m_serial; }
    [[nodiscard]] constexpr bool isNull() const noexcept { return m_serial == 0; }

    friend constexpr bool operator==(const DomainHandle&, const DomainHandle&) = default;

private:
    friend class Domain;
    constexpr DomainHandle(DomainId id, std::uint64_t serial) noexcept
        : m_id(id)
        , m_serial(serial)
    {
    }

    DomainId m_id{};
    std::uint64_t m_serial{0};
};

/**
 * @brief 一个相互隔离的领域/上下文：独占一个 Container（即一个 entt::registry）
 *        外加一张按类型索引的"服务表"。
 *
 * ## 服务表（后续 EnTT 模块的挂载点）
 * Identifier / UndoStack / Selection / Serializer / Cloner / Dispatcher / Scheduler 等
 * 都是"绑定到某个 Registry 的对象"，以服务形式挂到域上即可，Workspace 本身不需要认识它们：
 * @code
 *   auto& gen = domain.emplaceService<SnowflakeGenerator>(1);
 *   auto& ids = domain.emplaceService<Identifier>(domain.registry(), &gen);
 *   auto& undo = domain.emplaceService<UndoStack<Position>>(domain.registry());
 * @endcode
 * 服务按构造的逆序销毁，且全部先于 Container 销毁——后构造的可以放心依赖先构造的，
 * 所有持有 Registry&/Connection 的服务都能在 Registry 仍存活时安全断开。
 *
 * @warning 服务通过 typeHash<T>() 索引：同一类型在一个域里只能有一个实例。
 *          非线程安全，线程亲和性由使用该域的一方负责。
 */
class Domain
{
public:
    ~Domain();

    Domain(const Domain&)            = delete;
    Domain& operator=(const Domain&) = delete;
    Domain(Domain&&)                 = delete;
    Domain& operator=(Domain&&)      = delete;

    [[nodiscard]] DomainId id() const noexcept { return m_id; }
    [[nodiscard]] std::string_view name() const noexcept { return m_name; }
    [[nodiscard]] std::uint64_t serial() const noexcept { return m_serial; }
    [[nodiscard]] DomainHandle handle() const noexcept { return DomainHandle{m_id, m_serial}; }
    /// 正在被销毁（onDomainDestroying 回调期间为 true）。
    [[nodiscard]] bool isClosing() const noexcept { return m_closing; }

    [[nodiscard]] Container& container() noexcept { return m_container; }
    [[nodiscard]] const Container& container() const noexcept { return m_container; }
    /// 逃生舱口，语义同 Container::registry()：给 UndoStack/Selection 等需要 Registry& 的服务用。
    [[nodiscard]] Registry& registry() noexcept { return m_container.registry(); }
    [[nodiscard]] const Registry& registry() const noexcept { return m_container.registry(); }

    // ---- 服务表 ----

    /// 构造并登记服务；同类型已存在时抛 DomainError（不做静默覆盖/未定义行为）。
    template<typename T, typename... Args>
    T& emplaceService(Args&&... args)
    {
        static_assert(std::is_same_v<T, std::remove_cvref_t<T>>, "服务类型不能带 cv/引用修饰");
        const std::uint32_t type = typeHash<T>();
        if (findService(type) != nullptr) {
            throw DomainError("service already registered: " + std::string(typeName<T>()));
        }
        T* raw = new T(std::forward<Args>(args)...);
        std::unique_ptr<void, void (*)(void*)> holder(raw, &destroyService<T>);
        m_services.push_back(
            ServiceSlot{type, std::move(holder)}); // 抛 bad_alloc 时 holder 负责回收
        return *raw;
    }

    template<typename T>
    [[nodiscard]] T* tryService() noexcept
    {
        return static_cast<T*>(findService(typeHash<T>()));
    }

    template<typename T>
    [[nodiscard]] const T* tryService() const noexcept
    {
        return static_cast<const T*>(findService(typeHash<T>()));
    }

    template<typename T>
    [[nodiscard]] T& service()
    {
        if (T* p = tryService<T>()) {
            return *p;
        }
        throw DomainError("service not found: " + std::string(typeName<T>()));
    }

    template<typename T>
    [[nodiscard]] bool hasService() const noexcept
    {
        return findService(typeHash<T>()) != nullptr;
    }

    /// 提前摘除并销毁单个服务；不存在返回 false。
    template<typename T>
    bool eraseService() noexcept
    {
        const std::uint32_t type = typeHash<T>();
        const auto it = std::find_if(m_services.begin(),
                                     m_services.end(),
                                     [type](const ServiceSlot& s) { return s.type == type; });
        if (it == m_services.end()) {
            return false;
        }
        auto object = std::move(it->object); // 先让服务表回到一致状态，再销毁服务
        m_services.erase(it);
        return true; // object 在此析构
    }

    [[nodiscard]] std::size_t serviceCount() const noexcept { return m_services.size(); }

private:
    friend class Workspace;
    Domain(DomainId id, std::string name, std::uint64_t serial);

    struct ServiceSlot
    {
        std::uint32_t type;
        std::unique_ptr<void, void (*)(void*)> object;
    };

    template<typename T>
    static void destroyService(void* p) noexcept
    {
        delete static_cast<T*>(p);
    }

    [[nodiscard]] void* findService(std::uint32_t type) const noexcept
    {
        for (const ServiceSlot& slot : m_services) {
            if (slot.type == type) {
                return slot.object.get();
            }
        }
        return nullptr;
    }

private:
    DomainId m_id;
    std::string m_name;
    std::uint64_t m_serial;
    bool m_closing = false;

    // 声明顺序 = 构造顺序；析构时 m_services 先于 m_container 销毁。
    // 不要为了"看起来整齐"调整这两个成员的相对顺序。
    Container m_container;
    std::vector<ServiceSlot> m_services;
};

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::DomainId>
{
    [[nodiscard]] std::size_t operator()(bakuon::core::DomainId id) const noexcept
    {
        return std::hash<bakuon::core::DomainId::value_type>{}(id.value());
    }
};
