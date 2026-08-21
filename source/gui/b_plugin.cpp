#pragma once

#include "gui/b_plugin.h"

namespace bakuon::gui {

class Plugin::Implementation
{
};

Plugin::Plugin()
    : d(new Implementation)
{
    // Do nothing
}

Plugin::~Plugin()
{
    // Do nothing
}

} // namespace bakuon::gui
