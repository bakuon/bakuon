#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "core/b_archivable.h"
#include "core/b_archive.h"
#include "core/b_entity.h"
#include "core/b_result.h"

namespace bakuon::core {

/// 补充：
/// * version<T> traits
/// * load() 时对每个 component 类型做 from_v1_to_v2 的迁移钩子链

namespace serializer_detail {
/**
 * @brief Serializer<Components...>（原 DocumentSerializer）内部使用的诊断信息
 * 生成函数——不是模板，纯字符串拼接逻辑，独立放进 .cpp，避免头文件膨胀。
 */
[[nodiscard]] std::string describeCountMismatch(std::size_t expected, std::uint32_t actual);
[[nodiscard]] std::string describeNameMismatch(std::string_view expected, std::string_view actual);
} // namespace serializer_detail

/**
 * @brief 编译期已知组件集合的整体序列化器。
 *
 * @tparam Components 参与序列化的组件类型（至少一个）。每个类型必须：
 *   - 可平凡拷贝（`std::is_trivially_copyable_v`），此时零代码接入；或
 *   - 提供一对 ADL 自由函数 `archive_write(IArchiveWriter&, const T&)` /
 *     `archive_read(IArchiveReader&, T&)`（见 b_archivable.h 的
 *     `ArchivableComponent` 概念）。
 *
 * ## 与 UndoStack 的分工
 * UndoStack（b_undostack.h）同样基于 entt::snapshot/snapshot_loader，但用的是
 * 逐字节内存拷贝的二进制归档器，只支持"可平凡拷贝"的组件、只用于进程内的
 * 撤销/重做历史，从不落盘。Serializer 换成 JSON 归档器：牺牲一些性能
 * 和体积，换来（a）人类可读、可跨进程/跨版本迁移的落盘格式；（b）支持
 * std::string 等非平凡可拷贝的字段（只要组件类型自己实现了 to_json/from_json）。
 * 两者都不重复发明"怎么遍历整个 Registry 的实体和组件"这件事——那正是
 * entt::snapshot 已经做好、经过 entt 自身测试覆盖的部分。
 *
 * 本类是"类型集合编译期固定"场景的薄封装——文档/场景全量存盘这类场景，
 * 参与序列化的类型通常在写代码时就已知，不需要运行期动态注册能力。
 *   - 想换序列化后端（二进制 → Protobuf/FlatBuffers）：只需要新写一对
 *     `IArchiveWriter`/`IArchiveReader` 实现，`Serializer`/组件代码都不用改；
 *   - 想让插件组件也参与（编译期不可知的类型集合）：改用 `ComponentArchive`。
 *
 * ## 二进制格式（与旧版 JSON 的关键差异）
 * JSON 版本把各组件放进一个 `{"Name": [...], "Tag": [...]}` 对象，按 key
 * 查找，`Components...` 的顺序与文件里的字段顺序无关。二进制流没有这种
 * 随机访问能力，`entt::snapshot_loader` 本身也要求同一会话内按固定顺序
 * `.get<T1>().get<T2>()...`——因此 `save()`/`load()` 两端**必须使用完全相同
 * 顺序的 `Components...` 列表**。每个类型的名字仍然会被写进流里，但只用于
 * 加载时逐位校验、给出清晰的不匹配诊断，不再承担"按名字查找"的职责。
 *
 * ## 已知限制（v1，刻意不做）
 * 不处理"文档里缺少某个 Components 类型"这类模式演进场景（比如老文档没有
 * 某个新增的组件字段）——load() 遇到缺失字段会失败并返回 Fail<void>，
 * 不会静默跳过。前向/后向兼容的 schema 演进留给未来按实际需要再设计。
 *
 * @code
 *   struct Position { float x, y; }; // 可平凡拷贝：零代码
 *   struct Name { std::string value; };
 *   void archive_write(IArchiveWriter& ar, const Name& n) { ar.writeString(n.value); }
 *   void archive_read(IArchiveReader& ar, Name& n) { n.value = ar.readString(); }
 *
 *   Serializer<Position, Name> serializer(registry);
 *   const std::vector<std::byte> bytes = serializer.save();
 *   // ... 落盘/跨进程传输 ...
 *   Serializer<Position, Name> loader(otherRegistry);
 *   if (auto result = loader.load(bytes); result.error()) { ... }
 * @endcode
 */
template<typename... Components>
class Serializer
{
    static_assert(sizeof...(Components) > 0, "Serializer 至少需要指定一个要序列化的组件类型");
    static_assert(((ArchivableComponent<Components> || std::is_trivially_copyable_v<Components>)
                   && ...),
                  "每个 Components 类型必须可平凡拷贝，或提供一对 ADL 自由函数 "
                  "archive_write(IArchiveWriter&, const T&) / archive_read(IArchiveReader&, T&)");

public:
    explicit Serializer(Registry& registry)
        : m_registry(registry)
    {
    }

