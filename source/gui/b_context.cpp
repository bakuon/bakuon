#include "gui/b_context.h"

namespace bakuon::gui {

ContextSet::ContextSet(std::initializer_list<ContextId> list)
    : m_set(list)
{
}

ContextSet::ContextSet(ContextId id)
    : m_set({std::move(id)})
{
}

QStringList ContextSet::toStringList() const
{
    QStringList names;
    for (const auto &id : m_set) {
        names.append(id.value());
    }
    return names;
}

Context::Context(QObject *parent)
    : QObject(parent)
    , m_mode(ActivationMode::Foreground)
{
}

QDebug operator<<(QDebug debug, const ContextSet &set)
{
    debug.nospace() << "ContextSet(";
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

QDebug operator<<(QDebug debug, const Context &context)
{
    debug.nospace() << "Context([";
    auto it  = context.m_contexts.begin();
    auto end = context.m_contexts.end();
    if (it != end) {
        debug << *it;
        ++it;
    }
    while (it != end) {
        debug << ", " << *it;
        ++it;
    }
    debug << "],";

    const QString mode = (context.m_mode == Context::ActivationMode::Foreground
                              ? QLatin1String("Foreground")
                              : QLatin1String("Background"));
    debug << "mode:" << mode;

    if (context.m_widget) {
        debug << "widget:{" << context.m_widget->metaObject()->className() << ", "
              << &context.m_widget << "}";
    } else {
        debug << "widget: null";
    }

    debug << ')';

    return debug;
}

} // namespace bakuon::gui
