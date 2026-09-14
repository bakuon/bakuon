#pragma once

#include <vector>

#include <QtCore/QMetaType>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <bakuon/core/Registry.h>

#include "gui/b_gui_export.h"

namespace bakuon::gui {

/**
 * @brief 把 core::Registry 的 onConstruct/onUpdate/onDestroy 观察者转成 Qt 信号，
 * 方便 QAbstractItemModel / 自定义 Widget 直接 connect()，不需要认识
 * core::Connection/std::function 这一套非 Qt 的观察者接口——这是 core 的
 * "GUI 复杂状态数据驱动"真正落地到 Qt 界面前的最后一环胶水代码，core 本身
 * 完全不知道 Qt 的存在，这层胶水反过来也不应该往 core 里塞任何 Qt 相关代码。
 *
 * ## 为什么不是模板类
 * Qt 的元对象系统（Q_OBJECT / 信号槽）不支持类模板——RegistryBridge 因此设计成
 * "一个实例可以同时桥接多种组件类型"，用 componentTag（调用方在 watch<T>() 时
 * 自己起的字符串标签，比如 "Position"/"Selected"）区分信号来自哪个组件类型，
 * 而不是"每种组件类型各自生成一个桥接类"（那需要代码生成工具，本类刻意不
 * 追求那个复杂度）。
 *
 * ## 信号只携带"发生了什么"，不携带具体数值
 * entityConstructed/entityUpdated/entityDestroyed 的参数只有 componentTag 和
 * Handle，没有组件的具体数值——需要数据时槽函数自己回头调用
 * `registry.tryGet<T>(id)` 查询。这是刻意的：让信号签名保持统一、不随
 * Components 类型变化，也避免把任意组件类型（可能带着只有 core 认识的类型）
 * 打包进 QVariant 到处传递、引入不必要的 Qt 元类型注册负担。
 *
 * ## 使用方式
 * @code
 *   bakuon::core::Registry registry;
 *   auto* bridge = new bakuon::gui::RegistryBridge(registry, this);
 *   bridge->watch<Position>(QStringLiteral("Position"));
 *   bridge->watchSelection(); // Selected 标签 + selectionChanged
 *
 *   connect(bridge, &RegistryBridge::entityUpdated, this,
 *           [&registry](const QString& tag, bakuon::core::Handle id) {
 *               if (tag == QLatin1String("Position")) {
 *                   const auto* pos = registry.tryGet<Position>(id);
 *                   // ... 用 pos 刷新界面 ...
 *               }
 *           });
 *   connect(bridge, &RegistryBridge::selectionChanged, this, [this]() {
 *       // 槽内自行查询 Selection / each<Selected>
 *   });
 * @endcode
 */
class BAKUON_GUI_EXPORT RegistryBridge : public QObject
{
    Q_OBJECT
public:
    /**
     * @param registry 被桥接的 Registry；必须比 RegistryBridge 活得更久
     *                 （本类只持有引用，不管理它的生命周期，也不管理线程安全——
     *                 与 core::Registry 本身一致，只应在单线程/GUI 主线程使用）。
     */
    explicit RegistryBridge(core::Registry& registry, QObject* parent = nullptr);
    ~RegistryBridge() override;

    RegistryBridge(const RegistryBridge&)            = delete;
    RegistryBridge& operator=(const RegistryBridge&) = delete;

    /**
     * @brief 开始桥接组件类型 T：其 onConstruct/onUpdate/onDestroy 都会被转发成
     * 下面对应的信号，信号参数里的 componentTag 固定为这里传入的 tag。
     * @note 对同一个 T 重复调用 watch<T>() 会重复订阅、重复发信号——调用方应该
     * 保证每个组件类型只 watch() 一次（典型用法是在构造完 RegistryBridge 后
     * 集中调用一遍，不在运行期反复调用）。
     */
    template<typename T>
    void watch(const QString& componentTag)
    {
        m_connections.push_back(
            m_registry.onConstruct<T>([this, componentTag](core::Registry&, core::Handle id) {
                Q_EMIT entityConstructed(componentTag, id);
            }));
        m_connections.push_back(
            m_registry.onUpdate<T>([this, componentTag](core::Registry&, core::Handle id) {
                Q_EMIT entityUpdated(componentTag, id);
            }));
        m_connections.push_back(
            m_registry.onDestroy<T>([this, componentTag](core::Registry&, core::Handle id) {
                Q_EMIT entityDestroyed(componentTag, id);
            }));
    }

    /**
     * @brief 便捷订阅 Selected 标签，并额外发出 selectionChanged()。
     * 内部等价于 watch<components::Selected>(tag)，在三条实体信号之外再发集合级信号。
     */
    void watchSelection(const QString& tag = QStringLiteral("Selected"));

Q_SIGNALS:
    void entityConstructed(const QString& componentTag, bakuon::core::Handle id);
    void entityUpdated(const QString& componentTag, bakuon::core::Handle id);
    void entityDestroyed(const QString& componentTag, bakuon::core::Handle id);
    /// 选中集发生任意增删时发出（无参数；槽内自行查询 Selection / each<Selected>）
    void selectionChanged();

private:
    core::Registry& m_registry;
    // Connection 是 RAII 的：随本类析构自动断开全部订阅，不需要手写 shutdown 逻辑。
    std::vector<core::Connection> m_connections;
};

} // namespace bakuon::gui

// Handle 是 core 模块的类型，本身不认识/不应该认识 Qt——把它注册成 Qt 元类型
// 这件事放在"消费 Handle 的 Qt 侧"（也就是这里）来做，而不是塞进 b_handle.h，
// 与本类"胶水代码只应该单向依赖 core，不能反过来污染 core"的定位一致。
Q_DECLARE_METATYPE(bakuon::core::Handle)
