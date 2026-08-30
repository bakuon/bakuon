#pragma once

#include <unordered_set>

#include <QtCore/QDebug>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

#include "gui/b_gui_export.h"
#include "gui/b_types.h"

namespace bakuon::gui {

class BAKUON_GUI_EXPORT Context
{
public:
    Context() = default;
    explicit Context(std::initializer_list<ContextId> list);
    explicit Context(const ContextId &id);
    Context(const Context &o)            = default;
    Context &operator=(const Context &o) = default;
    ~Context()                           = default;

    size_t count() const { return m_set.size(); }
    bool contains(const ContextId &id) const { return m_set.contains(id); }
    bool empty() const { return m_set.empty(); }
    size_t capacity() const { return m_set.max_size(); }

    void reserve(size_t capacity) { m_set.reserve(capacity); }
    void append(const ContextId &id) { m_set.emplace(id); }
    void append(const Context &ctx) { m_set.insert(ctx.begin(), ctx.end()); }
    void merge(Context &ctx) { m_set.merge(ctx.m_set); }
    void remove(const ContextId &id) { m_set.erase(id); }
    void clear() { m_set.clear(); }

    QStringList toStringList() const;

    using const_iterator = std::unordered_set<ContextId>::const_iterator;
    const_iterator begin() const { return m_set.begin(); }
    const_iterator end() const { return m_set.end(); }
    const_iterator cbegin() const { return m_set.cbegin(); }
    const_iterator cend() const { return m_set.cend(); }

    bool operator==(const Context &o) const { return m_set == o.m_set; }
    bool operator!=(const Context &o) const { return !(*this == o); }
    friend QDebug operator<<(QDebug debug, const Context &set);

private:
    // std::unordered_set 是一个‌无序‌且‌不支持随机访问（即不支持下标索引）‌
    // 所以本类中不提供 at、indexOf 和 removeAt 访问功能
    std::unordered_set<ContextId> m_set{};
};

} // namespace bakuon::gui
