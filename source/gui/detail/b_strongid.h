#pragma once

#include <QtCore/QDebug>
#include <QtCore/QHash>
#include <QtCore/QString>

namespace bakuon::gui {

// 通用的“强类型 ID”包装器：CommandId 与 ContextId 底层都是字符串，
// 但语义完全不同，用模板参数 Tag 在编译期把它们区分开，
// 避免出现「把 ContextId 错传成 CommandId」这类只有运行时才会暴露的低级错误。
// Tag 只作为区分标记使用，不需要被实际定义（可以是不完整类型）。
template<typename Tag>
class StrongId
{
public:
    StrongId() = default;
    explicit StrongId(QString value)
        : m_value(std::move(value))
    {
    }
    explicit StrongId(const char* value)
        : m_value(QString::fromUtf8(value))
    {
    }

    [[nodiscard]] const QString& value() const noexcept { return m_value; }
    [[nodiscard]] bool isValid() const noexcept { return !m_value.isEmpty(); }

    friend bool operator==(const StrongId& lhs, const StrongId& rhs) noexcept
    {
        return lhs.m_value.compare(rhs.m_value, Qt::CaseInsensitive) == 0;
    }
    friend bool operator!=(const StrongId& lhs, const StrongId& rhs) noexcept
    {
        return lhs.m_value.compare(rhs.m_value, Qt::CaseInsensitive) != 0;
    }

    friend QDebug operator<<(QDebug dbg, const StrongId& id)
    {
        QDebugStateSaver saver(dbg);
        if (!id.isValid()) {
            dbg.nospace() << "StrongId(invalid)";
        } else {
            dbg.nospace().noquote() << "StrongId(" << id.value() << ")";
        }
        return dbg;
    }

private:
    QString m_value;
};

} // namespace bakuon::gui

// 为 std::unordered_map / std::unordered_set 提供 std::hash 特化，
// 使 StrongId<Tag> 可以直接作为这些容器的 key 使用。
namespace std {
template<typename Tag>
struct hash<bakuon::gui::StrongId<Tag>>
{
    size_t operator()(const bakuon::gui::StrongId<Tag>& id) const noexcept
    {
        auto h = static_cast<size_t>(qHash(id.value()));
        return h ^ (h >> 33);
    }
};
} // namespace std
