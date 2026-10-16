#pragma once

#include <memory>

#include <bakuon/gui/IExtensionSystem.h>

#include "core/b_extension.h"
#include "gui/b_gui_export.h"

namespace bakuon::gui {

/**
 * @brief 扩展系统：ECS 里的一个 System ———— 对 registry 中"扩展点实体 / 扩展实体"的
 *        注册、排序、查询、销毁逻辑，自身只持有 Registry 引用和 id 索引
 *
 * ## 归属与访问（没有单例）
 * 它是 Domain 的一个服务，由最上层创建并持有：
 * @code
 *   // Application / Workspace 启动时
 *   Domain& app = workspace.require("app", [](Domain& d) {
 *       d.emplaceService<ExtensionSystem>(d.registry());
 *   });
 *
 *   // 插件侧：PluginContext 只暴露接口，且是"某个领域"的扩展系统
 *   IExtensionSystem* ext = workspace.find("app")->tryService<ExtensionSystem>();
 * @endcode
 * 不同领域（例如每个沙箱/每个文档）各自拥有隔离的扩展点表，互不可见——这正是单例
 * 做不到、而原来依赖"进程隔离"才获得的隔离性，现在在进程内也成立。
 * 单例方案下需要 IExtensionSystem* 显式注入以绕开 STATIC 库的多份单例问题；现在没有任何
 * 全局状态，插件库即便静态链接了自己的一份本类代码，操作的也始终是宿主传入的那个实例的数据。
 *
 * ## 数据模型
 * 扩展点和扩展都是实体，其关系完全由组件表达，见 core/b_extension.h 顶部的组件表。
 * 扩展点 id → 实体的唯一性与索引复用 BasicTagger<ExtensionPointKey>（id 空间与其它
 * Tagger 隔离，见 b_tagger.h）。
 *
 * ## 生命周期与一致性
 *  - 扩展实体被销毁（无论经由 remove()、unregisterPoint()、还是 registry.destroy()/clear()）
 *    时，on_destroy<ExtensionOf> 监听器负责把它从所属扩展点的有序列表里摘除。
 *  - 扩展点实体不要直接 registry.destroy()：请用 unregisterPoint()，它会先销毁名下的扩展实体；
 *    直接销毁扩展点会让名下扩展成为孤儿（查询时因句柄校验失败而不可见，但实体仍占内存）。
 *  - 必须先于 Registry 销毁（作为 Domain 服务时由 Domain 的逆序销毁保证）。
 *
 * ## 线程模型
 * 非线程安全：与 Registry/Domain 一致，线程亲和性由持有该领域的一方决定（旧实现里的读写锁
 * 保护的是"全局单例被任意线程访问"这个前提，前提消失后锁也一并去掉）。
 * 需要跨线程派发的场景，先用 list()/resolve() 在领域线程取快照，再把 shared_ptr 交给工作线程。
 *
 * 不可拷贝、不可移动。
 *
 * @code
 * 示例：
 * /// 在内部：
 * // 定义扩展接口
 * class IEditor
 * {
 * public:
 *     // some interfaces
 *     // ...
 * };
 * Q_DECLARE_INTERFACE(bakuon::gui::IEditor, "com.bakuon.extension.IEditor")
 *
 * // 注册接口扩展点
 * ExtensionSystem system;
 * ExtensionPoint<IEditor> point = system.ensure<IEditor>(
 *     qobject_interface_iid<IEditor*>(), //"com.bakuon.extension.editor",
 *     "编辑器扩展点"", // description
 * );
 *
 * // 内部也可像插件一样使用扩展
 * IEditor* editor = point.extension<IEditor*>(
 *     [&](auto &editor) -> IEditor* {
 *         return editor->canHandle(filePath) ? editor.get() : nullptr;
 *     },
 *     nullptr  // 默认值
 * );
 * // 将编辑器的部件置于主窗口编辑器布局
 * QWidget *widget = editor->widget();
 * editorArea->addWidget(widget);
 *
 * ----------------------------------------------------------------------
 *
 * /// 在外部（插件 A）：
 * // 实现扩展接口
 * class TextEditor : public IEditor{};
 * IEditor *editor = new TextEditor;
 *
 * // 注册新扩展
 * IExtensionSystem *system = ctx.extensions("app");
 * auto point = system->point<IEditor>(qobject_interface_iid<IEditor*>());
 * point.add(editor, 100);
 *
 * // 使用扩展接口（插件 B）：
 * IExtensionSystem *system = ctx.extensions("app");
 * auto point = system->point<IEditor>(qobject_interface_iid<IEditor*>());
 * // 获取扩展
 * IEditor* editor = point.extension<IEditor*>(
 *     [&](auto &editor) -> IEditor* {
 *         return editor->canHandle(filePath) ? editor.get() : nullptr;
 *     },
 *     nullptr  // 默认值
 * );
 * editor->open(filePath); // TextEditor
 * @endcode
 */
class BAKUON_GUI_EXPORT ExtensionSystem : public IExtensionSystem
{
public:
    explicit ExtensionSystem(core::Registry &registry);
    ~ExtensionSystem() override;

    ExtensionSystem(const ExtensionSystem &)            = delete;
    ExtensionSystem &operator=(const ExtensionSystem &) = delete;
    ExtensionSystem(ExtensionSystem &&)                 = delete;
    ExtensionSystem &operator=(ExtensionSystem &&)      = delete;

    // IExtensionSystem —— 扩展点

    [[nodiscard]] ExtensionPointHandle registerPoint(std::string_view id,
                                                     std::string_view description,
                                                     std::uint64_t type) override;
    [[nodiscard]] ExtensionPointHandle ensurePoint(std::string_view id,
                                                   std::string_view description,
                                                   std::uint64_t type) override;
    bool unregisterPoint(std::string_view id) override;

    [[nodiscard]] bool hasPoint(std::string_view id) const override;
    [[nodiscard]] ExtensionPointHandle findPoint(std::string_view id) const override;
    [[nodiscard]] std::string pointId(ExtensionPointHandle point) const override;
    [[nodiscard]] std::uint64_t pointType(ExtensionPointHandle point) const override;
    [[nodiscard]] std::vector<std::string> pointIds() const override;

    // ---- 扩展 ----

    [[nodiscard]] ExtensionHandle add(ExtensionPointHandle point, std::shared_ptr<void> object,
                                      int priority) override;
    [[nodiscard]] ExtensionHandle add(ExtensionPointHandle point,
                                      std::function<std::shared_ptr<void>()> factory, int priority,
                                      bool singleton) override;

    bool remove(ExtensionHandle extension) override;
    bool setPriority(ExtensionHandle extension, int priority) override;

    [[nodiscard]] std::size_t count(ExtensionPointHandle point) const override;
    [[nodiscard]] std::vector<ExtensionHandle> list(ExtensionPointHandle point) const override;
    [[nodiscard]] std::shared_ptr<void> resolve(ExtensionHandle extension) override;

private:
    std::unique_ptr<core::ExtensionPoint> m_points;
    std::unique_ptr<core::Extension> m_extensions;
};

} // namespace bakuon::gui
