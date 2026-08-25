#pragma once

#include <optional>
#include <vector>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace bakuon::gui {

/**
 * @brief 插件启用策略
 */
enum class PluginEnablePolicy {
    EnabledByDefault,  // 默认启用
    DisabledByDefault, // 默认禁用
    ForceEnabled,      // 强制启用（用户或系统要求）
    ForceDisabled      // 强制禁用（用户或系统要求）
};

/**
 * @brief 插件依赖
 */
struct PluginDependency
{
    enum class RequireType {
        Optional
        = 0, // 可选依赖，缺失时插件可以加载但功能受限（当前 PluginSystem 不做区分处理，见 b_pluginsystem.cpp 的 TODO）
        Required = 1, // 必需依赖，缺失/循环依赖时该插件的 Resolving 阶段直接失败
        Test     = 2  // 测试依赖，仅在测试环境需要（当前未做特殊处理）
    };

    QString id;
    QString name;
    QString version;
    RequireType type = RequireType::Required;

    friend bool operator==(const PluginDependency &lhs, const PluginDependency &rhs) noexcept
    {
        return lhs.id == rhs.id && lhs.version == rhs.version;
    }
};

/**
 * @brief 插件启动参数
 * @note 插件启动参数选项的值 value 可能从运行时动态传入
 */
struct PluginArgument
{
    QString name;        // 参数名称
    QString option;      // 参数选项（如：--theme=<value>）
    QString description; // 参数说明
};

/**
 * @brief 单个插件的元数据。
 *
 * 两个来源：
 *  - 动态库插件：PluginPipeline 在 Validating 阶段解析自 Q_PLUGIN_METADATA 内嵌的 JSON
 *    （见 include/bakuon/gui/IPlugin.h 头部注释里的 JSON 示例，字段与本结构体一一对应）。
 *  - 内置插件：PluginPipeline 构造时直接从 IPlugin 实例的虚函数读取（id()/name()/version()/
 *    description()/dependencies()），没有 json 文件，因此 vendor/copyright/license/url/platform/
 *    experimental/required/enablePolicy/arguments 这些字段留空/默认值——IPlugin 接口目前也没有
 *    暴露这些访问器，如果以后内置插件也需要它们，需要先扩展 IPlugin。
 */
struct PluginMetadata
{
    QString id; // 用作字符串查询 key，必须非空
    QString name;
    QString version;
    QString compatVersion;
    QString category;
    QString description;
    QString vendor;
    QString copyright;
    QString license;
    QString url;
    QString platform;
    bool experimental               = false;
    bool required                   = false;
    PluginEnablePolicy enablePolicy = PluginEnablePolicy::EnabledByDefault;

    std::vector<PluginDependency> dependencies;
    std::vector<PluginArgument> arguments;

    QString filePath; // 内置插件为空
};

/**
 * @brief 解析插件 json 元数据。
 * @param json 对应 QPluginLoader::metaData().value("MetaData").toObject()（已经剥掉 Qt 自己
 *             生成的外层包装），即插件作者写在 .json 文件里的原始内容本身。
 * @param errorOut 非空时，失败原因写入这里（当前只有 "Id 缺失或为空" 一种情况）。
 * @note 纯函数，不依赖 QPluginLoader/文件系统，可以脱离真实插件文件单独单元测试。
 */
inline std::optional<PluginMetadata> parsePluginMetadataJson(const QJsonObject &json,
                                                             QString *errorOut = nullptr)
{
    const QString id = json.value(QLatin1String("Id")).toString();
    if (id.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("字段 'Id' 缺失或值为空");
        }
        return std::nullopt;
    }

    PluginMetadata meta;
    meta.id            = id;
    meta.name          = json.value(QLatin1String("Name")).toString();
    meta.version       = json.value(QLatin1String("Version")).toString();
    meta.compatVersion = json.value(QLatin1String("CompatVersion")).toString();
    meta.category      = json.value(QLatin1String("Category")).toString();
    meta.description   = json.value(QLatin1String("Description")).toString();
    meta.vendor        = json.value(QLatin1String("Vendor")).toString();
    meta.copyright     = json.value(QLatin1String("Copyright")).toString();
    meta.license       = json.value(QLatin1String("License")).toString();
    meta.url           = json.value(QLatin1String("Url")).toString();
    meta.platform      = json.value(QLatin1String("Platform")).toString();
    meta.experimental  = json.value(QLatin1String("Experimental")).toBool();
    meta.required      = json.value(QLatin1String("Required")).toBool();
    meta.enablePolicy  = json.value(QLatin1String("DisabledByDefault")).toBool()
                             ? PluginEnablePolicy::DisabledByDefault
                             : PluginEnablePolicy::EnabledByDefault;

    const QJsonArray deps = json.value(QLatin1String("Dependencies")).toArray();
    meta.dependencies.reserve(static_cast<size_t>(deps.size()));
    // clang: -Wrange-loop-bind-reference -> "const QJsonValue &v"
    for (const QJsonValue v : deps) {
        const QJsonObject depObj = v.toObject();
        const QString depId      = depObj.value(QLatin1String("Id")).toString();
        if (depId.isEmpty()) {
            continue;
        }
        PluginDependency dep;
        dep.id      = depId;
        dep.name    = depObj.value(QLatin1String("Name")).toString();
        dep.version = depObj.value(QLatin1String("Version")).toString();
        dep.type    = depObj.value(QLatin1String("Required")).toInt(1) != 0
                          ? PluginDependency::RequireType::Required
                          : PluginDependency::RequireType::Optional;
        meta.dependencies.push_back(std::move(dep));
    }

    const QJsonArray args = json.value(QLatin1String("Arguments")).toArray();
    meta.arguments.reserve(static_cast<size_t>(args.size()));
    // clang: -Wrange-loop-bind-reference -> "const QJsonValue &v"
    for (const QJsonValue v : args) {
        const QJsonObject argObj = v.toObject();
        PluginArgument arg;
        arg.name        = argObj.value(QLatin1String("Name")).toString();
        arg.option      = argObj.value(QLatin1String("Option")).toString();
        arg.description = argObj.value(QLatin1String("Description")).toString();
        meta.arguments.push_back(std::move(arg));
    }

    return meta;
}

} // namespace bakuon::gui
