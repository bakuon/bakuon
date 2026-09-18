#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bakuon::core {

/**
 * @brief 归档格式解析/写入异常：越界读取、magic/version 不匹配等。
 */
class ArchiveError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief 格式无关的写入原语集合。
 *
 * @details 这是整个组件归档框架里唯一允许被具体序列化格式（二进制/JSON/
 * Protobuf/FlatBuffers……）实现的抽象边界。组件作者（见 b_componentarchive.h
 * 的 archive_write/archive_read ADL 约定）只依赖这组原语，不依赖任何具体格式，
 * 因此替换后端格式不需要改任何组件代码，也不需要改 bakuon::core 本身 ————
 * ——只需要新写一对 IArchiveWriter/IArchiveReader 实现。
 *
 * writeBytes()/readBytes() 是变长、length-prefixed 的"任意二进制块"，供组件
 * 需要内嵌变长原始数据（如缩略图）时使用；writeRaw()/readRaw() 是定长、
 * 不带长度前缀的原语，专供 WriteAdapter/ReadAdapter 对"可平凡拷贝组件"
 * 做零代码的整体字节拷贝（调用方两端都已知道固定长度 sizeof(T)，不需要，
 * 也不应该重复写一次长度）。
 */
class IArchiveWriter
{
public:
    virtual ~IArchiveWriter() = default;

    virtual void writeBool(bool value)               = 0;
    virtual void writeI32(std::int32_t value)        = 0;
    virtual void writeU32(std::uint32_t value)       = 0;
    virtual void writeI64(std::int64_t value)        = 0;
    virtual void writeU64(std::uint64_t value)       = 0;
    virtual void writeF32(float value)               = 0;
    virtual void writeF64(double value)              = 0;
    virtual void writeString(std::string_view value) = 0;

    /// 变长二进制块，实现应自行加长度前缀。
    virtual void writeBytes(std::span<const std::byte> blob) = 0;
    /// 定长二进制块，不加长度前缀——调用方（读端）必须用同样的固定长度来读。
    virtual void writeRaw(std::span<const std::byte> data)   = 0;
};

/// 与 IArchiveWriter 严格对称的读取原语集合。
class IArchiveReader
{
public:
    virtual ~IArchiveReader() = default;

    virtual bool readBool()                        = 0;
    virtual std::int32_t readI32()                 = 0;
    virtual std::uint32_t readU32()                = 0;
    virtual std::int64_t readI64()                 = 0;
    virtual std::uint64_t readU64()                = 0;
    virtual float readF32()                        = 0;
    virtual double readF64()                       = 0;
    virtual std::string readString()               = 0;
    virtual std::vector<std::byte> readBytes()     = 0;
    /// 读取恰好 out.size() 字节，原地填入 out；越界读取抛 ArchiveError。
    virtual void readRaw(std::span<std::byte> out) = 0;
};

/**
 * @brief 默认的二进制具体实现：把归档内容原样追加到一段连续内存里。
 * @note 这是"能编译、能跑通完整链路"的默认后端，不是唯一后端——未来的
 * Protobuf/FlatBuffers 后端只需要各自实现 IArchiveWriter/IArchiveReader，
 * ComponentArchiveRegistry 和所有组件代码都不需要改一行。
 * @warning 按本机字节序、本机基础类型宽度原样写入，不做跨平台/跨字节序
 * 归一化——与 SharedMemoryChannel 等仓库里其它"同进程/同机型假设"的二进制
 * 协议一致的取舍。跨机型场景需要另一个做了字节序归一化的后端。
 */
class ByteBufferWriter final : public IArchiveWriter
{
public:
    ByteBufferWriter()           = default;
    ~ByteBufferWriter() override = default;

    void writeBool(bool value) override;
    void writeI32(std::int32_t value) override;
    void writeU32(std::uint32_t value) override;
    void writeI64(std::int64_t value) override;
    void writeU64(std::uint64_t value) override;
    void writeF32(float value) override;
    void writeF64(double value) override;
    void writeString(std::string_view value) override;
    void writeBytes(std::span<const std::byte> blob) override;
    void writeRaw(std::span<const std::byte> data) override;

    [[nodiscard]] const std::vector<std::byte>& buffer() const noexcept { return m_buffer; }
    [[nodiscard]] std::vector<std::byte> takeBuffer() noexcept { return std::move(m_buffer); }

private:
    std::vector<std::byte> m_buffer;
};

/// 配套的读取端：包一个只读 span，游标向前推进；越界访问抛 ArchiveError。
class ByteBufferReader final : public IArchiveReader
{
public:
    explicit ByteBufferReader(std::span<const std::byte> bytes) noexcept
        : m_bytes(bytes)
    {
    }
    ~ByteBufferReader() override = default;

    bool readBool() override;
    std::int32_t readI32() override;
    std::uint32_t readU32() override;
    std::int64_t readI64() override;
    std::uint64_t readU64() override;
    float readF32() override;
    double readF64() override;
    std::string readString() override;
    std::vector<std::byte> readBytes() override;
    void readRaw(std::span<std::byte> out) override;

    [[nodiscard]] std::size_t remaining() const noexcept { return m_bytes.size() - m_offset; }
    [[nodiscard]] std::size_t offset() const noexcept { return m_offset; }

private:
    void ensure(std::size_t n) const;
    template<typename T>
    T readPod();

    std::span<const std::byte> m_bytes;
    std::size_t m_offset = 0;
};

} // namespace bakuon::core
