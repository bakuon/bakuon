#pragma once

#include <QtCore/QMetaObject>
#include <QtCore/QObject>

#include "gui/b_types.h"

class QItemSelectionModel;

namespace bakuon::gui {

class ContextSelectionRouter : public QObject
{
    Q_OBJECT
public:
    explicit ContextSelectionRouter(const ContextId& context, QObject* parent = nullptr,
                                    ContextTier tier = ContextTier::Foreground);
    ~ContextSelectionRouter() override;

    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const noexcept { return m_selected; }
    [[nodiscard]] const ContextId& context() const noexcept { return m_context; }

    void route(QItemSelectionModel* selectionModel);

private:
    ContextId m_context;
    ContextTier m_tier;
    bool m_selected = false;
    QMetaObject::Connection m_selectionConnection;
};

} // namespace bakuon::gui
