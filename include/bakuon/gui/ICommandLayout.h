#pragma once

#include <cstddef>
#include <span>

#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <gui/b_gui_export.h>

namespace bakuon::gui {

class ICommandLayout;

class CommandItem
{
public:
    enum ItemRole : quint8 { DisplayRole, TypeRole, CommandRole, UserRole = 100 };
    enum class Type : quint8 {
        Root,      // 仅根节点使用，从不出现在序列化结果里（根是隐式的容器，不作为一个"节点"落盘）
        Container, // 菜单/子菜单/工具栏
        Command,   // 命令引用
        Separator, // 分隔线
        Section,   // Section Header: a non-clickable text item to the menu
        Custom = 100
    };

    CommandItem()
        : m_id(0)
        , m_layout(nullptr)
    {
    }

    [[nodiscard]] inline bool isValid() const noexcept { return m_id != 0 && m_layout != nullptr; }
    explicit inline operator bool() const noexcept { return isValid(); }

    [[nodiscard]] inline QVariant data(int role) const;
    [[nodiscard]] inline std::uintptr_t id() const { return m_id; }
    [[nodiscard]] inline void* pointer() const noexcept { return reinterpret_cast<void*>(m_id); }
    [[nodiscard]] inline CommandItem parent() const noexcept;
    [[nodiscard]] inline CommandItem sibling(std::size_t index) const noexcept;
    [[nodiscard]] inline std::size_t childCount() const noexcept;
    [[nodiscard]] inline bool hasChildren() const noexcept { return childCount() > 0; }
    [[nodiscard]] inline int index() const noexcept;
    [[nodiscard]] inline std::size_t depth() const noexcept;
    [[nodiscard]] inline std::vector<std::size_t> path() const;

    inline bool operator==(const CommandItem& other) const noexcept
    {
        return m_layout == other.m_layout && m_id == other.m_id;
    }
    inline bool operator!=(const CommandItem& other) const noexcept { return !(*this == other); }

private:
    friend class ICommandLayout;
    CommandItem(const ICommandLayout* layout, const void* ptr) noexcept
        : m_id(reinterpret_cast<std::uintptr_t>(ptr))
        , m_layout(layout)
    {
    }

    CommandItem(const ICommandLayout* layout, std::uintptr_t id) noexcept
        : m_id(id)
        , m_layout(layout)
    {
    }

private:
    std::uintptr_t m_id;
    const ICommandLayout* m_layout;
};

class BAKUON_GUI_EXPORT ICommandLayout
{
public:
    ICommandLayout()          = default;
    virtual ~ICommandLayout() = default;

    ICommandLayout(const ICommandLayout&)                = delete;
    ICommandLayout& operator=(const ICommandLayout&)     = delete;
    ICommandLayout(ICommandLayout&&) noexcept            = default;
    ICommandLayout& operator=(ICommandLayout&&) noexcept = default;

    [[nodiscard]] virtual CommandItem invisibleItem() const                      = 0;
    [[nodiscard]] virtual CommandItem parentItem(const CommandItem& child) const = 0;
    [[nodiscard]] virtual CommandItem itemAt(std::size_t index, const CommandItem& parent) const = 0;
    [[nodiscard]] virtual std::size_t count(const CommandItem& parent = {}) const noexcept = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept                                = 0;
    [[nodiscard]] virtual bool isEmpty() const noexcept                                    = 0;
    [[nodiscard]] virtual int itemIndex(const CommandItem& item) const                     = 0;
    [[nodiscard]] virtual std::size_t itemDepth(const CommandItem& item) const             = 0;
    [[nodiscard]] virtual std::vector<std::size_t> itemPath(const CommandItem& item) const = 0;
    [[nodiscard]] virtual CommandItem itemFromPath(
        std::span<const std::size_t> path) const noexcept                                    = 0;
    [[nodiscard]] virtual bool isValidPath(std::span<const std::size_t> path) const noexcept = 0;

    virtual bool add(CommandItem item, CommandItem parent, int index = -1)               = 0;
    virtual bool remove(CommandItem item)                                                = 0;
    virtual CommandItem take(CommandItem parent, int index)                              = 0;
    virtual bool move(CommandItem sourceItem, CommandItem targetParent, int targetIndex) = 0;
    virtual bool move(CommandItem sourceParent, int sourceIndex, CommandItem targetParent,
                      int targetIndex)                                                   = 0;

    virtual CommandItem addContainer(const QString& title, CommandItem parent = {},
                                     int index = -1)                                         = 0;
    virtual CommandItem addCommand(const QString& id, CommandItem parent, int index = -1)    = 0;
    virtual CommandItem addSeparator(CommandItem parent, int index = -1)                     = 0;
    virtual CommandItem addSection(const QString& title, CommandItem parent, int index = -1) = 0;

    virtual QVariant itemData(CommandItem item, int role) const                 = 0;
    virtual void setItemData(CommandItem item, int role, const QVariant& value) = 0;

    [[nodiscard]] virtual QJsonObject serialize() const = 0;
    virtual void deserialize(const QJsonObject& obj)    = 0;

    virtual bool save(const QString& filePath) const = 0;
    virtual bool load(const QString& filePath)       = 0;

    inline CommandItem createItem(const void* ptr = nullptr) const;
    inline CommandItem createItem(std::uintptr_t id) const;
};

inline QVariant CommandItem::data(int role) const
{
    return m_layout ? m_layout->itemData(*this, role) : QVariant{};
}
inline CommandItem CommandItem::parent() const noexcept
{
    return m_layout ? m_layout->parentItem(*this) : CommandItem{};
}
inline CommandItem CommandItem::sibling(std::size_t index) const noexcept
{
    return m_layout ? m_layout->itemAt(index, this->parent()) : CommandItem{};
}
inline std::size_t CommandItem::childCount() const noexcept
{
    return m_layout ? m_layout->count(*this) : 0;
}
inline int CommandItem::index() const noexcept
{
    return m_layout ? m_layout->itemIndex(*this) : -1;
}
inline std::size_t CommandItem::depth() const noexcept
{
    return m_layout ? m_layout->itemDepth(*this) : 0;
}
inline std::vector<std::size_t> CommandItem::path() const
{
    return m_layout ? m_layout->itemPath(*this) : std::vector<std::size_t>{};
}
inline CommandItem ICommandLayout::createItem(const void* ptr) const
{
    return CommandItem{this, ptr};
}
inline CommandItem ICommandLayout::createItem(std::uintptr_t id) const
{
    return CommandItem{this, id};
}

} // namespace bakuon::gui
