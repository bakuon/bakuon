#pragma once

#include <QtCore/QString>
#include <QtCore/QVariantMap>

#include "bakuon/gui/PluginContext.h"

// ============================================================================
// bakuon::gui 插件开发 SDK —— 门面头文件（facade）
//
// include/bakuon/gui/ 与 source/gui/ 下的 b_ 前缀头文件是两回事：
//   - source/gui/b_xxx.h        内部实现，随时可能重命名/重构，仅供 bakuon 自身模块间使用
//   - include/bakuon/gui/xxx.h  面向第三方插件开发者的稳定门面，本身不应该包含任何新的
//                               实现代码，只做“重新导出”，方便未来内部重构时只需要同步
//                               更新这一层转发，而不必对已发布的第三方插件造成破坏性影响
//
// 插件开发者应该写：
//   #include "bakuon/gui/Plugin.h"
// 而不是直接：
//   #include "gui/b_plugin.h"
//
// 当前只是最小可用的转发，还没有做“类型隐藏 / ABI 隔离”，
// 即 bakuon::gui::Plugin 目前就是 source/gui/b_plugin.h 里定义的同一个类型，
// 没有额外包一层接口。等未来插件生态成型、确实需要在不破坏第三方插件的前提下
// 重构内部实现时，可以在这里改成真正的转发/适配层，而不改变本文件的 #include 路径约定。
// ============================================================================

namespace bakuon::gui {

/**
// 插件标准格式定义
{
    "MetaData": {
        "Id": "com.bakuon.editor.text",
        "Name": "Text Editor",
        "Version": "1.2.3",
        "CompatVersion": "1.0.0",
        "Category": "Editor",
        "Description": "Advanced text editor with syntax highlighting",
        "Vendor": "bakuon team",
        "Copyright": "Copyright (c) 2026",
        "License": "MIT",
        "Url": "https://example.com/plugins/texteditor",
        "Platform": "Windows",
        "Experimental": false,
        "Required": false,
        "DisabledByDefault": false,
        "Dependencies": [
            {
                "Id": "com.bakuon.core",
                "Name": "Core",
                "Version": "1.0.0",
                "Type": 0
            },
            {
                "Id": "com.bakuon.plugin.syntaxhighlighter",
                "Name": "Syntax Highlighter",
                "Version": "1.1.0",
                "Type": 1
            }
        ],
        "Arguments": [
            {
                "Name": "theme",
                "Parameter": "--theme=<name>",
                "Description": "Set the editor theme"
            },
            {
                "Name": "readonly",
                "Parameter": "--readonly",
                "Description": "Open in read-only mode"
            }
        ]
    }
}
*/

/**
 * @brief 插件接口
 * @version 1.0
 * @note 插件是扩展的容器（见ExtensionPoint），负责注册和管理扩展。
 * 
 * 生命周期阶段：
 * 1. 构造 -> 2. initialize() -> 3. extensionsInitialized() -> 4. shutdown()
 * 
 * 设计原则：
 * - initialize(): 注册扩展点、向 Application 注册服务提供者、初始化内部状态
 * - extensionsInitialized(): 所有插件加载完成，可安全使用其他插件的扩展
 * - shutdown(): 清理资源、注销扩展
 *
 * 开发期                    构建期                    运行时
 *   │                         │                        │
 *   ▼                         ▼                        ▼
 * 命名规范文档           CMake静态扫描            Registry重复检测
 * bakuon.* 保留         check_plugin_ids.py      + 清晰冲突报告
 * 反向域名约束           构建失败阻断              + ID/JSON一致性校验
 * (第三方无法侵入)       (官方插件100%覆盖)        (第三方插件兜底)
 *
 */
class IPlugin
{
public:
    IPlugin()          = default;
    virtual ~IPlugin() = default;

    /**
     * @brief 插件唯一标识符，返回值必须与 JSON 中的 Id 一致。
     * @details Id 必须与 JSON 中的 Id 一致，在宿主内部做必要的一致性验证断言
     * @example 
     *  官方插件："com.bakuon.editor.text" "com.bakuon.editor.markdown" "com.bakuon.tool.filebrowser"
     *  第三方插件："com.example.myplugin" "io.github.username.feature"
     */
    [[nodiscard]] virtual QString id() const = 0;

    /**
     * @brief 插件显示名称
     */
    [[nodiscard]] virtual QString name() const = 0;

    /**
     * @brief 插件版本
     */
    [[nodiscard]] [[nodiscard]] virtual QString version() const = 0;

    /**
     * @brief 插件描述
     */
    [[nodiscard]] virtual QString description() const { return {}; }

    /**
     * @brief 插件依赖（其他插件的ID）
     * @return 依赖的插件ID列表
     */
    [[nodiscard]] virtual QStringList dependencies() const { return {}; }

    /**
     * @brief 获取插件元数据
     */
    [[nodiscard]] virtual QVariantMap metadata() const { return {}; }

    // 生命周期

    /**
     * @brief 初始化插件 register extensions
     * @param ctx 插件初始化上下文，提供对 IApplication 和启动参数的访问
     * @return 成功返回true；false 初始化失败（插件系统将回滚）
     * @note 此时其他插件可能尚未完成初始化，不应依赖其他插件提供的服务。
     *       如需跨插件交互，请在 extensionsInitialized() 中进行。
     * 
     * 在此阶段：
     * - 向 ctx.app() 注册自身的 IServiceProvider
     * - 注册扩展到扩展点
     * - 初始化插件内部状态
     * - 注册服务
     * 
     * 注意：此时其他插件可能还未加载
     */
    virtual bool initialize(PluginContext &ctx) = 0;

    /**
     * @brief 所有插件初始化完成后调用
     * 
     * 在此阶段：
     * - 可以安全地使用其他插件提供的扩展
     * - 建立插件间的连接
     */
    virtual void extensionsInitialized() {}

    /**
     * @brief 关闭插件
     * 
     * 在此阶段：
     * - 注销所有扩展
     * - 释放资源
     * - 断开连接
     */
    virtual void shutdown() = 0;
};

} // namespace bakuon::gui

Q_DECLARE_INTERFACE(bakuon::gui::IPlugin, "com.bakuon.plugin")
