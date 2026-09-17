#include <gtest/gtest.h>

#include <string>

#include <bakuon/core/Entity.h>
#include <bakuon/core/Serializer.h>

using namespace bakuon::core;

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;
};
void to_json(nlohmann::json& j, const Position& p)
{
    j = {{"x", p.x}, {"y", p.y}};
}
void from_json(const nlohmann::json& j, Position& p)
{
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
}

struct Name
{
    std::string value;
};
void to_json(nlohmann::json& j, const Name& n)
{
    j = {{"value", n.value}};
}
void from_json(const nlohmann::json& j, Name& n)
{
    j.at("value").get_to(n.value);
}

struct Selected
{
};
// Selected 是空结构体标签组件：entt 的空类型优化（与 Registry::each() 文档里
// 提到的是同一机制）意味着序列化 Selected 时根本不会有"值"需要写入/读出——
// entt 只需要知道"这个实体携带了这个类型"，不需要调用 to_json()/from_json()。
// 下面两个 ADL 自由函数因此实际不会被调用（编译器会发出 unused-function 警告，
// 这里用 [[maybe_unused]] 显式承认这一点，而不是删掉它们——nlohmann::json 的
// 静态类型检查仍然要求这两个重载"存在"，只是运行期用不上）。
[[maybe_unused]] void to_json(nlohmann::json& j, const Selected&)
{
    j = nlohmann::json::object();
}
[[maybe_unused]] void from_json(const nlohmann::json&, Selected&)
{
}

} // namespace

BAKUON_DECLARE_COMPONENT_NAME(Position, "Position")
BAKUON_DECLARE_COMPONENT_NAME(Name, "Name")
BAKUON_DECLARE_COMPONENT_NAME(Selected, "Selected")

TEST(DocumentSerializerTest, SaveThenLoadIntoTheSameRegistryRoundTrips)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 1.5f, 2.5f);
    registry.emplace<Name>(node, Name{"hello"});

    DocumentSerializer<Position, Name> serializer(registry);
    const nlohmann::json doc = serializer.save();

    ASSERT_TRUE(serializer.load(doc).success());
    ASSERT_TRUE(registry.valid(node));
    EXPECT_EQ(registry.get<Position>(node).x, 1.5f);
    EXPECT_EQ(registry.get<Position>(node).y, 2.5f);
    EXPECT_EQ(registry.get<Name>(node).value, "hello");
}

TEST(DocumentSerializerTest, SaveThenLoadIntoADifferentRegistryRoundTrips)
{
    Registry source;
    const Entity node = source.create();
    source.emplace<Position>(node, 3.f, 4.f);
    source.emplace<Name>(node, Name{"world"});
    DocumentSerializer<Position, Name> saver(source);
    const nlohmann::json doc = saver.save();

    Registry destination;
    DocumentSerializer<Position, Name> loader(destination);
    ASSERT_TRUE(loader.load(doc).success());

    ASSERT_TRUE(destination.valid(node)) << "两个独立 Registry 之间的实体标识符应当一致地还原";
    EXPECT_EQ(destination.get<Position>(node).x, 3.f);
    EXPECT_EQ(destination.get<Name>(node).value, "world");
}

TEST(DocumentSerializerTest, TagComponentRoundTrips)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Selected>(node);

    DocumentSerializer<Selected> serializer(registry);
    const nlohmann::json doc = serializer.save();

    Registry other;
    DocumentSerializer<Selected> otherSerializer(other);
    ASSERT_TRUE(otherSerializer.load(doc).success());
    EXPECT_TRUE(other.all_of<Selected>(node));
}

TEST(DocumentSerializerTest, MultipleEntitiesPreserveTheirOwnData)
{
    Registry registry;
    const Entity a = registry.create();
    const Entity b = registry.create();
    registry.emplace<Position>(a, 1.f, 1.f);
    registry.emplace<Position>(b, 2.f, 2.f);

    DocumentSerializer<Position> serializer(registry);
    const nlohmann::json doc = serializer.save();

    registry.patch<Position>(a, [](Position& p) { p.x = 999.f; }); // 破坏当前状态
    ASSERT_TRUE(serializer.load(doc).success());

    EXPECT_EQ(registry.get<Position>(a).x, 1.f);
    EXPECT_EQ(registry.get<Position>(b).x, 2.f);
}

TEST(DocumentSerializerTest, LoadRejectsDocumentMissingRequiredFields)
{
    Registry registry;
    DocumentSerializer<Position> serializer(registry);
    nlohmann::json doc = serializer.save();
    doc.erase("components");

    const auto result = serializer.load(doc);
    EXPECT_TRUE(result.error());
    EXPECT_FALSE(result.status().message.empty());
}

TEST(DocumentSerializerTest, LoadRejectsUnsupportedVersion)
{
    Registry registry;
    DocumentSerializer<Position> serializer(registry);
    nlohmann::json doc = serializer.save();
    doc["version"]     = 999;

    const auto result = serializer.load(doc);
    EXPECT_TRUE(result.error());
}

TEST(DocumentSerializerTest, LoadRejectsDocumentMissingAComponentKey)
{
    Registry registry;
    DocumentSerializer<Position, Name> serializer(registry);
    nlohmann::json doc = serializer.save(); // 没有任何实体带 Name，但字段本身应该存在
    doc["components"].erase("Name");

    const auto result = serializer.load(doc);
    EXPECT_TRUE(result.error());
}

TEST(DocumentSerializerTest, EmptyRegistrySerializesAndLoadsCleanly)
{
    Registry registry;
    DocumentSerializer<Position, Name> serializer(registry);
    const nlohmann::json doc = serializer.save();

    ASSERT_TRUE(serializer.load(doc).success());
}
