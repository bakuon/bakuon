
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include "core/b_entity.h"
#include "core/b_stableid.h"

namespace bakuon::core {

/// 当 StableId 不变性被违反时（如冲突、无效绑定、溢出）抛出。
/// Thrown when a StableId invariant is violated (collision, invalid bind, overflow).
class StableIdError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// 生成器与解码器共享的雪花位布局。
/// Snowflake bit layout shared by the generator and decoder.
///
/// 我们将标准的 64 位雪花算法进行如下变体分配：
// - 1 bit: 固定为 0（保证 ID 为正数）。
// - 31 bit: 时间戳（秒级或毫秒级，31位秒级可支撑约 68 年，毫秒级需视生命周期调整。
//           由于工业软件通常单次会话运行，建议采用秒级时间戳 + 16位进程内自增序列，或毫秒级）。
// - 16 bit: 进程/沙箱隔离 ID (Process/Worker ID)。在沙箱进程启动时由宿主分配
//           （可支持 2¹⁶ = 65536 个并发子进程），从根本上杜绝多进程 ID 冲突。
// - 16 bit: 进程内自增序列（Sequence Number）。支持单进程每秒/每毫秒并发创建 65536 个实体。
/// ```
///  63       62 ................ 32  31 ........ 16  15 ......... 0
///  rsv      timestamp (31 bits)     worker (16)     sequence (16)
/// ```
namespace snowflake {
inline constexpr unsigned kSequenceBits         = 16;
inline constexpr unsigned kWorkerBits           = 16;
inline constexpr unsigned kTimestampBits        = 31;
inline constexpr unsigned kTimestampShift       = kSequenceBits + kWorkerBits; // 32
inline constexpr std::int64_t kEpochUnixSeconds = 1'767'225'600'000ULL; // 2026-01-01T00:00:00Z
inline constexpr std::uint64_t kSequenceMask    = (std::uint64_t{1} << kSequenceBits) - 1u;
inline constexpr std::uint64_t kWorkerMask      = (std::uint64_t{1} << kWorkerBits) - 1u;
inline constexpr std::uint64_t kTimestampMask   = (std::uint64_t{1} << kTimestampBits) - 1u;
} // namespace snowflake

struct SnowflakeView
{
    std::uint64_t timestamp{0}; // 毫秒 millisecond
    std::uint16_t worker{0};
    std::uint16_t sequence{0};
};

[[nodiscard]] SnowflakeView toSnowflake(StableId id) noexcept;
[[nodiscard]] std::string toHex(StableId id);
[[nodiscard]] std::string toString(StableId id);

class StableIdGenerator
{
public:
    virtual ~StableIdGenerator() = default;

    virtual std::uint64_t next() noexcept = 0;
};

/**
* @brief 无锁的64位雪花式稳定ID生成器(Snowflake Generator)。
*
* 位布局（从高位到低位），共64位：
*   [63]        符号位，始终为0（确保该值仍可表示为有符号64位整数——适
*               用于JSON/QtRO路径，这些路径通过qint64而非quint64进行往返转换）
*   [62:32]     自基准时间（2026-01-01T00:00:00Z，Unix时间1767225600）起经过
*               的31位秒数（硬编码，而非通过<chrono>日历类型计算，以避免依赖于
*               我们三个CI工具链之间仍不一致的std::chrono::year_month_day支持，
*               参见.github/workflows/ci.yml）
**  [31:16]     16位工作者/进程ID，由主机分配（类似于SandboxSystem::nextSandboxId()
*               将applicationPid()混入沙箱ID生成中，参见源码/sandbox/b_sandboxsystem.cpp）
*   [15:0]      16位单调序列计数器，每墙钟秒重置一次
*
* 31位秒数可覆盖从起始时间点（直至约2094年）的约68年。这是一个“每个创建对象唯一一次”的ID，
* 而非高频时间戳，因此秒级分辨率加上每秒65536个序列的预算，对于任何现实中的编辑器/GUI工作负载来说都绰绰有余。
*
* 线程安全：next() 是无锁的（单次原子CAS循环），不分配内存， 且永远不会阻塞——包括在序列预算耗尽时，
* 此时它会将第二个字段向前移动一位，而不是等待真实时钟（参见CAS循环）。
*/
class SnowflakeGenerator : public StableIdGenerator
{
public:
    using worker_type = std::uint16_t;
    using Clocker     = std::function<std::int64_t()>;

