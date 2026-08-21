#pragma once

#include <unordered_set>

#include <QtCore/QDebug>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

#include "gui/detail/b_types.h"

namespace bakuon::gui {

struct ContextHelp
{
    // TODO: IMPLEMENT ME
};

class ContextSet
{
public:
    ContextSet() = default;
    explicit ContextSet(std::initializer_list<ContextId> list);
    explicit ContextSet(ContextId id);
    ContextSet(const ContextSet &o)            = default;
    ContextSet &operator=(const ContextSet &o) = default;
    ~ContextSet()                              = default;

    size_t count() const { return m_set.size(); }
    bool contains(const ContextId &id) const { return m_set.contains(id); }
    bool empty() const { return m_set.empty(); }
    size_t capacity() const { return m_set.max_size(); }

    void reserve(size_t capacity) { m_set.reserve(capacity); }
    void append(const ContextId &id) { m_set.emplace(id); }
    void remove(const ContextId &id) { m_set.erase(id); }
    void clear() { m_set.clear(); }

    QStringList toStringList() const;

    using const_iterator = std::unordered_set<ContextId>::const_iterator;
    const_iterator begin() const { return m_set.begin(); }
    const_iterator end() const { return m_set.end(); }
    const_iterator cbegin() const { return m_set.cbegin(); }
    const_iterator cend() const { return m_set.cend(); }

    bool operator==(const ContextSet &o) const { return m_set == o.m_set; }
    bool operator!=(const ContextSet &o) const { return !(*this == o); }
    friend QDebug operator<<(QDebug debug, const ContextSet &set);

private:
    // std::unordered_set 是一个‌无序‌且‌不支持随机访问（即不支持下标索引）‌
    // 所以本类中不提供 at、indexOf 和 removeAt 访问功能
    std::unordered_set<ContextId> m_set{};
};

// context provider
class Context : public QObject
{
    Q_OBJECT
public:
    enum class ActivationMode : quint8 {
        Foreground, // 默认(Interactive)：代表真实的用户交互（获得焦点、点击选中……），参与仲裁竞争
        Background, // 后台/异步任务：进入激活集合，但不参与"最近激活"的仲裁竞争
    };

    Context(QObject *parent = nullptr);
    ~Context() override = default;

    ActivationMode activateMode() const { return m_mode; }
    void setActivateMode(ActivationMode mode) { m_mode = mode; }

    // context id list
    ContextSet contexts() const { return m_contexts; }
    void setContexts(const ContextSet &set) { m_contexts = set; }

    // source
    QWidget *widget() const { return m_widget; }
    void setWidget(QWidget *widget) { m_widget = widget; }

    ContextHelp help() const { return m_help; }
    void setHelp(const ContextHelp &help) { m_help = help; }

    friend QDebug operator<<(QDebug debug, const Context &context);

private:
    ActivationMode m_mode;
    QPointer<QWidget> m_widget;
    ContextHelp m_help;
    ContextSet m_contexts{};
};

} // namespace bakuon::gui
