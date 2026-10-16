#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/b_entity.h"
#include "core/b_result.h"
#include "core/b_tagger.h"

// ============================================================================
// 扩展系统的组件词汇表（普通结构体，不继承、不带虚函数、不认识 Qt / 插件）。
//
//  扩展点实体： ExtensionPointKey + ExtensionPointInfo + ExtensionList
//  扩展实体：   ExtensionOf + ExtensionPriority + (ExtensionInstance | ExtensionFactory)
//
// 其它"变体"都是往实体上加组件，而不是新增扩展点子类：
//  - 懒加载 = ExtensionFactory（本文件已实现）；单例/瞬态是它的 policy 字段。
//  - 作用域/权限过滤 = 再加一个 ExtensionScopes 组件 + 查询时过滤（未实现，接口位置已留好）。
//  - 异步派发 = 另一个 System，读 ExtensionList 自行投递，不属于本类职责。
// ============================================================================

namespace bakuon::core {

namespace extension {

/// 扩展点的身份组件（id 字符串）：BasicTagger 以它为唯一键维护 id → 实体索引。
struct ExtensionPointKey
{
    std::string value;
};

struct ExtensionPointInfo
{
    std::string description;
    std::uint64_t type{0}; ///< extension_type_key<T>()，用于 point<T>() 的类型校验
};

/// 扩展点名下的有序扩展：priority 降序、同优先级按注册序升序。只由 ExtensionPoint 维护。
struct ExtensionList
{
    std::vector<Entity> ordered;
};

/// 扩展 → 所属扩展点。
struct ExtensionOf
{
    Entity point{NullEntity};
};

struct ExtensionPriority
{
    int value{0};
    std::uint64_t order{0}; ///< 注册序号，稳定排序用
};

/// 已实例化的扩展对象（类型擦除；取回时由 ExtensionPoint<T> 静态转换）。
struct ExtensionInstance
{
    std::shared_ptr<void> object;
};

/// 懒加载工厂。Singleton 策略首次解析后会补上 ExtensionInstance 并保留工厂。
struct ExtensionFactory
{
    std::function<std::shared_ptr<void>()> make;
    bool singleton{true};
};

} // namespace extension

/**
 * @brief 扩展点实体操作接口
 */
class ExtensionPoint
{
public:
    explicit ExtensionPoint(Registry &registry);
    ~ExtensionPoint();

    ExtensionPoint(const ExtensionPoint &)            = delete;
    ExtensionPoint &operator=(const ExtensionPoint &) = delete;
    ExtensionPoint(ExtensionPoint &&)                 = delete;
    ExtensionPoint &operator=(ExtensionPoint &&)      = delete;

    [[nodiscard]] Result<Entity> create(std::string_view id, std::string_view description,
                                        std::uint64_t type);
    [[nodiscard]] Result<Entity> ensure(std::string_view id, std::string_view description,
                                        std::uint64_t type);
    bool erase(std::string_view id);

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] Entity find(std::string_view id) const;
    [[nodiscard]] std::string id(Entity point) const;
    [[nodiscard]] std::uint64_t type(Entity point) const;
    [[nodiscard]] std::vector<std::string> list() const;

private:
    void extensionDestroyed(Registry &registry, Entity extension);

private:
    Registry &m_registry;
    ScopedConnection m_extensionDestroyed;
    BasicTagger<extension::ExtensionPointKey> m_points;
};

/**
 * @brief 扩展实体操作接口
 */
class Extension
{
public:
    explicit Extension(Registry &registry);
    ~Extension() = default;

    [[nodiscard]] Result<Entity> add(Entity point, std::shared_ptr<void> object, int priority);
    [[nodiscard]] Result<Entity> addFactory(Entity point,
                                            std::function<std::shared_ptr<void>()> factory,
                                            int priority, bool singleton = true);
    bool remove(Entity extension);
    bool setPriority(Entity extension, int priority);
    [[nodiscard]] std::size_t count(Entity point) const;
    [[nodiscard]] std::vector<Entity> list(Entity point) const;
    [[nodiscard]] std::shared_ptr<void> resolve(Entity extension);

private:
    [[nodiscard]] extension::ExtensionList *listOf(Entity point) const;
    [[nodiscard]] Result<Entity> createExtensionEntity(Entity point, int priority);
    void insertSorted(Entity point, Entity extension);

private:
    Registry &m_registry;
    std::uint64_t m_nextOrder = 1;
};

} // namespace bakuon::core
