#include "gui/b_context.h"

namespace bakuon::gui {

Context::Context(std::initializer_list<ContextId> list)
    : m_set(list)
{
}

Context::Context(const ContextId &id)
    : m_set({id})
{
}

QStringList Context::toStringList() const
{
    QStringList names;
    for (const auto &id : m_set) {
        names.append(id.name());
    }
    return names;
}

QDebug operator<<(QDebug debug, const Context &set)
{
    debug.nospace() << "Context(";
    auto it  = set.begin();
    auto end = set.end();
    if (it != end) {
        debug << *it;
        ++it;
    }
    while (it != end) {
        debug << ", " << *it;
        ++it;
    }
    debug << ')';

    return debug;
}

} // namespace bakuon::gui