    /**
     * @param worker Distinguishes id spaces across processes (e.g. each
     *        sandbox child process gets a distinct worker id assigned by the
     *        Host at spawn time). Defaults to 0 for standalone/single-process use.
     */
    explicit SnowflakeGenerator(worker_type worker = 0, Clocker clock = {}) noexcept;

    SnowflakeGenerator(const SnowflakeGenerator&)            = delete;
    SnowflakeGenerator& operator=(const SnowflakeGenerator&) = delete;
    SnowflakeGenerator(SnowflakeGenerator&&)                 = delete;
    SnowflakeGenerator& operator=(SnowflakeGenerator&&)      = delete;

    [[nodiscard]] worker_type worker() const noexcept { return m_worker; }
    /// Generate a new, process-wide-unique, monotonically non-decreasing id.
    [[nodiscard]] std::uint64_t next() noexcept override;

private:
    [[nodiscard]] std::uint64_t packed() const noexcept;
    [[nodiscard]] std::uint64_t assemble(std::uint64_t seconds, std::uint64_t seq) const noexcept;

    worker_type m_worker;
    // 打包存储 [lastSeconds | seq] 到单个原子量，靠一次 CAS 整体更新——避免
    // "秒" 和 "序列号" 分成两个独立原子量时，两者之间出现的先后不一致窗口。
    std::atomic<std::uint64_t> m_state{0};
    Clocker m_clock{};
};

/**
* @brief 非正式化、基于生成器的 O(1) 双向 Handle <-> StableId 注册表，
* 通过 on_construct/on_destroy 回调实现自动清理。
*
* - 明确地针对一个注册表和一个稳定 ID 生成器进行构造（因此调用者可
*   控制 ID 空间的分区，例如每个沙箱工作线程使用一个生成器）；
* - 其索引作为普通成员状态而非 entt::registry.ctx() ---隐藏状态被持有，这设计上避
*   免了其进入 UndoStack/DocumentSerializer 的 clear()+reload 
*   循环——该注册表旨在用于批处理克隆流程自身的记录管理，而非用于保存撤销快照。
*
* 两者可以安全地共存于同一注册表中：它们会透明地操作相同的稳定标识符（StableId）组件类型。
*
* 不可复制/不可移动：onConstruct/onDestroy lambda 会捕获 `this`，  
* 原因与 Registry 自身的不可移动策略相同（参见 b_registry.h）。
*/
class Identity
{
public:
    explicit Identity(Registry& registry, StableIdGenerator* generator = nullptr);
    ~Identity();

    Identity(const Identity&)            = delete;
    Identity& operator=(const Identity&) = delete;
    Identity(Identity&&)                 = delete;
    Identity& operator=(Identity&&)      = delete;

    void setGenerator(StableIdGenerator* generator);

    // 注： 只是分配一个新的 stable id，不会关联任何实体。
    std::uint64_t mint() const noexcept;

    /// 作为全新的雪花对象并进行附加，如果已存在则为幂等操作。
    StableId ensure(Entity entity); // mint

    /// 附加一个已知的标识符（反序列化 / 恢复/重做）。拒绝冲突。
    void assign(Entity entity, StableId id);

    /// 用于撤销、QtRO 处理器和项目文件恢复的反向查找。
    /// 当标识符未知或实体已死亡时，返回 `entt::null`。
    [[nodiscard]] std::optional<Entity> find(StableId id) const noexcept;

    /// 前向查找。如果实体没有标识符，则抛出 `StableIdError` 异常。
    [[nodiscard]] std::optional<StableId> get(Entity entity) const noexcept;

    [[nodiscard]] bool contains(StableId id) const noexcept;
    [[nodiscard]] bool contains(Entity entity) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    void reserve(std::size_t size);

private:
    void constructed(Registry& registry, Entity entity);
    void destroyed(Registry& registry, Entity entity);
    void updated(Registry& registry, Entity entity);

private:
    class IdentityImpl;
    IdentityImpl* m_impl;
};

} // namespace bakuon::core
