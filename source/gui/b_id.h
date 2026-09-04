#pragma once

#include <cstdint>
#include <string_view>

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include "gui/b_gui_export.h"

namespace bakuon::gui {

// Id<Tag> 本身是头文件里的模板，任何消费者（包括跨动态库边界的插件/沙箱代码）在自己的
// 编译单元里实例化 Id<Tag> 时都会内联调用下面这几个自由函数——它们的定义只存在于
// b_id.cpp（即只存在于 gui.dll/.so 里），因此必须显式导出，否则 MSVC 上任何用到
// Id<Tag> 的跨库消费者都会在链接期报 unresolved external symbol。
namespace internal {
BAKUON_GUI_EXPORT uint32_t lookupId(std::string_view name);
BAKUON_GUI_EXPORT std::string_view lookupName(uint32_t raw) noexcept;
} // namespace internal

// 通用的“强类型 ID”包装器：CommandId 与 ContextId 底层都是字符串，
// 但语义完全不同，用模板参数 Tag 在编译期把它们区分开，
// 避免出现「把 ContextId 错传成 CommandId」这类只有运行时才会暴露的低级错误。
// Tag 只作为区分标记使用，不需要被实际定义（可以是不完整类型）。
//
// 与旧版本的关键区别：本类型本身不再依赖 QString/QHash/QDebug——名字驻留表内部
// 用 std::string 存储、std::string_view 传参，比较/哈希（含大小写折叠）全部
// 在原始字节上完成，不产生任何 QString<->UTF-8 的编码转换开销。构造已存在的
// Id（绝大多数调用场景：同一个字符串字面量被反复用来构造同一个 Id）时，查表命中
// 路径不需要任何堆分配（见 b_id.cpp 中 IdTable 的异构查找设计）。
template<typename Tag>
class Id
{
public:
    constexpr Id() noexcept = default;

    // 核心构造路径：接受 std::string_view（因此 std::string 可以隐式转换过来），
    // 全程不触碰 Qt。
    Id(std::string_view name)
        : m_id(internal::lookupId(name))
    {
    }

    // 专门为 const char* 字面量提供的重载。没有它的话，`CommandId{"edit.delete"}`
    // 这种到处都是的写法会在“转成 std::string_view”和“转成 QString”这两条
    // 用户自定义转换路径之间产生二义性（QString 同样有一个 const char* 的隐式
    // 构造函数）——两条路径“同样好”，重载决议无法选择。这里直接精确匹配
    // const char*（无需任何用户自定义转换），把它挤到两者之前。
    Id(const char* name)
        : Id(std::string_view(name))
    {
    }

    explicit Id(const QByteArray& utf8)
        : Id(std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())))
    {
    }

    explicit Id(const QString& name)
        : Id(name.toUtf8())
    {
    }

    uint32_t rawId() const noexcept { return m_id; }
    bool isValid() const noexcept { return m_id != 0; }

    // 零拷贝：返回的 string_view 指向驻留表内部的 std::string。驻留表只增不减
    // （id 一旦分配就永不回收/复用），因此只要进程存活，这个 view 就一直有效，
    // 可以放心保存/传递，不必担心悬空。
    std::string_view name() const noexcept { return internal::lookupName(m_id); }

    QString toString() const noexcept
    {
        const auto n = name();
        return QString::fromUtf8(n.data(), static_cast<int>(n.size()));
    }

    friend bool operator==(const Id& lhs, const Id& rhs) noexcept { return lhs.m_id == rhs.m_id; }
    friend bool operator!=(const Id& lhs, const Id& rhs) noexcept { return lhs.m_id != rhs.m_id; }

    // 同时服务 std::ostream 与 QDebug
    template<typename Stream>
    friend Stream&& operator<<(Stream&& os, const Id& id)
    {
        if (!id.isValid()) {
            os << "Id(invalid)";
        } else {
            os << "Id(" << id.rawId() << ',' << std::string_view(id.name()) << ')';
        }
        return std::forward<Stream>(os);
    }

private:
    uint32_t m_id{0};
};

} // namespace bakuon::gui

// 为 std::unordered_map / std::unordered_set 提供 std::hash 特化，
// 使 Id<Tag> 可以直接作为这些容器的 key 使用。
namespace std {
template<typename Tag>
struct hash<bakuon::gui::Id<Tag>>
{
    size_t operator()(const bakuon::gui::Id<Tag>& id) const noexcept
    {
        return static_cast<size_t>(id.rawId());
    }
};
} // namespace std
