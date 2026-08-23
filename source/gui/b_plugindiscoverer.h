#pragma once

#include <QtCore/QObject>

namespace bakuon::gui {

/**
 * @brief 插件发现器 —— 插件动态库元数据解析与拓扑依赖关系解析中心
 *
 * ## 核心职责
 *   1. **发现**：扫描动态库文件，解析 JSON 元数据
 *   2. **注册内置插件**：无需 JSON，直接从 Plugin 接口读取元数据
 *   3. **依赖解析**：循环依赖检测（DFS）+ 拓扑排序（Kahn），返回合法加载顺序
 *
 * ## 不负责
 *   - 动态库的实际加载/卸载
 *   - Plugin 生命周期
 *
 * ## 扩展
 *   - watchDirectory() / unwatchDirectory()：目录实时监控
 *
 * ## 线程安全
 *   所有方法应在同一线程调用（通常为主线程）。
 */
class PluginDiscoverer : public QObject
{
    Q_OBJECT
public:
    explicit PluginDiscoverer(QObject *parent = nullptr);
    ~PluginDiscoverer() override = default;

    // ── 插件发现 ───

    bool discover(const QString &filePath);
    // 返回数量还是文件列表
    size_t discoverDirectory(const QString &directory, bool recursive = false);

    // ── 依赖解析 ───

Q_SIGNALS:
    // 插件成功发现并通过元数据验证（包括内置插件注册）
    void discovered(const QString &filePath);

    // 插件发现失败（文件损坏 / IID 不匹配 / JSON 缺失必要字段等）
    void discoveryFailed(const QString &filePath, const QString &reason);

    // ── 目录监控扩展 ────────────────────────────
    /// 目录监控：检测到新增插件文件
    void pluginFileAdded(const QString &filePath);
    /// 目录监控：检测到插件文件被删除
    void pluginFileRemoved(const QString &filePath);

private:
    QStringList m_watchedDirectories;
};

} // namespace bakuon::gui
