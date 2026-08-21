#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QEvent>
#include <QtWidgets/QWidget>

#include "gui/b_commandsystem.h"

namespace bakuon::gui {

class CommandManager;

// focus context provider
class ContextFocusRouter : public QObject
{
    Q_OBJECT
public:
    ContextFocusRouter(QObject* parent = nullptr);

    void addProviderWidget(QObject* widget, const ContextId& context);
    void removeProviderWidget(QObject* widget);
    void clear();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void handleFocusProvider(QObject* focusedWidget);
    QObject* findContextProvider(QObject* widget) const;
    void updateProvider(QWidget* old, QWidget* now);

private:
    QObject* m_lastProvider;
    ContextId m_lastContext;
    std::unordered_map<QObject*, ContextId> m_providers; // 记录打过标签的物理部件
};

} // namespace bakuon::gui
