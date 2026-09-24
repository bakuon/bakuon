#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <bakuon/core/command/CommandLayout.h>

using namespace bakuon::core;
using namespace bakuon::core::command;

TEST(CommandLayoutTest, FreshLayoutHasOnlyAnEmptyRoot)
{
    CommandLayout layout;
    EXPECT_TRUE(layout.registry().valid(layout.root()));
    EXPECT_EQ(layout.type(layout.root()), LayoutItemType::Container);
    EXPECT_TRUE(layout.isEmpty());
    EXPECT_EQ(layout.count(), 0u);
}

TEST(CommandLayoutTest, BuildFileEditMenuTree)
{
    CommandLayout layout;

    const Entity fileMenu = layout.addContainer("文件(&F)");
    layout.addCommand("file.save", fileMenu);

    const Entity editMenu = layout.addContainer("编辑(&E)", nullentity, /*index=*/1);
    layout.addCommand("edit.delete", editMenu, 0);
    layout.addSeparator(editMenu, 1);
    layout.addCommand("edit.duplicate", editMenu, 2);

    EXPECT_EQ(layout.count(), 2u);
    ASSERT_TRUE(layout.registry().valid(layout.itemAt(0)));
    EXPECT_EQ(layout.title(layout.itemAt(0)), "文件(&F)");
    EXPECT_EQ(layout.title(layout.itemAt(1)), "编辑(&E)");
    EXPECT_EQ(layout.count(editMenu), 3u);

    const Entity sep = layout.itemAt(1, editMenu);
    EXPECT_EQ(layout.type(sep), LayoutItemType::Separator);
    EXPECT_EQ(layout.commandId(layout.itemAt(0, editMenu)), "edit.delete");
    EXPECT_EQ(layout.commandId(layout.itemAt(2, editMenu)), "edit.duplicate");
}

TEST(CommandLayoutTest, RootRejectsSeparatorAndSection)
{
    CommandLayout layout;
    EXPECT_TRUE(layout.addSeparator(layout.root()) == nullentity);
    EXPECT_TRUE(layout.addSeparator(nullentity) == nullentity); // parent 缺省即根
    EXPECT_TRUE(layout.addSection("标题", layout.root()) == nullentity);
    EXPECT_TRUE(layout.isEmpty());
}

TEST(CommandLayoutTest, PathRoundTripsThroughItemFromPath)
{
    CommandLayout layout;
    const Entity menu = layout.addContainer("编辑");
    const Entity cmd  = layout.addCommand("edit.delete", menu);

    const auto p = layout.path(cmd);
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(layout.itemFromPath(p), cmd);
    EXPECT_TRUE(layout.isValidPath(p));
    using LIST = std::initializer_list<std::size_t>;
    EXPECT_FALSE(layout.isValidPath(LIST{99, 99}));
}

TEST(CommandLayoutTest, MoveRelocatesSubtree)
{
    CommandLayout layout;
    const Entity fileMenu = layout.addContainer("文件");
    const Entity editMenu = layout.addContainer("编辑");
    const Entity cmd      = layout.addCommand("edit.delete", editMenu);

    ASSERT_TRUE(layout.move(cmd, fileMenu, 0));
    EXPECT_EQ(layout.count(editMenu), 0u);
    EXPECT_EQ(layout.count(fileMenu), 1u);
    EXPECT_EQ(layout.parentOf(cmd), fileMenu);
}

TEST(CommandLayoutTest, MoveRejectsCycles)
{
    CommandLayout layout;
    const Entity menu = layout.addContainer("菜单");
    EXPECT_FALSE(layout.move(menu, menu, -1)) << "不能把节点移动到自己下面";
}

TEST(CommandLayoutTest, RemoveDropsSubtree)
{
    CommandLayout layout;
    const Entity menu = layout.addContainer("编辑");
    const Entity cmd  = layout.addCommand("edit.delete", menu);

    ASSERT_TRUE(layout.remove(menu));
    EXPECT_FALSE(layout.registry().valid(menu));
    EXPECT_FALSE(layout.registry().valid(cmd)) << "移除容器应该级联销毁其子树";
    EXPECT_TRUE(layout.isEmpty());

    EXPECT_FALSE(layout.remove(layout.root())) << "根节点不可移除";
}

