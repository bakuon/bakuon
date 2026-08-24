#include "gui/b_plugindiscoverer.h"

#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QLibrary>
#include <QtCore/QPluginLoader>

namespace bakuon::gui {

namespace {

// 元数据JSON键名常量
namespace Meta {
constexpr QLatin1String METADATA{"MetaData"};
constexpr QLatin1String ID{"Id"}; // 插件唯一标识
constexpr QLatin1String NAME{"Name"};
constexpr QLatin1String VERSION{"Version"};
constexpr QLatin1String COMPAT_VERSION{"CompatVersion"};
constexpr QLatin1String CATEGORY{"Category"};
constexpr QLatin1String DESCRIPTION{"Description"};
constexpr QLatin1String VENDOR{"Vendor"};
constexpr QLatin1String COPYRIGHT{"Copyright"};
constexpr QLatin1String LICENSE{"License"};
constexpr QLatin1String URL{"Url"};
constexpr QLatin1String PLATFORM{"Platform"};
constexpr QLatin1String EXPERIMENTAL{"Experimental"};
constexpr QLatin1String REQUIRED{"Required"};
constexpr QLatin1String DISABLED_BY_DEFAULT{"DisabledByDefault"};
constexpr QLatin1String DEPENDENCIES{"Dependencies"};
constexpr QLatin1String ARGUMENTS{"Arguments"};
// 依赖字段
constexpr QLatin1String DEP_ID{"Id"};
constexpr QLatin1String DEP_NAME{"Name"};
constexpr QLatin1String DEP_VERSION{"Version"};
constexpr QLatin1String DEP_TYPE{"Type"};
// 参数字段
constexpr QLatin1String ARG_NAME{"Name"};
constexpr QLatin1String ARG_OPTION{"Option"};
constexpr QLatin1String ARG_DESCRIPTION{"Description"};

// 必须与 include/bakuon/gui/IPlugin.h 里 Q_DECLARE_INTERFACE 的 IID 字符串保持一致，
// 也是每个插件 Q_PLUGIN_METADATA(IID "...") 里应该填写的值。
constexpr QLatin1String EXPECTED_IID{"com.bakuon.plugin"};
} // namespace Meta

} // anonymous namespace

PluginDiscoverer::PluginDiscoverer(QObject *parent)
    : QObject(parent)
{
}

bool PluginDiscoverer::discover(const QString &filePath)
{
    if (!QLibrary::isLibrary(filePath)) {
        Q_EMIT discoveryFailed(filePath, QStringLiteral("不是有效的动态库文件"));
        return false;
    }

    // QPluginLoader::metaData() 只读取插件二进制里 Q_PLUGIN_METADATA 嵌入的 JSON 头，
    // 不会触发 dlopen/instance()，因此可以对大量候选文件廉价地批量调用。
    QPluginLoader probe(filePath);
    const QJsonObject root = probe.metaData();

    if (root.isEmpty()) {
        Q_EMIT discoveryFailed(filePath,
                               QStringLiteral("无法读取插件元数据（不是 Qt 插件，或已损坏）"));
        return false;
    }

    if (root.value(QLatin1String("IID")).toString() != Meta::EXPECTED_IID) {
        Q_EMIT discoveryFailed(filePath, QStringLiteral("IID 不匹配，不是 bakuon 插件"));
        return false;
    }

    // 注意这里有一层嵌套：Qt 的 Q_PLUGIN_METADATA(FILE "...") 机制会把
    // 整个 json 文件的内容原样包进它自己生成的顶层 "MetaData" 键里；IPlugin.h 的json
    // 文件内容不要包含进 "MetaData" 键里（见该文件头部的 JSON 示例）。
    const QJsonObject meta = root.value(QLatin1String(Meta::METADATA)).toObject();
    const QString id       = meta.value(QLatin1String(Meta::ID)).toString();
    if (id.isEmpty()) {
        Q_EMIT discoveryFailed(filePath, QStringLiteral("MetaData.Id 缺失或为空"));
        return false;
    }

    PluginMetadata info;
    info.id            = id;
    info.name          = meta.value(Meta::NAME).toString();
    info.version       = meta.value(Meta::VERSION).toString();
    info.compatVersion = meta.value(Meta::COMPAT_VERSION).toString();
    info.category      = meta.value(Meta::CATEGORY).toString();
    info.description   = meta.value(Meta::DESCRIPTION).toString();
    info.filePath      = filePath;

    const QJsonArray deps = meta.value(Meta::DEPENDENCIES).toArray();
    info.dependencyIds.reserve(deps.size());
    for (const QJsonValue v : deps) {
        const QString depId = v.toObject().value(Meta::ID).toString();
        if (!depId.isEmpty()) {
            info.dependencyIds.push_back(depId);
        }
    }

    m_metadata.insert(filePath, info);
    Q_EMIT discovered(filePath);
    return true;
}

size_t PluginDiscoverer::discoverDirectory(const QString &directory, bool recursive)
{
    QDir dir(directory);
    if (!dir.exists()) {
        return 0;
    }

    const auto flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(directory, QDir::Files, flags);

    size_t count = 0;
    while (it.hasNext()) {
        const QString filePath = it.next();
        if (QLibrary::isLibrary(filePath) && discover(filePath)) {
            ++count;
        }
    }
    return count;
}

std::optional<PluginMetadata> PluginDiscoverer::metadata(const QString &filePath) const
{
    const auto it = m_metadata.constFind(filePath);
    if (it == m_metadata.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

} // namespace bakuon::gui
