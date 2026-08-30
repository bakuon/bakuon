#pragma once

#include <unordered_map>

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

#include "gui/b_context.h"
#include "gui/b_gui_export.h"

namespace bakuon::gui {

// focus context provider
class BAKUON_GUI_EXPORT ContextFocusRouter : public QObject
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
    void popCurrent();

private:
    bool m_installed = false;
    QPointer<QObject> m_currentProvider;
    Context m_currentContext;
    std::unordered_map<QObject*, Context> m_providers; // 记录打过标签的物理部件
};

} // namespace bakuon::gui
