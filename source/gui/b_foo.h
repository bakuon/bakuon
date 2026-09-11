#pragma once

#include <QtCore/QObject>

#include "gui/b_gui_export.h"

namespace bakuon::gui {

class BAKUON_GUI_EXPORT Foo : public QObject
{
    Q_OBJECT
public:
    explicit Foo(QObject *parent = nullptr);
    ~Foo() override;
};

} // namespace bakuon::gui
