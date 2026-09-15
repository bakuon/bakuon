#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <entt/entity/mixin.hpp> // 必须包含才能使用：entt::snapshot::get() & entt::snapshot_loader::get
#include <entt/entity/snapshot.hpp>
#include <nlohmann/json.hpp>

#include "core/b_registry.h"
#include "core/b_serializer.h" // 复用其中的 serialize_detail::JsonWriter/JsonReader

namespace bakuon::core {

namespace undo_detail {

/**
 * @brief 最简单的"整体二进制"归档器：entt::snapshot/snapshot_loader 只要求
 * 归档对象提供 `operator()(T&)`（读）或 `operator()(const T&)`（写），本类
 * 原样按字节追加/读回，不做任何格式化或压缩。
 *
 * @warning 因此只支持"可平凡拷贝"（std::is_trivially_copyable_v）的值：
 * 逐字节 memcpy 对持有堆内存的类型（如 std::string）不安全——UndoStack
 * 在类模板层面对每个 Components... 都做了这个约束的 static_assert，
 * 这里的归档器本身不重复校验。
 */
struct ByteWriter
{
    std::vector<std::byte> buffer;

    template<typename T>
    void operator()(const T& value)
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
    }
};

struct ByteReader
{
    const std::vector<std::byte>& buffer;
    std::size_t offset = 0;

    template<typename T>
    void operator()(T& value)
    {
        std::memcpy(&value, buffer.data() + offset, sizeof(T));
        offset += sizeof(T);
    }
};

} // namespace undo_detail

