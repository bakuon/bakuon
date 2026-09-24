#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/b_archive.h"
#include "core/b_entity.h"
#include "core/b_result.h"

namespace bakuon::core::command {

/**
 * @brief 布局节点的类型——与旧 gui::CommandItem::Type 一一对应，去掉了只在
 *        Qt Model/View 适配层才有意义的 Root（本类里"根"是一个具体的、
 *        类型为 Container 的 Entity，见 CommandLayout::root()，不需要一个
 *        专门表达"不存在"的枚举值）。
 */
enum class LayoutItemType : std::uint8_t {
    Container = 0, ///< 菜单/子菜单/工具栏分组
    Command,       ///< 命令引用（对应某个 command::Command 的字符串 id）
    Separator,     ///< 分隔线
    Section,       ///< 不可点击的分组标题
    Custom = 100,  ///< 预留给 gui 层自定义的节点类型，core 不解释其含义
};

/// 节点类型标签，任何被 CommandLayout 创建出来的 Entity 都带这个组件。
struct LayoutItem
{
    LayoutItemType type = LayoutItemType::Container;
};

/// Container/Section 的显示标题；Separator 不使用（展示样式交给 gui 层决定，
/// 不像旧版那样把 "──────" 这种展示细节硬编码进数据层）。
struct LayoutTitle
{
    std::string text;
};

/// Command 节点引用的命令字符串 id——只是弱引用，不持有到 command::Command
/// Entity 的指针/句柄，语义与旧 CommandItem::CommandRole 存 QString 完全一致：
/// 布局树和命令注册表是两个独立子系统，只通过字符串 id 关联。
struct LayoutCommandRef
{
    std::string commandId;
};

/// 空标签：标记某个 Entity 是它所属 CommandLayout 实例的根节点。
/// 存在的意义见 CommandLayout::importBytes() 的实现注释——反序列化后不能假设
/// 根节点的原始 entt entity 数值依然等于构造时分配的那个，必须靠这个标签
/// 重新定位，而不是记一个裸 Entity 值再祈祷它还有效。
struct LayoutRoot
{
};

// 见 core/b_archivable.h 的 ArchivableComponent 概念约定：LayoutTitle/
// LayoutCommandRef 持有 std::string，不是可平凡拷贝类型，必须提供这一对
// ADL 自由函数才能参与 Serializer<Components...>（写法与 b_components.h 里
// Name/Tag 的 archive_write/archive_read 完全一致）。
void archive_write(IArchiveWriter& ar, const LayoutTitle& v);
void archive_read(IArchiveReader& ar, LayoutTitle& v);
void archive_write(IArchiveWriter& ar, const LayoutCommandRef& v);
void archive_read(IArchiveReader& ar, LayoutCommandRef& v);

/**
 * @brief Qt-free 的菜单/工具栏分组布局树。
 *
 * 对应旧 gui::CommandLayout（TreeNode<ItemDataList> + QVariant + QJsonObject），
 * 但结构层面换成了 core::hierarchy 的十字链表（Entity + Hierarchy 组件），
 * 数据层面换成了几个固定的强类型组件（LayoutItem/LayoutTitle/
 * LayoutCommandRef），不再需要"每个节点一个 QVariant map + 手写 role 枚举"
 * 这套间接。
 *
 * ## 为什么持有私有 Registry，而不是像 command::CommandRegistry 那样接一个
 * 外部 Registry&
 * `core::Serializer<Components...>` 序列化的是它拿到的那个 Registry 的
 * *全部* 实体，没有"只序列化某棵子树"这个概念（见 b_serializer.h 类文档）。
 * 如果布局树的 Entity 和 command::Command / command::CommandContext 的
 * Entity 共用同一个 Registry，导出布局时会把命令/上下文也一并序列化进去，
 * 这不是我们想要的。让每个 CommandLayout 实例私有持有一份独立 Registry
 * （与 core::Domain 持有私有 Container 是同一个理由），
 * `exportBytes()`/`importBytes()` 天然只覆盖这一棵布局树，边界干净。
 *
 * ## 持久化
 * 本类只负责"内存数据 <-> 字节流"（委托给 core::Serializer，格式与 Qt
 * 完全无关）。"字节流写到哪个文件、用什么写入策略（原子写/备份/对话框）"
 * 是宿主环境的职责，本类不提供 save(path)/load(path)——那需要文件系统访问，
 * 不属于"数据模型"这一层。真实的落地参照见：
 *   - tests/core/CommandLayoutTest.cpp（Qt-free，用 std::ofstream/ifstream
 *     演示一次完整、可运行、经过测试验证的落盘往返）；
 *   - examples/core/command_layout_persistence_reference.cpp（面向 gui 层的
 *     参照实现，用 QSaveFile/QFile 包一层同样的 exportBytes()/importBytes()
 *     调用——这段代码没有接入构建系统，是给 gui 层落地时直接抄的模板，
 *     不是本次改动范围）。
 *
 * 不可拷贝（Registry 拷贝语义代价高昂且容易被误用，与 core::Container 的既有
 * 约束一致）；可移动。
 */
class CommandLayout
{
public:
    CommandLayout();
    ~CommandLayout();

