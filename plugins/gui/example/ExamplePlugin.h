#pragma once

#include <QtCore/QObject>

// 插件实现应当只依赖 include/bakuon/gui/ 下的公开门面，
// 不要直接 #include "gui/b_xxx.h"（那是 bakuon 内部实现细节）。
#include "bakuon/gui/Plugin.h"

namespace bakuon::plugins::gui {

/**
 * @brief bakuon_add_plugin() 的最小可用示例
 * @note 仅用于验证“插件作为 MODULE 动态库构建 + Q_PLUGIN_METADATA 元数据”这条链路，
 *       不注册任何真实的扩展点，DisabledByDefault 也因此设为 true（见 example_plugin.json）。
 */
class ExamplePlugin final : public QObject, public bakuon::gui::Plugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.bakuon.plugin" FILE "example_plugin.json")
    Q_INTERFACES(bakuon::gui::Plugin)

public:
    [[nodiscard]] QString id() const override { return QStringLiteral("com.bakuon.example"); }
    [[nodiscard]] QString name() const override { return QStringLiteral("Example Plugin"); }
    [[nodiscard]] QString version() const override { return QStringLiteral("1.0.0"); }
    [[nodiscard]] QString description() const override;

    bool initialize(bakuon::gui::PluginContext& ctx) override;
    void shutdown() override;
};

} // namespace bakuon::plugins::gui
