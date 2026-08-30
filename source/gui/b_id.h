#pragma once

#include <QtCore/QDebug>
#include <QtCore/QHash>
#include <QtCore/QString>

#include "gui/b_gui_export.h"

namespace bakuon::gui {

// Id<Tag> 本身是头文件里的模板，任何消费者（包括跨动态库边界的插件/沙箱代码）在自己的
// 编译单元里实例化 Id<Tag> 时都会内联调用下面这两个自由函数——它们的定义只存在于
// b_id.cpp（即只存在于 gui.dll/.so 里），因此必须显式导出，否则 MSVC 上任何用到
// Id<Tag> 的跨库消费者都会在链接期报 unresolved external symbol。
namespace internal {
BAKUON_GUI_EXPORT uint32_t lookupId(QString original);
BAKUON_GUI_EXPORT QString lookupName(uint32_t raw);
} // namespace internal

// 通用的“强类型 ID”包装器：CommandId 与 ContextId 底层都是字符串，
// 但语义完全不同，用模板参数 Tag 在编译期把它们区分开，
// 避免出现「把 ContextId 错传成 CommandId」这类只有运行时才会暴露的低级错误。
// Tag 只作为区分标记使用，不需要被实际定义（可以是不完整类型）。
template<typename Tag>
class Id
{
public:
    Id() = default;
    Id(QString name)
        : m_id(internal::lookupId(std::move(name)))
    {
    }

    Id(const char* name)
        : Id(QString::fromUtf8(name))
    {
    }

    uint32_t rawId() const noexcept { return m_id; }
    bool isValid() const noexcept { return m_id != 0; }
    QString name() const { return internal::lookupName(m_id); }

    friend bool operator==(const Id& lhs, const Id& rhs) noexcept { return lhs.m_id == rhs.m_id; }
    friend bool operator!=(const Id& lhs, const Id& rhs) noexcept { return lhs.m_id != rhs.m_id; }

    friend QDebug operator<<(QDebug dbg, const Id& id)
    {
        QDebugStateSaver saver(dbg);
        if (!id.isValid()) {
            dbg.nospace() << "Id(invalid)";
        } else {
            dbg.nospace().noquote() << "Id(" << id.name() << ")";
        }
        return dbg;
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