TEST(CommandLayoutTest, ExportImportRoundTripsIntoTheSameInstance)
{
    CommandLayout layout;
    const Entity editMenu = layout.addContainer("编辑(&E)");
    layout.addCommand("edit.delete", editMenu, 0);
    layout.addSeparator(editMenu, 1);
    layout.addCommand("edit.duplicate", editMenu, 2);

    const std::vector<std::byte> bytes = layout.exportBytes();
    ASSERT_FALSE(bytes.empty());

    ASSERT_TRUE(layout.importBytes(bytes).success());
    ASSERT_EQ(layout.count(), 1u);
    EXPECT_EQ(layout.title(layout.itemAt(0)), "编辑(&E)");
    EXPECT_EQ(layout.count(layout.itemAt(0)), 3u);
}

TEST(CommandLayoutTest, ExportImportRoundTripsAcrossIndependentInstances)
{
    // 这是本类最关键的正确性保证：目标实例在导入前已经自己创建过一个根实体
    // （构造函数干的事），其 entt 原始数值与来源实例的根实体没有任何关系——
    // importBytes() 必须靠 LayoutRoot 标签重新定位，而不是假设数值碰巧相同。
    CommandLayout source;
    const Entity menu = source.addContainer("文件(&F)");
    source.addCommand("file.save", menu);
    const std::vector<std::byte> bytes = source.exportBytes();

    CommandLayout destination; // 已经有自己的根实体
    ASSERT_TRUE(destination.importBytes(bytes).success());

    ASSERT_TRUE(destination.registry().valid(destination.root()));
    EXPECT_EQ(destination.type(destination.root()), LayoutItemType::Container);
    ASSERT_EQ(destination.count(), 1u);
    EXPECT_EQ(destination.title(destination.itemAt(0)), "文件(&F)");
    EXPECT_EQ(destination.commandId(destination.itemAt(0, destination.itemAt(0))), "file.save");
}

TEST(CommandLayoutTest, ImportRejectsGarbageBytes)
{
    CommandLayout layout;
    const std::vector<std::byte> garbage{std::byte{0}, std::byte{1}, std::byte{2}};
    EXPECT_TRUE(layout.importBytes(garbage).error());
}

// ----------------------------------------------------------------------------
// "存储过程"的真实参照实现（Qt-free）：把 exportBytes() 的字节流原样写到磁盘，
// 再用 importBytes() 读回来验证内容一致。这不是伪代码——它是一段确实编译、
// 确实运行、确实通过 CI 的落盘往返，证明 CommandLayout 的持久化契约本身
// 不需要 Qt 就能完整跑通。gui 层要做的只是把 std::ofstream/std::ifstream
// 换成 QSaveFile/QFile（参照 examples/core/command_layout_persistence_reference.cpp），
// 调用 exportBytes()/importBytes() 这两个入口完全不需要改。
// ----------------------------------------------------------------------------
TEST(CommandLayoutTest, PersistenceReference_RoundTripsThroughARealFileOnDisk)
{
    CommandLayout original;
    const Entity fileMenu = original.addContainer("文件(&F)");
    original.addCommand("file.save", fileMenu, 0);
    original.addSeparator(fileMenu, 1);
    original.addCommand("file.quit", fileMenu, 2);

    const std::filesystem::path filePath = std::filesystem::temp_directory_path()
                                           / "bakuon_command_layout_reference.bin";

    // ---- 写：exportBytes() -> std::ofstream（二进制模式，原样落盘） ----
    {
        const std::vector<std::byte> bytes = original.exportBytes();
        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open()) << "无法打开 " << filePath;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good());
    }

    // ---- 读：std::ifstream -> importBytes()（另一个独立实例，模拟"下次启动"） ----
    CommandLayout restored;
    {
        std::ifstream in(filePath, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(in.is_open()) << "无法读取 " << filePath;
        const auto size = static_cast<std::size_t>(in.tellg());
        in.seekg(0);
        std::vector<std::byte> bytes(size);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        ASSERT_TRUE(in.good() || in.eof());

        const auto result = restored.importBytes(bytes);
        ASSERT_TRUE(result.success()) << result.status().message;
    }

    ASSERT_EQ(restored.count(), 1u);
    const Entity restoredMenu = restored.itemAt(0);
    EXPECT_EQ(restored.title(restoredMenu), "文件(&F)");
    ASSERT_EQ(restored.count(restoredMenu), 3u);
    EXPECT_EQ(restored.commandId(restored.itemAt(0, restoredMenu)), "file.save");
    EXPECT_EQ(restored.type(restored.itemAt(1, restoredMenu)), LayoutItemType::Separator);
    EXPECT_EQ(restored.commandId(restored.itemAt(2, restoredMenu)), "file.quit");

    std::error_code ec;
    std::filesystem::remove(filePath, ec); // 清理临时文件，失败不影响测试结果
}