    CommandLayout(const CommandLayout&)                = delete;
    CommandLayout& operator=(const CommandLayout&)     = delete;
    CommandLayout(CommandLayout&&) noexcept            = default;
    CommandLayout& operator=(CommandLayout&&) noexcept = default;

    /// 根节点（"不可见根"）；类型恒为 Container，永远有效，不可被 remove()。
    [[nodiscard]] Entity root() const noexcept { return m_root; }

    [[nodiscard]] Registry& registry() noexcept { return m_registry; }
    [[nodiscard]] const Registry& registry() const noexcept { return m_registry; }

    // ---- 查询 ----

    /// item 无效（未创建/已销毁）时返回 LayoutItemType::Container 作为哨兵；
    /// 调用前应自行用 registry().valid(item) 判断，不要单靠返回值区分。
    [[nodiscard]] LayoutItemType type(Entity item) const;
    /// Container/Section 的标题；item 不携带 LayoutTitle 时返回空串。
    [[nodiscard]] std::string title(Entity item) const;
    /// Command 节点引用的命令 id；item 不携带 LayoutCommandRef 时返回空串。
    [[nodiscard]] std::string commandId(Entity item) const;

    [[nodiscard]] Entity parentOf(Entity item) const;
    /// parent 为 nullentity（或已失效）时按根节点处理，与旧 CommandItem{} 默认参数语义一致。
    [[nodiscard]] Entity itemAt(std::size_t index, Entity parent = nullentity) const;
    [[nodiscard]] std::size_t count(Entity parent = nullentity) const;
    [[nodiscard]] bool isEmpty() const;

    [[nodiscard]] std::vector<std::size_t> path(Entity item) const;
    [[nodiscard]] Entity itemFromPath(std::span<const std::size_t> path) const;
    [[nodiscard]] bool isValidPath(std::span<const std::size_t> path) const;

    // ---- 编辑 ----
    // parent 为 nullentity 时一律落到根节点；index 越界或 < 0 时追加到末尾。

    Entity addContainer(std::string title, Entity parent = nullentity, int index = -1);
    Entity addCommand(std::string commandId, Entity parent = nullentity, int index = -1);
    /// 根节点不允许直接挂分隔线（与旧版顶层菜单栏不支持分隔线的约束一致），
    /// parent 解析为根时返回 nullentity。
    Entity addSeparator(Entity parent, int index = -1);
    /// 同上：根节点不允许直接挂 Section。
    Entity addSection(std::string title, Entity parent, int index = -1);

    /// 摘除并销毁整棵子树；根节点不可移除。
    bool remove(Entity item);
    /// 把 item（及其子树）移动到 newParent 下的 index 位置；成环/自我挂接会被拒绝。
    bool move(Entity item, Entity newParent, int index = -1);

    // ---- 序列化：委托给 core::Serializer<Components...>；格式细节见类文档。 ----

    /// 注意：非 const——Serializer 的构造函数要求非 const Registry&，即使
    /// save() 本身只读，这是 core::Serializer 现有接口的既定形状（见
    /// b_serializer.h：`explicit Serializer(Registry& registry)`），这里不
    /// 通过 const_cast 掩盖这一点。
    [[nodiscard]] std::vector<std::byte> exportBytes();

    /// 整体替换当前内容（内部先 clear() 再重建）；失败时返回 Result::error()，
    /// 此时布局树处于"已清空"状态，不回滚到导入前的旧内容——与
    /// core::Serializer::load() 的既有失败语义一致，见其类文档。
    [[nodiscard]] Result<void> importBytes(std::span<const std::byte> bytes);

private:
    [[nodiscard]] Entity resolveParent(Entity parent) const noexcept;
    Entity insertAt(Entity parent, Entity child, int index);

private:
    Registry m_registry;
    Entity m_root{nullentity};
};

} // namespace bakuon::core::command
