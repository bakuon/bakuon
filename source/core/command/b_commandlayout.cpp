#include "core/command/b_commandlayout.h"

#include "core/b_hierarchy.h"
#include "core/b_serializer.h"

namespace bakuon::core::command {

void archive_write(IArchiveWriter& ar, const LayoutTitle& v)
{
    ar.writeString(v.text);
}
void archive_read(IArchiveReader& ar, LayoutTitle& v)
{
    v.text = ar.readString();
}
void archive_write(IArchiveWriter& ar, const LayoutCommandRef& v)
{
    ar.writeString(v.commandId);
}
void archive_read(IArchiveReader& ar, LayoutCommandRef& v)
{
    v.commandId = ar.readString();
}

namespace {

namespace h = bakuon::core::hierarchy;

/// save()/load() 两端必须用完全相同顺序的组件列表（见 Serializer 类文档），
/// 因此固定成一个别名，杜绝两处各写一遍、顺序悄悄漂移的风险。
using LayoutSerializer
    = Serializer<h::Hierarchy, LayoutItem, LayoutTitle, LayoutCommandRef, LayoutRoot>;

} // namespace

CommandLayout::CommandLayout()
{
    m_root = m_registry.create();
    m_registry.emplace<LayoutItem>(m_root, LayoutItemType::Container);
    m_registry.emplace<LayoutRoot>(m_root);
}

CommandLayout::~CommandLayout() = default;

Entity CommandLayout::resolveParent(Entity parent) const noexcept
{
    return m_registry.valid(parent) ? parent : m_root;
}

Entity CommandLayout::insertAt(Entity parent, Entity child, int index)
{
    const Entity p = resolveParent(parent);

    Entity before = nullentity;
    if (index >= 0 && static_cast<std::size_t>(index) < h::childCount(m_registry, p)) {
        before = h::child(m_registry, static_cast<std::size_t>(index), p);
    }

    if (!h::attach(m_registry, child, p, before)) {
        // 挂接失败（比如 parent 本身非法/成环）：回收刚创建的孤儿实体，
        // 不留下一个既不在树里、外部也拿不到句柄的悬空 Entity。
        m_registry.destroy(child);
        return nullentity;
    }
    return child;
}

LayoutItemType CommandLayout::type(Entity item) const
{
    if (const auto* it = m_registry.try_get<LayoutItem>(item)) {
        return it->type;
    }
    return LayoutItemType::Container;
}

std::string CommandLayout::title(Entity item) const
{
    if (const auto* t = m_registry.try_get<LayoutTitle>(item)) {
        return t->text;
    }
    return {};
}

std::string CommandLayout::commandId(Entity item) const
{
    if (const auto* c = m_registry.try_get<LayoutCommandRef>(item)) {
        return c->commandId;
    }
    return {};
}

Entity CommandLayout::parentOf(Entity item) const
{
    return h::parent(m_registry, item);
}

Entity CommandLayout::itemAt(std::size_t index, Entity parent) const
{
    return h::child(m_registry, index, resolveParent(parent));
}

std::size_t CommandLayout::count(Entity parent) const
{
    return h::childCount(m_registry, resolveParent(parent));
}

bool CommandLayout::isEmpty() const
{
    return h::childCount(m_registry, m_root) == 0;
}

std::vector<std::size_t> CommandLayout::path(Entity item) const
{
    return h::path(m_registry, item);
}

Entity CommandLayout::itemFromPath(std::span<const std::size_t> path) const
{
    return h::pathNode(m_registry, m_root, path);
}

bool CommandLayout::isValidPath(std::span<const std::size_t> path) const
{
    return m_registry.valid(itemFromPath(path));
}

Entity CommandLayout::addContainer(std::string titleText, Entity parent, int index)
{
    const Entity item = m_registry.create();
    m_registry.emplace<LayoutItem>(item, LayoutItemType::Container);
    m_registry.emplace<LayoutTitle>(item, std::move(titleText));
    return insertAt(parent, item, index);
}

Entity CommandLayout::addCommand(std::string commandId, Entity parent, int index)
{
    const Entity item = m_registry.create();
    m_registry.emplace<LayoutItem>(item, LayoutItemType::Command);
    m_registry.emplace<LayoutCommandRef>(item, std::move(commandId));
    return insertAt(parent, item, index);
}

Entity CommandLayout::addSeparator(Entity parent, int index)
{
    const Entity p = resolveParent(parent);
    if (p == m_root) {
        return nullentity; // 与旧版一致：顶层（不可见根）不支持分隔线
    }
    const Entity item = m_registry.create();
    m_registry.emplace<LayoutItem>(item, LayoutItemType::Separator);
    return insertAt(p, item, index);
}

Entity CommandLayout::addSection(std::string titleText, Entity parent, int index)
{
    const Entity p = resolveParent(parent);
    if (p == m_root) {
        return nullentity; // 与旧版一致：顶层不支持 Section
    }
    const Entity item = m_registry.create();
    m_registry.emplace<LayoutItem>(item, LayoutItemType::Section);
    m_registry.emplace<LayoutTitle>(item, std::move(titleText));
    return insertAt(p, item, index);
}

bool CommandLayout::remove(Entity item)
{
    if (!m_registry.valid(item) || item == m_root) {
        return false;
    }
    h::destroy(m_registry, item);
    return true;
}

bool CommandLayout::move(Entity item, Entity newParent, int index)
{
    if (!m_registry.valid(item) || item == m_root) {
        return false;
    }
    const Entity dest = resolveParent(newParent);

    Entity before = nullentity;
    if (index >= 0 && static_cast<std::size_t>(index) < h::childCount(m_registry, dest)) {
        before = h::child(m_registry, static_cast<std::size_t>(index), dest);
        if (before == item) {
            return true; // 已经在目标位置，视为成功的空操作
        }
    }
    // hierarchy::attach() 内部已经处理自我挂接/成环检测（见 b_hierarchy.cpp），
    // 这里不需要重复校验。
    return h::attach(m_registry, item, dest, before);
}

std::vector<std::byte> CommandLayout::exportBytes()
{
    const LayoutSerializer serializer(m_registry);
    return serializer.save();
}

Result<void> CommandLayout::importBytes(std::span<const std::byte> bytes)
{
    LayoutSerializer serializer(m_registry);
    if (auto result = serializer.load(bytes); result.error()) {
        return result;
    }

    // 不能假设根节点的原始 entt entity 数值在反序列化后依然等于构造时分配
    // 的那个——同一个 Registry 自我往返时大概率相同（entt::snapshot_loader
    // 按原始数值重建，见 SerializerTest.cpp 的验证），但导入到"另一个刚构造
    // 好、已经自己创建过一个根实体"的 CommandLayout 实例时，两者的分配历史
    // 完全不同，不能指望数值巧合对上。唯一可靠的做法是靠 LayoutRoot 标签
    // 重新定位，不信任任何记下来的裸 Entity 值。
    Entity foundRoot      = nullentity;
    std::size_t rootCount = 0;
    m_registry.view<LayoutRoot>().each([&](Entity e) {
        foundRoot = e;
        ++rootCount;
    });

    if (rootCount != 1 || !m_registry.valid(foundRoot)) {
        return Fail<void>(StatusCode::DataLoss,
                          "归档中 LayoutRoot 标签数量不为 1（实际 " + std::to_string(rootCount)
                              + " 个），数据已损坏或不是合法的布局归档");
    }

    m_root = foundRoot;
    return Ok();
}

} // namespace bakuon::core::command
