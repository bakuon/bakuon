#include "core/b_archive.h"

#include <cstring>

namespace bakuon::core {

namespace {

template<typename T>
void appendPod(std::vector<std::byte>& buffer, T value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
}

} // namespace

// ---------------------------------------------------------------- Writer --

void ByteBufferWriter::writeBool(bool value)
{
    appendPod<std::uint8_t>(m_buffer, value ? 1 : 0);
}

void ByteBufferWriter::writeI32(std::int32_t value)
{
    appendPod(m_buffer, value);
}

void ByteBufferWriter::writeU32(std::uint32_t value)
{
    appendPod(m_buffer, value);
}

void ByteBufferWriter::writeI64(std::int64_t value)
{
    appendPod(m_buffer, value);
}

void ByteBufferWriter::writeU64(std::uint64_t value)
{
    appendPod(m_buffer, value);
}

void ByteBufferWriter::writeF32(float value)
{
    appendPod(m_buffer, value);
}

void ByteBufferWriter::writeF64(double value)
{
    appendPod(m_buffer, value);
}

void ByteBufferWriter::writeString(std::string_view value)
{
    writeU32(static_cast<std::uint32_t>(value.size()));
    const auto* bytes = reinterpret_cast<const std::byte*>(value.data());
    m_buffer.insert(m_buffer.end(), bytes, bytes + value.size());
}

void ByteBufferWriter::writeBytes(std::span<const std::byte> blob)
{
    writeU32(static_cast<std::uint32_t>(blob.size()));
    m_buffer.insert(m_buffer.end(), blob.begin(), blob.end());
}

void ByteBufferWriter::writeRaw(std::span<const std::byte> data)
{
    m_buffer.insert(m_buffer.end(), data.begin(), data.end());
}

// ---------------------------------------------------------------- Reader --

void ByteBufferReader::ensure(std::size_t n) const
{
    if (remaining() < n) {
        throw ArchiveError("ByteBufferReader:Reading beyond the boundary (archived data is "
                           "truncated or does not conform to the format)");
    }
}

template<typename T>
T ByteBufferReader::readPod()
{
    ensure(sizeof(T));
    T value{};
    std::memcpy(&value, m_bytes.data() + m_offset, sizeof(T));
    m_offset += sizeof(T);
    return value;
}

bool ByteBufferReader::readBool()
{
    return readPod<std::uint8_t>() != 0;
}

std::int32_t ByteBufferReader::readI32()
{
    return readPod<std::int32_t>();
}

std::uint32_t ByteBufferReader::readU32()
{
    return readPod<std::uint32_t>();
}

std::int64_t ByteBufferReader::readI64()
{
    return readPod<std::int64_t>();
}

std::uint64_t ByteBufferReader::readU64()
{
    return readPod<std::uint64_t>();
}

float ByteBufferReader::readF32()
{
    return readPod<float>();
}

double ByteBufferReader::readF64()
{
    return readPod<double>();
}

std::string ByteBufferReader::readString()
{
    const auto len = readU32();
    ensure(len);
    std::string out(reinterpret_cast<const char*>(m_bytes.data() + m_offset), len);
    m_offset += len;
    return out;
}

std::vector<std::byte> ByteBufferReader::readBytes()
{
    const auto len = readU32();
    ensure(len);
    std::vector<std::byte> out(m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset),
                               m_bytes.begin() + static_cast<std::ptrdiff_t>(m_offset + len));
    m_offset += len;
    return out;
}

void ByteBufferReader::readRaw(std::span<std::byte> out)
{
    ensure(out.size());
    std::memcpy(out.data(), m_bytes.data() + m_offset, out.size());
    m_offset += out.size();
}

} // namespace bakuon::core
