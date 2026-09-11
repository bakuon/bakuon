#include "plugin_p.h"

namespace bakuon::plugin {

Plugin::Plugin()
    : d(new Implementation)
{
    // Do nothing
}

Plugin::~Plugin()
{
    // Do nothing
}

bool Plugin::hasInterface(const std::string &interfaceName, bool demangle) const
{
    return false;
}

void *Plugin::resoleInterface(const std::string &interfaceName) const
{
    return nullptr;
}

} // namespace bakuon::plugin
