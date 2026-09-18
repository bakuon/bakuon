#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

#include "core/b_archive.h"
#include "core/b_entity.h"

namespace bakuon::core {

/**
 * @brief 组件是否实现了自定义归档编解码（通过 ADL 找到的自由函数）。
 *
 * @details 组件作者（通常是插件）在组件类型所在的命名空间里定义：
 * @code
 *   void archive_write(bakuon::core::IArchiveWriter& ar, const MyComponent& v);
 *   void archive_read(bakuon::core::IArchiveReader& ar, MyComponent& v);
 * @endcode
 * 这两个函数只依赖 IArchiveWriter/IArchiveReader 的原语，不依赖任何具体格式，
 * 因此同一份组件代码在二进制/JSON/未来任何后端下都无需改动。
 *
 * 不满足本概念、但满足 std::is_trivially_copyable_v 的组件完全不需要写任何
 * 代码 ——E ArchiveWritable/ArchiveReadable 会自动退化为整体字节拷贝。
 */
template<typename T>
concept ArchivableComponent = requires(IArchiveWriter& w, IArchiveReader& r, T& mutableValue,
                                       const T& value) {
    { archive_write(w, value) } -> std::same_as<void>;
    { archive_read(r, mutableValue) } -> std::same_as<void>;
};

/**
 * @brief entt::snapshot 期望的 Archive 概念的适配器（写方向）：把 entt 对
 * operator()(const T&) 的调用转发到 IArchiveWriter 的格式无关原语。
 *
 * @details entt::basic_snapshot::get<T>() 对同一个 archive 对象会依次用不同的
 * T 调用 operator()——先是池大小（某个整型），再是交替的 (Entity, T) 对
 * （空结构体标签组件的值不会被调用，entt 内部已经跳过，见 b_serializer.h /
 * SerializerTest.cpp 里对同一现象的说明，这里复用同一前提）。分发顺序：
 *   1) T == Entity                → 写实体的底层整数值
 *   2) T 是整型（覆盖池大小前缀）    → writeU64（宽度统一，收发两端自洽即可）
 *   3) 组件自带 archive_write/read → 转发给它（推荐路径，任意复杂度组件）
 *   4) 其余（可平凡拷贝）           → 整体字节拷贝（零代码路径）
 * 不满足以上任何一条的类型会在编译期直接 static_assert 失败，提示组件作者
 * 需要补一对 archive_write/archive_read。
 */
class ArchiveWritable
{
public:
    explicit ArchiveWritable(IArchiveWriter& writer) noexcept
        : m_writer(writer)
    {
    }

    template<typename T>
    void operator()(const T& value)
    {
        if constexpr (std::is_same_v<T, Entity>) {
            m_writer.writeU32(static_cast<std::uint32_t>(entt::to_integral(value)));
        } else if constexpr (std::is_integral_v<T>) {
            m_writer.writeU64(static_cast<std::uint64_t>(value));
        } else if constexpr (ArchivableComponent<T>) {
            archive_write(m_writer, value);
        } else {
            static_assert(std::is_trivially_copyable_v<T>,
                          "组件必须是可平凡拷贝类型，或者提供一对 ADL 自由函数 "
                          "archive_write(IArchiveWriter&, const T&) / "
                          "archive_read(IArchiveReader&, T&)");
            m_writer.writeRaw(std::as_bytes(std::span(std::addressof(value), 1)));
        }
    }

private:
    IArchiveWriter& m_writer;
};

/// 与 ArchiveWritable 对称的读方向适配器，喂给 entt::snapshot_loader。
class ArchiveReadable
{
public:
    explicit ArchiveReadable(IArchiveReader& reader) noexcept
        : m_reader(reader)
    {
    }

    template<typename T>
    void operator()(T& value)
    {
        if constexpr (std::is_same_v<T, Entity>) {
            using Underlying = std::underlying_type_t<Entity>;
            value            = static_cast<Entity>(static_cast<Underlying>(m_reader.readU32()));
        } else if constexpr (std::is_integral_v<T>) {
            value = static_cast<T>(m_reader.readU64());
        } else if constexpr (ArchivableComponent<T>) {
            archive_read(m_reader, value);
        } else {
            static_assert(std::is_trivially_copyable_v<T>,
                          "组件必须是可平凡拷贝类型，或者提供一对 ADL 自由函数 "
                          "archive_write(IArchiveWriter&, const T&) / "
                          "archive_read(IArchiveReader&, T&)");
            m_reader.readRaw(std::as_writable_bytes(std::span(std::addressof(value), 1)));
        }
    }

private:
    IArchiveReader& m_reader;
};

} // namespace bakuon::core
