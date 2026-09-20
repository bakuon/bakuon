#include "core/b_identifier.h"

#include <chrono>
#include <sstream>
#include <unordered_map>

namespace bakuon::core {
namespace {

[[nodiscard]] [[maybe_unused]] std::int64_t system_now_ms() noexcept
{
    using namespace std::chrono;
    const auto now = system_clock::now().time_since_epoch();
    return duration_cast<milliseconds>(now).count();
}

[[nodiscard]] [[maybe_unused]] std::int64_t system_now() noexcept
{
    using namespace std::chrono;
    const auto now = system_clock::now().time_since_epoch();
    return duration_cast<seconds>(now).count();
}

} // namespace

SnowflakeView toSnowflake(StableId id) noexcept
{
    SnowflakeView view;
    if (!id.isValid()) {
        return view;
    }
    const auto raw   = id.value();
    view.sequence    = static_cast<std::uint16_t>(raw & snowflake::kSequenceMask);
    view.worker      = static_cast<std::uint16_t>((raw >> snowflake::kSequenceBits)
                                                  & snowflake::kWorkerMask);
    const auto ticks = (raw >> snowflake::kTimestampShift) & snowflake::kTimestampMask;
    view.timestamp   = ticks + snowflake::kEpochUnixSeconds;
    return view;
}

std::string toHex(StableId id)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << id.value();
    return out.str();
}

std::string toString(StableId id)
{
    if (!id.isValid()) {
        return "sid:invalid";
    }
    const auto view = toSnowflake(id);
    std::ostringstream out;
    out << "sid:" << toHex(id) << " wid=" << view.worker << " seq=" << view.sequence;
    return out.str();
}

SnowflakeGenerator::SnowflakeGenerator(worker_type worker, Clocker clock) noexcept
    : m_worker(worker)
    , m_clock(clock ? std::move(clock) : Clocker{&system_now})
{
}

std::uint64_t SnowflakeGenerator::next() noexcept
{
    for (;;) {
        const std::uint64_t now = packed();
        std::uint64_t state     = m_state.load(std::memory_order_relaxed);

        const std::uint64_t lastSeconds = state >> snowflake::kSequenceBits;
        const std::uint64_t seq         = state & snowflake::kSequenceMask;

        std::uint64_t newSeconds;
        std::uint64_t newSequence;
        if (now > lastSeconds) {
            newSeconds  = now;
            newSequence = 0;
        } else {
            // 同一秒内(或系统时钟未前进/被回拨): 序列号自增。
            newSeconds  = lastSeconds;
            newSequence = seq + 1;
            if (newSequence > snowflake::kSequenceMask) {
                // 单秒 65536 个 id 的预算耗尽: 直接把"秒槽"滚动进位，而不是
                // 自旋等待真实时钟前进——宁可让内部时间戳短暂跑得比真实时间快，
                // 也要保持 next() 无锁、无等待。
                newSeconds  = lastSeconds + 1;
                newSequence = 0;
            }
        }

        const std::uint64_t newState = (newSeconds << snowflake::kSequenceBits) | newSequence;
        if (m_state.compare_exchange_weak(state, newState, std::memory_order_relaxed)) {
            return assemble(newSeconds, newSequence);
        }
        // CAS 失败: 另一线程抢先更新了 m_state，重试。
    }
}