/**
 * @brief 基于 entt::snapshot / entt::snapshot_loader 的整体式撤销/重做栈。
 *
 * @tparam Components 参与快照追踪的组件类型（至少一个）。每次 snapshot() 都会
 *         把 Registry 当前的完整实体集合，加上这些类型各自的全部实例，打包成
 *         一帧存进历史记录；undo()/redo() 整体切换到相邻的一帧。
 *
 * ## 为什么是"整体快照"而不是逐操作记录 diff
 * 经典的撤销/重做实现有两条路线：(a) 记录每一步操作本身（Command 模式，
 * 需要每种操作都实现对应的 execute()/undo()）；(b) 定期给整个状态打一个完整
 * 快照，undo/redo 直接切换到相邻快照（本类采用的路线）。选择 (b) 的原因：
 * EnTT 已经提供了成熟、通过 entt 自身测试覆盖的 snapshot/snapshot_loader
 * 机制，直接复用比每种业务操作各自实现一遍 undo 逻辑要省心得多，尤其是在
 * "core 不知道任何具体业务组件是什么"这个前提下——UndoStack 完全不需要认识
 * Position/Selected/Name 之类的具体类型，只需要调用方在实例化时把这些类型
 * 列进模板参数。
 *
 * ## 归档器：复用 DocumentSerializer 的 JSON 归档器（不再是逐字节内存拷贝）
 * 早期版本自己实现了一个最简单的"整体二进制"归档器（逐字节 memcpy），因此
 * 要求 Components... 全部是 std::is_trivially_copyable_v 的——像 std::string
 * 这类持有堆内存的字段完全用不了，只能建议调用方拆到单独的、不参与 undo
 * 追踪的组件里。现在 DocumentSerializer（b_serializer.h）已经落地、并且用
 * entt::snapshot/snapshot_loader 实测验证过一套支持变长数据的 JSON 归档器
 * （serialize_detail::JsonWriter/JsonReader），本类直接复用同一套实现，
 * 不再自己维护第二份归档逻辑：
 *  - 好处：Components... 不再需要可平凡拷贝，std::string/std::vector 等
 *    变长字段可以直接参与撤销追踪；两处 entt::snapshot 归档逻辑合一，少一份
 *    需要独立维护、独立验证内存安全的代码。
 *  - 代价：JSON 文本化/解析比逐字节 memcpy 慢、体积也更大——对"每次逻辑操作
 *    调用一次 snapshot()"这种触发频率（而不是每次鼠标移动都触发）而言，这个
 *    开销是可以接受的；如果未来出现历史帧数量巨大、对撤销延迟极度敏感的场景，
 *    再回头针对"逐字节可平凡拷贝"的情形单独做一条快路径也不迟，现在不提前
 *    为了尚不存在的性能问题引入两套归档器的维护负担。
 *  - Components... 现在的约束与 DocumentSerializer 一致：具备 nlohmann::json
 *    认识的 to_json()/from_json()（ADL 自由函数）。不需要（也不要求）像
 *    DocumentSerializer 那样额外用 BAKUON_DECLARE_COMPONENT_NAME 声明字符串键
 *    ——本类的历史帧纯粹是进程内瞬时状态，按 Components... 包里的位置顺序存取
 *    即可，没有 DocumentSerializer 那种"要在磁盘上长期保持稳定、可读"的诉求。
 *
 * ## 关键的正确性依据（务必先读）
 * 本类的实现依赖以下经过实测验证（而不是想当然）的 entt 行为：
 *  1. `entt::registry::clear()` 会对已存在的每一个组件正常触发 on_destroy
 *     信号，之后 `entt::snapshot_loader::get<T>()` 重新灌入数据时会正常
 *     触发 on_construct 信号——也就是说，restore() 前后，Registry 的现有
 *     Connection（onConstruct/onUpdate/onDestroy 订阅）**完全不受影响**，
 *     不需要重新订阅。这是本类能够安全地反复 clear()+reload 而不破坏 GUI
 *     侧已经建立的观察者的前提。
 *  2. `entt::snapshot_loader` 官方文档要求"目标 registry 必须是空的"，但
 *     实测（在一个已经创建/销毁过多个实体、随后调用过 clear() 的 registry
 *     上）构造 snapshot_loader 并不会触发这个断言失败——clear() 之后
 *     底层实体存储的"回收计数"确实会归零，满足 snapshot_loader 的前提。
 *  3. 快照/回放全程使用同一个 Registry 实例（不会另起一个新的
 *     entt::registry 再整体替换），因此实体标识符（Handle 的底层数值）
 *     在 undo()/redo() 前后是稳定的、可预测的——不需要额外的"持久化 id"
 *     组件来跨越 undo 维持引用（多数其它 ECS 的整体快照方案不能保证这一点，
 *     这里特别记录下来，因为最初设计时曾经担心过这个问题，被实测结果推翻）。
 *
 * ## 使用方式
 * @code
 *   struct Position { float x, y; };
 *   void to_json(nlohmann::json& j, const Position& p) { j = {{"x", p.x}, {"y", p.y}}; }
 *   void from_json(const nlohmann::json& j, Position& p) { j.at("x").get_to(p.x); j.at("y").get_to(p.y); }
 *
 *   bakuon::core::Registry registry;
 *   bakuon::core::UndoStack<Position, Selected> undo(registry);
 *
 *   registry.emplace<Position>(node, 1.f, 2.f);
 *   undo.snapshot(); // 记录"添加了 Position"这一步之后的状态
 *
 *   registry.patch<Position>(node, [](Position& p) { p.x = 99.f; });
 *   undo.snapshot(); // 记录"移动"之后的状态
 *
 *   undo.undo(); // 回到"移动"之前
 *   undo.undo(); // 回到"添加 Position"之前（也就是构造时的初始状态）
 *   undo.redo(); // 重新回到"添加了 Position"之后
 * @endcode
 *
 * @warning Components... 必须具备 nlohmann::json 认识的 to_json()/from_json()
 * （见上方"归档器"一节）；构造 UndoStack 时会立即打一次初始快照
 * （historyDepth() 从 1 开始，而不是 0）。
 */
template<typename... Components>
class UndoStack
{
    static_assert(sizeof...(Components) > 0, "UndoStack 至少需要指定一个要追踪的组件类型");

public:
    /**
     * @param registry 被追踪的 Registry；必须比 UndoStack 活得更久（本类只持有引用）。
     * @param capacity 最多保留多少帧历史（含初始帧），超出时丢弃最旧的一帧；
     *                 传 0 会被当作 1 处理（至少要能容纳当前这一帧）。
     */
    explicit UndoStack(Registry& registry, std::size_t capacity = 64)
        : m_registry(registry)
        , m_capacity(capacity == 0 ? 1 : capacity)
    {
        pushFrame();
    }

