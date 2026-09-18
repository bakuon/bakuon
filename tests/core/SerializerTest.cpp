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
    friend bool operator==(const Position& a, const Position& b) noexcept
    {
        return a.x == b.x && a.y == b.y;
    }
};

// 持有 std::string：通过 ADL archive_write/archive_read 接入，
struct Name
{
    std::string value;
};
void archive_write(IArchiveWriter& ar, const Name& n)
{
    ar.writeString(n.value);
}
void archive_read(IArchiveReader& ar, Name& n)
{
    n.value = ar.readString();
}

// 空结构体标签组件：同样可平凡拷贝，零代码。
struct Selected
{
};

} // namespace

TEST(SerializerTest, SaveThenLoadIntoTheSameRegistryRoundTrips)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Position>(node, 1.5f, 2.5f);
    registry.emplace<Name>(node, Name{"hello"});

    Serializer<Position, Name> serializer(registry);
    const std::vector<std::byte> bytes = serializer.save();

    ASSERT_TRUE(serializer.load(bytes).success());
    ASSERT_TRUE(registry.valid(node));
    EXPECT_EQ(registry.get<Position>(node).x, 1.5f);
    EXPECT_EQ(registry.get<Name>(node).value, "hello");
}

TEST(SerializerTest, SaveThenLoadIntoADifferentRegistryRoundTrips)
{
    Registry source;
    const Entity node = source.create();
    source.emplace<Position>(node, 3.f, 4.f);
    source.emplace<Name>(node, Name{"world"});
    const Serializer<Position, Name> saver(source);
    const std::vector<std::byte> bytes = saver.save();

    Registry destination;
    Serializer<Position, Name> loader(destination);
    ASSERT_TRUE(loader.load(bytes).success());

    ASSERT_TRUE(destination.valid(node)) << "两个独立 Registry 之间的实体标识符应当一致地还原";
    EXPECT_EQ(destination.get<Position>(node).x, 3.f);
    EXPECT_EQ(destination.get<Name>(node).value, "world");
}

TEST(SerializerTest, TagComponentRoundTrips)
{
    Registry registry;
    const Entity node = registry.create();
    registry.emplace<Selected>(node);

    Serializer<Selected> serializer(registry);
    const std::vector<std::byte> bytes = serializer.save();

    Registry other;
    Serializer<Selected> otherSerializer(other);
    ASSERT_TRUE(otherSerializer.load(bytes).success());
    EXPECT_TRUE(other.all_of<Selected>(node));
}

TEST(SerializerTest, MultipleEntitiesPreserveTheirOwnData)
{
    Registry registry;
    const Entity a = registry.create();
    const Entity b = registry.create();
    registry.emplace<Position>(a, 1.f, 1.f);
    registry.emplace<Position>(b, 2.f, 2.f);

    Serializer<Position> serializer(registry);
    const std::vector<std::byte> bytes = serializer.save();

    registry.patch<Position>(a, [](Position& p) { p.x = 999.f; }); // 破坏当前状态
    ASSERT_TRUE(serializer.load(bytes).success());

    EXPECT_EQ(registry.get<Position>(a).x, 1.f);
    EXPECT_EQ(registry.get<Position>(b).x, 2.f);
}

TEST(SerializerTest, LoadRejectsUnsupportedVersion)
{
    Registry registry;
    Serializer<Position> serializer(registry);
    ByteBufferWriter writer;
    writer.writeU32(0x53524B42); // kMagic
    writer.writeU32(999);        // 错误版本号

    const auto result = serializer.load(writer.buffer());
    EXPECT_TRUE(result.error());
}

TEST(SerializerTest, LoadRejectsComponentTypeCountMismatch)
{
    // 模拟"save() 时用了 <Position, Name>，load() 却只声明了 <Position>"这类
    // 两端模板参数不一致的场景——二进制流没有 JSON 那种按 key 容错的空间，
    // 数量不匹配必须被明确拒绝，而不是静默地把 Name 的字节错读成别的东西。
    Registry registry;
    registry.emplace<Position>(registry.create(), 1.f, 1.f);
    registry.emplace<Name>(registry.get<Position>(*registry.view<Position>().begin()) == Position{}
                               ? Entity{}
                               : *registry.view<Position>().begin(),
                           Name{"x"});

    const Serializer<Position, Name> saver(registry);
    const std::vector<std::byte> bytes = saver.save();

    Serializer<Position> underDeclaredLoader(registry);
    EXPECT_TRUE(underDeclaredLoader.load(bytes).error());
}

TEST(SerializerTest, TruncatedArchiveIsRejectedNotCrashed)
{
    Registry registry;
    registry.emplace<Position>(registry.create(), 1.f, 1.f);

    const Serializer<Position> serializer(registry);
    const std::vector<std::byte> full = serializer.save();
    const std::vector<std::byte> truncated(full.begin(), full.begin() + 4);

    Serializer<Position> loader(registry);
    EXPECT_TRUE(loader.load(truncated).error());
}

TEST(SerializerTest, EmptyRegistrySerializesAndLoadsCleanly)
{
    Registry registry;
    const Serializer<Position, Name> serializer(registry);
    const std::vector<std::byte> bytes = serializer.save();

    Serializer<Position, Name> loader(registry);
    ASSERT_TRUE(loader.load(bytes).success());
}