std::uint64_t SnowflakeGenerator::packed() const noexcept
{
    using namespace std::chrono;
    const auto now     = m_clock();
    const auto elapsed = now - snowflake::kEpochUnixSeconds;
    // 钳制到 31 位有效范围: 系统时钟早于纪元(几乎不可能，但做防御性处理)时
    // 不产生未定义的比特模式，直接归零。
    if (elapsed < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(elapsed) & snowflake::kTimestampMask;
}

std::uint64_t SnowflakeGenerator::assemble(std::uint64_t seconds, std::uint64_t seq) const noexcept
{
    std::uint64_t id = 0;
    id |= (seconds & snowflake::kTimestampMask)
          << (snowflake::kWorkerBits + snowflake::kSequenceBits);
    id |= (static_cast<std::uint64_t>(m_worker) & ((std::uint64_t{1} << snowflake::kWorkerBits) - 1))
          << snowflake::kSequenceBits;
    id |= seq & snowflake::kSequenceMask;
    return id; // 31+16+16 = 63 位已用满，最高位(符号位)天然留空为 0。
}

class Identifier::IdentityImpl
{
public:
    IdentityImpl(Registry& reg, StableIdGenerator* gen)
        : registry(reg)
        , defaultGenerator()
        , generator(gen ? gen : &defaultGenerator)
    {
    }

    Registry& registry;
    SnowflakeGenerator defaultGenerator;
    StableIdGenerator* generator = nullptr;

    ScopedConnection construct_conn;
    ScopedConnection destroy_conn;
    ScopedConnection update_conn;

    std::unordered_map<StableId, Entity> id_to_entity;
    std::unordered_map<Entity, StableId> entity_to_id;
};

Identifier::Identifier(Registry& registry, StableIdGenerator* generator)
    : m_impl(new IdentityImpl(registry, generator))
{
    m_impl->construct_conn = registry.on_construct<StableId>().connect<&Identifier::constructed>(
        *this);
    m_impl->destroy_conn = registry.on_destroy<StableId>().connect<&Identifier::destroyed>(*this);
    m_impl->update_conn  = registry.on_destroy<StableId>().connect<&Identifier::updated>(*this);

    // 填充：钩子仅能看到*未来的*事件，因此在本注册对象创建之前已存在的任何 StableId 组件都需要提前索引。
    m_impl->registry.view<StableId>().each([this](Entity entity, const StableId& id) {
        if (id.isValid()) {
            m_impl->entity_to_id[entity] = id;
            m_impl->id_to_entity[id]     = entity;
        }
    });
}

Identifier::~Identifier()
{
    delete m_impl;
    m_impl = nullptr;
}

void Identifier::setGenerator(StableIdGenerator* generator)
{
    m_impl->generator = generator;
    if (!m_impl->generator) {
        m_impl->generator = &m_impl->defaultGenerator;
    }
}

std::uint64_t Identifier::mint() const noexcept
{
    return m_impl->generator->next();
}

StableId Identifier::ensure(Entity entity)
{
    if (!m_impl->registry.valid(entity)) {
        throw StableIdError("cannot mint StableId to an invalid entity");
    }

    if (const StableId* existing = m_impl->registry.try_get<StableId>(entity)) {
        return *existing;
    }
    StableId id{m_impl->generator->next()};
    // triggers constructed -> indexes
    m_impl->registry.emplace<StableId>(entity, id);
    return id;
}

void Identifier::assign(Entity entity, StableId id)
{
    if (!m_impl->registry.valid(entity)) {
        throw StableIdError("cannot assign StableId to an invalid entity");
    }
    if (!id.isValid()) {
        throw StableIdError("cannot bind an invalid StableId");
    }

    // if (const auto occupant = this->find(id); occupant.has_value() && occupant != entity) {
    //     throw StableIdError("StableId collision on bind");
    // }
    // if (m_impl->registry.all_of<StableId>(entity)) {
    //     const auto current = m_impl->registry.get<StableId>(entity);
    //     if (current == id) {
    //         return;
    //     }
    // }

    // triggers constructed/updated -> indexes new value
    m_impl->registry.emplace_or_replace<StableId>(entity, id);
}

Entity Identifier::find(StableId id) const noexcept
{
    const auto it = m_impl->id_to_entity.find(id);
    return it != m_impl->id_to_entity.end() ? it->second : nullentity;
}

StableId Identifier::get(Entity entity) const noexcept
{
    const auto it = m_impl->entity_to_id.find(entity);
    return it != m_impl->entity_to_id.end() ? it->second : StableId{};
}

bool Identifier::contains(Entity entity) const noexcept
{
    return this->get(entity).isValid();
}

bool Identifier::contains(StableId id) const noexcept
{
    return this->find(id) != nullentity;
}

void Identifier::reserve(std::size_t size)
{
    m_impl->id_to_entity.reserve(size);
    m_impl->entity_to_id.reserve(size);
}

std::size_t Identifier::size() const noexcept
{
    return m_impl->id_to_entity.size();
}

void Identifier::constructed(Registry& registry, Entity entity)
{
    if (const auto& id = registry.get<StableId>(entity); id.isValid()) {
        m_impl->id_to_entity[id]     = entity;
        m_impl->entity_to_id[entity] = id;
    }
}

void Identifier::destroyed(Registry& /*registry*/, Entity entity)
{
    const auto it = m_impl->entity_to_id.find(entity);
    if (it == m_impl->entity_to_id.end()) {
        return;
    }
    m_impl->id_to_entity.erase(it->second);
    m_impl->entity_to_id.erase(it);
}

void Identifier::updated(Registry& /*registry*/, Entity entity)
{
    const auto it = m_impl->entity_to_id.find(entity);
    if (it == m_impl->entity_to_id.end()) {
        return;
    }
    m_impl->entity_to_id[entity]     = it->second;
    m_impl->id_to_entity[it->second] = entity;
}

} // namespace bakuon::core
