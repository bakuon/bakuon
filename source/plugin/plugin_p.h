#include "plugin.h"

#include <map>

namespace bakuon::plugin {

class Plugin::Implementation
{
public:
    std::map<std::string, void *> interfaces;
};

} // namespace bakuon::plugin
