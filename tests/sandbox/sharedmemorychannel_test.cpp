#include <gtest/gtest.h>

#include <QUuid>

#include <sandbox/b_sharedmemorychannel.h>

using namespace bakuon::sandbox;

namespace {
QString uniqueKey()
{
    return QStringLiteral("bakuon-test-shm-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}
} // namespace

TEST(SharedMemoryChannelTest, CreateThenAttachRoundTripsPayload)
{
    const QString key = uniqueKey();

    SharedMemoryChannel writer;
    const QByteArray input = QByteArrayLiteral("hello-shared-memory");
    ASSERT_FALSE(writer.create(key, input, /*extraCapacity=*/16).has_value());
    EXPECT_EQ(writer.capacity(), input.size() + 16);

    SharedMemoryChannel reader;
    ASSERT_FALSE(reader.attach(key).has_value());
    EXPECT_EQ(reader.readPayload(), input);
}

TEST(SharedMemoryChannelTest, WritePayloadIsVisibleAcrossAttachedInstances)
{
    const QString key = uniqueKey();

    SharedMemoryChannel a;
    ASSERT_FALSE(a.create(key, QByteArray(), /*extraCapacity=*/64).has_value());

    SharedMemoryChannel b;
    ASSERT_FALSE(b.attach(key).has_value());

    const QByteArray payload = QByteArrayLiteral("result-payload");
    ASSERT_FALSE(a.writePayload(payload).has_value());

    // b 是独立的 QSharedMemory 句柄，但指向同一块物理内存段——每次读取都重新加锁/取指针，
    // 因此 a 写完之后 b 应该能立刻读到，不需要重新 attach()。
    EXPECT_EQ(b.readPayload(), payload);
}

TEST(SharedMemoryChannelTest, WritePayloadRejectsOversizedInput)
{
    const QString key = uniqueKey();
    SharedMemoryChannel channel;
    ASSERT_FALSE(channel.create(key, QByteArray(), /*extraCapacity=*/4).has_value());

    const auto err = channel.writePayload(QByteArrayLiteral("way-too-long-for-4-bytes"));
    EXPECT_TRUE(err.has_value());
}

TEST(SharedMemoryChannelTest, AttachToNonExistentKeyFails)
{
    SharedMemoryChannel channel;
    const auto err = channel.attach(uniqueKey());
    EXPECT_TRUE(err.has_value());
    EXPECT_FALSE(channel.isAttached());
}

TEST(SharedMemoryChannelTest, StatusDefaultsToPendingAndIsSettable)
{
    const QString key = uniqueKey();
    SharedMemoryChannel channel;
    ASSERT_FALSE(channel.create(key, QByteArray(), 0).has_value());
    EXPECT_EQ(channel.status(), static_cast<quint32>(SharedMemoryChannel::StatusPending));

    channel.setStatus(SharedMemoryChannel::StatusOk);
    EXPECT_EQ(channel.status(), static_cast<quint32>(SharedMemoryChannel::StatusOk));
}

TEST(SharedMemoryChannelTest, ReleaseDetachesAndClearsKey)
{
    const QString key = uniqueKey();
    SharedMemoryChannel channel;
    ASSERT_FALSE(channel.create(key, QByteArray(), 8).has_value());
    ASSERT_TRUE(channel.isAttached());

    channel.release();
    EXPECT_FALSE(channel.isAttached());
    EXPECT_TRUE(channel.key().isEmpty());
}
