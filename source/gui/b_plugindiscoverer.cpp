#include "gui/b_plugindiscoverer.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QPluginLoader>
#include <QtCore/QQueue>

namespace bakuon::gui {

PluginDiscoverer::PluginDiscoverer(QObject *parent)
    : QObject(parent)
{
}

bool PluginDiscoverer::discover(const QString &filePath)
{
    return false;
}

size_t PluginDiscoverer::discoverDirectory(const QString &directory, bool recursive)
{
    return 0;
}

} // namespace bakuon::gui
