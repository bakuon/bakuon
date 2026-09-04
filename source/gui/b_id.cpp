// 仅本文件（且仅本文件的“Qt 互操作”那两个自由函数）需要 QString 的完整定义；
// b_id.h 只做了前置声明。这两行必须放在 #include "gui/b_id.h" 之前——
// b_id.h 里 Id<Tag>::toQString() 的返回类型是非依赖类型 QString，
// 模板定义阶段就需要看到它的完整定义。
#include <QtCore/QByteArray>
#include <QtCore/QString>

#include "gui/b_id.h"

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bakuon::gui {
namespace detail {

inline unsigned char asciiFold(unsigned char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

// MurmurHash3(32bit) 变体：直接在原始字节上"边做大小写折叠边哈希"，不像旧实现
// 那样先 toCaseFolded() 生成一份折叠后的临时字符串再哈希——省掉了那次堆分配。
//
// 另外，旧实现用 std::memcpy(&kv, data+i*4, 4) 从字节流里直接读一个 uint32_t，
// 注释里写着"考虑小端序"，但 memcpy 读出来的值实际上是*运行平台原生字节序*，
// 在大端平台上会算出不同的哈希——与该函数自己的文档承诺（"同一字符串在任何时间、
// 任何运行实例中都生成相同 id，这对持久化/分布式场景至关重要"）自相矛盾。
// 这里改为手工按字节拼接 uint32_t，哈希结果与平台字节序无关。
inline uint32_t hashCaseInsensitive(std::string_view s) noexcept
{
    constexpr uint32_t C1   = 0xcc9e2d51;
    constexpr uint32_t C2   = 0x1b873593;
    constexpr uint32_t SEED = 0xc70f6907;

    const auto byteAt = [&s](size_t i) noexcept -> uint32_t {
        return static_cast<uint32_t>(asciiFold(static_cast<unsigned char>(s[i])));
    };

    uint32_t hash    = SEED;
    const size_t len = s.size();
    size_t i         = 0;
    for (; i + 4 <= len; i += 4) {
        uint32_t k = byteAt(i) | (byteAt(i + 1) << 8) | (byteAt(i + 2) << 16)
                     | (byteAt(i + 3) << 24);
        k *= C1;
        k = (k << 15) | (k >> 17);
        k *= C2;
        hash ^= k;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5 + 0xe6546b64;
    }

    uint32_t tail = 0;
    switch (len - i) {
    case 3: tail ^= byteAt(i + 2) << 16; [[fallthrough]];
    case 2: tail ^= byteAt(i + 1) << 8; [[fallthrough]];
    case 1:
        tail ^= byteAt(i);
        tail *= C1;
        tail = (tail << 15) | (tail >> 17);
        tail *= C2;
        hash ^= tail;
        break;
    default: break;
    }

    hash ^= static_cast<uint32_t>(len);
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);
    return hash;
}

// 大小写不敏感的哈希 / 相等比较器，均标注 is_transparent。
// 配合 C++20 unordered_map 的异构查找（heterogeneous lookup），
// 可以直接用 std::string_view 去查表而不必先构造出一个 std::string——
// 这意味着"重复用同一个字符串字面量构造已存在的 Id"（最常见的调用模式，
// 例如反复 CommandId("edit.delete")）在命中路径上零堆分配。
struct CaseInsensitiveHash
{
    using is_transparent = void;
    size_t operator()(std::string_view s) const noexcept { return hashCaseInsensitive(s); }
};

struct CaseInsensitiveEqual
{
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (asciiFold(static_cast<unsigned char>(a[i]))
                != asciiFold(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }
};

// 全局字符串驻留表（string interning table）。
//
// 相较旧版基于 detail::Bimap 的实现，这里做了两处简化/优化：
//
//  1. id -> name 不再需要第二张哈希表。旧实现的 Bimap 是通用双向映射，
//     right 侧（uint32_t -> IdData）本身也是一张 unordered_map，既要再哈希
//     一次，还把字符串内容完整地又拷贝存了一份。但这里的 id 完全是我们自己
//     按 1, 2, 3... 顺序分配的连续整数，因此 id -> name 直接用 vector 按
//     下标存"指向 name -> id 那张 map 里 key 的指针"即可 O(1) 反查，
//     既不需要哈希，也不需要重复存字符串内容。
//     前提：std::unordered_map 在“只插入、不删除”的使用模式下，节点（因而
//     key 的地址）永远稳定——本表确实只增不减（id 一旦分配就永不回收/复用，
//     与旧实现的语义完全一致）。
//
//  2. 不再需要旧实现里那段"用 hash 当 id、冲突时线性探测"的逻辑——那段逻辑
//     本身就是为了修复"曾经直接把哈希值当 id 用"的 bug 而补上的。既然 id
//     现在只是一个单调递增的计数器，从设计上就不可能发生 id 冲突。
class IdTable
{
public:
    uint32_t lookupId(std::string_view name)
    {
        if (name.empty()) {
            return 0;
        }

        {
            std::shared_lock<std::shared_mutex> read(m_mutex);
            if (auto it = m_nameToId.find(name); it != m_nameToId.end()) {
                return it->second;
            }
        }

        std::unique_lock<std::shared_mutex> write(m_mutex);
        // 双重检查：从释放读锁到拿到写锁之间，可能有别的线程已经插入了
        // 同一个名字。
        if (auto it = m_nameToId.find(name); it != m_nameToId.end()) {
            return it->second;
        }

        const uint32_t id   = static_cast<uint32_t>(m_idToName.size()) + 1;
        const auto [it, ok] = m_nameToId.emplace(std::string(name), id);
        (void) ok;
        m_idToName.push_back(&it->first);
        return id;
    }

    std::string_view lookupName(uint32_t id) const noexcept
    {
        if (id == 0) {
            return {};
        }
        std::shared_lock<std::shared_mutex> read(m_mutex);
        if (id > m_idToName.size()) {
            return {};
        }
        return *m_idToName[id - 1];
    }

private:
    // 注：原实现完全没有做任何线程同步（Bimap/QHash 均非线程安全）。这在评审中
    // 属于一个潜在设计问题——只要有任何子系统在非 GUI 线程构造 CommandId/ContextId
    // （例如后台任务、插件工作线程），就是一次未加锁的数据竞争，只是目前尚未
    // 触发。这里用 std::shared_mutex 加了一层保守的读写锁：命中（绝大多数）路径
    // 只需共享锁，真正的写入（新名字第一次注册）才升级为独占锁。如果能确认
    // Id 只会在 GUI 线程构造，这把锁可以整个去掉以进一步提速；目前选择保留，
    // 以避免把一个隐藏的线程安全问题原样带入新实现。
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, uint32_t, CaseInsensitiveHash, CaseInsensitiveEqual> m_nameToId;
    std::vector<const std::string*> m_idToName;
};

inline IdTable& table()
{
    // 有意泄漏，与旧实现一致：驻留表与进程等长，进程退出时由 OS 一并回收，
    // 避免全局对象析构顺序问题（参见项目里其它地方"析构顺序是有依赖的"的教训）。
    static auto* t = new IdTable;
    return *t;
}

} // namespace detail

namespace internal {

uint32_t lookupId(std::string_view name)
{
    return detail::table().lookupId(name);
}

std::string_view lookupName(uint32_t raw) noexcept
{
    return detail::table().lookupName(raw);
}

} // namespace internal
} // namespace bakuon::gui