    /// 便捷重载：使用默认二进制后端（ByteBufferWriter），直接返回完整字节序列。
    [[nodiscard]] std::vector<std::byte> save() const
    {
        ByteBufferWriter writer;
        save(writer);
        return writer.takeBuffer();
    }

    /**
     * @brief 序列化进调用方提供的任意 IArchiveWriter 实现。
     * @note 本类完全不知道 writer 背后是什么具体格式——这正是"格式与组件
     *       解耦"这条设计约束在写入路径上的体现。
     */
    void save(IArchiveWriter& writer) const
    {
        writer.writeU32(kMagic);
        writer.writeU32(kFormatVersion);

        ArchiveWritable entityAdapter(writer);
        const Snapshot snapshot{m_registry};
        snapshot.template get<Entity>(entityAdapter);

        writer.writeU32(static_cast<std::uint32_t>(sizeof...(Components)));
        (writeComponent<Components>(snapshot, writer), ...);
    }

    /// 便捷重载：从 save() 产出的一段连续内存整体恢复。
    [[nodiscard]] Result<void> load(std::span<const std::byte> bytes)
    {
        ByteBufferReader reader(bytes);
        return load(reader);
    }

    /**
     * @brief 从调用方提供的任意 IArchiveReader 整体恢复（覆盖 Registry 当前内容）。
     * @return 成功返回 Ok()；magic/version 不匹配、组件类型数量或顺序与
     *         Components... 声明不一致、或数据被截断，均返回 Fail<void>。
     *         失败时 Registry 已经被 clear() 过，调用方应把这种失败当作
     *         "整份数据不可用"处理，不要假设失败后 Registry 还是干净旧状态。
     */
    [[nodiscard]] Result<void> load(IArchiveReader& reader)
    {
        try {
            const auto magic = reader.readU32();
            if (magic != kMagic) {
                return Fail<void>(StatusCode::InvalidArgument, "归档 magic 不匹配，不是合法文件");
            }
            const auto version = reader.readU32();
            if (version != kFormatVersion) {
                return Fail<void>(StatusCode::InvalidArgument,
                                  "归档格式版本不受支持（期望 " + std::to_string(kFormatVersion)
                                      + "）");
            }

            m_registry.clear();

            ArchiveReadable entityAdapter(reader);
            SnapshotLoader loader{m_registry};
            loader.template get<Entity>(entityAdapter);

            const auto count = reader.readU32();
            if (count != sizeof...(Components)) {
                return Fail<void>(StatusCode::InvalidArgument,
                                  serializer_detail::describeCountMismatch(sizeof...(Components),
                                                                           count));
            }

            m_lastError.clear();
            // && 短路：一旦某个位置的类型名对不上就立即停止，不再从 reader
            // 继续读取——错位之后的字节已经不可信，读越多越可能变成越界访问。
            const bool ok = (readComponent<Components>(loader, reader) && ...);
            if (!ok) {
                return Fail<void>(StatusCode::InvalidArgument, m_lastError);
            }
            return Ok();
        } catch (const ArchiveError& e) {
            return Fail<void>(StatusCode::DataLoss, e.what());
        }
    }

    [[nodiscard]] static constexpr std::size_t count() noexcept { return sizeof...(Components); }

private:
    template<typename T>
    void writeComponent(const Snapshot& snapshot, IArchiveWriter& writer) const
    {
        writer.writeString(typeName<T>());
        ArchiveWritable adapter(writer);
        snapshot.template get<T>(adapter);
    }

    template<typename T>
    bool readComponent(SnapshotLoader& loader, IArchiveReader& reader)
    {
        const std::string storedName    = reader.readString();
        const std::string_view expected = typeName<T>();
        if (storedName != expected) {
            m_lastError = serializer_detail::describeNameMismatch(expected, storedName);
            return false;
        }
        ArchiveReadable adapter(reader);
        loader.template get<T>(adapter);
        return true;
    }

private:
    static constexpr std::uint32_t kMagic         = 0x53524B42; // 'BKRS' ('Bakuon Serializer')
    static constexpr std::uint32_t kFormatVersion = 1;

    Registry& m_registry;
    std::string m_lastError;
};

} // namespace bakuon::core