    UndoStack(const UndoStack&)            = delete;
    UndoStack& operator=(const UndoStack&) = delete;
    // 不可移动：原因与 Registry 一致——本类只持有 Registry& 引用，移动本身没有
    // 陷阱，但为了和 Registry 的既有约束保持一致的心智模型（"这类跟状态强绑定
    // 的对象不支持移动"），这里同样禁止，避免调用方对"移动后旧对象还能不能用"
    // 产生疑问。
    UndoStack(UndoStack&&)                 = delete;
    UndoStack& operator=(UndoStack&&)      = delete;

    /**
     * @brief 把 Registry 当前的完整状态记录为新的一帧。
     * @note 如果当前位置不在历史末尾（即之前调用过 undo()、还没有再次 snapshot()），
     *       会先丢弃"未来"的那部分历史——这是撤销栈的标准行为：一旦在旧状态上
     *       产生了新的改动，原来 redo() 能到达的那些"未来"分支就不再有意义。
     */
    void snapshot()
    {
        if (m_cursor + 1 < m_history.size()) {
            m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_cursor) + 1,
                            m_history.end());
        }
        pushFrame();
        if (m_history.size() > m_capacity) {
            // 容量已满：丢弃最旧的一帧，游标不需要移动（新的一帧仍然是"当前"）。
            m_history.erase(m_history.begin());
        } else {
            ++m_cursor;
        }
    }

    [[nodiscard]] bool canUndo() const noexcept { return m_cursor > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return m_cursor + 1 < m_history.size(); }

    /// @return false 表示已经在最早一帧、没有可撤销的历史；此时是安全的空操作。
    bool undo()
    {
        if (!canUndo()) {
            return false;
        }
        --m_cursor;
        restoreFrame(m_history[m_cursor]);
        return true;
    }

    /// @return false 表示已经在最新一帧、没有可重做的历史；此时是安全的空操作。
    bool redo()
    {
        if (!canRedo()) {
            return false;
        }
        ++m_cursor;
        restoreFrame(m_history[m_cursor]);
        return true;
    }

    /// 清空全部历史，只保留"当前状态"作为唯一的一帧（historyDepth() 回到 1）。
    void clearHistory()
    {
        m_history.clear();
        m_cursor = 0;
        pushFrame();
    }

    /// 当前保存了多少帧历史（构造时的初始帧算第一帧，因此永远 >= 1）。
    [[nodiscard]] std::size_t historyDepth() const noexcept { return m_history.size(); }

private:
    struct Frame
    {
        nlohmann::json entities;
        std::array<nlohmann::json, sizeof...(Components)> components;
    };

    void pushFrame()
    {
        Frame frame;

        serialize_detail::JsonWriter entityWriter;
        entt::snapshot{m_registry.native()}.template get<entt::entity>(entityWriter);
        frame.entities = std::move(entityWriter.array);

        std::size_t index = 0;
        ((frame.components[index++] = writeComponent<Components>()), ...);

        m_history.push_back(std::move(frame));
    }

    void restoreFrame(const Frame& frame)
    {
        // 见类文档"关键的正确性依据"：clear() 之后在同一个 Registry 实例上
        // 原地 reload，既不破坏已有的 Connection，也不需要重建它们。
        m_registry.native().clear();

        entt::snapshot_loader loader{m_registry.native()};
        serialize_detail::JsonReader entityReader{frame.entities};
        loader.template get<entt::entity>(entityReader);

        std::size_t index = 0;
        (restoreComponent<Components>(loader, frame.components[index++]), ...);
    }

    template<typename T>
    [[nodiscard]] nlohmann::json writeComponent()
    {
        serialize_detail::JsonWriter writer;
        entt::snapshot{m_registry.native()}.template get<T>(writer);
        return std::move(writer.array);
    }

    template<typename T>
    void restoreComponent(entt::snapshot_loader& loader, const nlohmann::json& array)
    {
        serialize_detail::JsonReader reader{array};
        loader.template get<T>(reader);
    }

private:
    Registry& m_registry;
    std::size_t m_capacity;
    std::vector<Frame> m_history;
    std::size_t m_cursor = 0;
};

} // namespace bakuon::core
