#include "gui/b_foo.h"

namespace bakuon::gui {

Foo::Foo(QObject *parent)
    : QObject(parent)
{
}

Foo::~Foo() = default;

} // namespace bakuon::gui
