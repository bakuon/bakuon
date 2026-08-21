#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QEvent>
#include <QtWidgets/QWidget>

#include "gui/b_context.h"

namespace bakuon::gui {

class CommandManager;

// 动态属性的键名：用于在任意 QObject/QWidget 上标记"获得焦点时应激活的上下文集合"。
constexpr char kProviderContextsPropertyName[] = "bakuon_provider_contexts";

static void setProviderContexts(QObject* widget, const Context& context)
{
    Q_ASSERT(widget != nullptr);
    widget->setProperty(kProviderContextsPropertyName, context.toStringList());
}

static Context providerContext(const QObject* widget)
{
    if (!widget) {
        return {};
    }
    const QVariant v = widget->property(kProviderContextsPropertyName);
    if (!v.isValid()) {
        return {};
    }

    Context result;
    const QStringList names = v.toStringList();
    result.reserve(static_cast<size_t>(names.size()));
    for (const QString& name : names) {
        if (ContextId id{name}; id.isValid()) {
            result.append(id);
        }
    }
    return result;
}

// focus context provider
class ContextFocusRouter : public QObject
{
    Q_OBJECT
public:
    explicit ContextFocusRouter(QObject* parent = nullptr);
    ~ContextFocusRouter() override;

    void addProviderWidget(QObject* widget, const Context& context);
    void removeProviderWidget(QObject* widget);
    void clearProviderWidget();

    void install();
    void uninstall();
    [[nodiscard]] bool isInstalled() const noexcept { return m_installed; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void handleFocusIn(QObject* gainer);
    QObject* findContextProvider(QObject* object) const;

private:
    bool m_installed = false;
    QPointer<QObject> m_currentProvider;
    Context m_currentContext;
    std::unordered_map<QObject*, Context> m_providers; // 记录打过标签的物理部件
};

} // namespace bakuon::gui
