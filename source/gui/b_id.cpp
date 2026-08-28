#include "gui/b_id.h"

#include "gui/detail/b_bimap.h"
#include "gui/detail/b_utils.h"

namespace bakuon::gui {

class IdData
{
public:
    IdData() = default;

    explicit IdData(QString original)
        : str(std::move(original).toCaseFolded()) // 大小写不敏感
    {
        hash = detail::hashString(str.toUtf8());
    }

    ~IdData() {}

    friend auto qHash(const IdData& s) { return QT_PREPEND_NAMESPACE(qHash)(s.hash, 0); }

    friend bool operator==(const IdData& s1, const IdData& s2)
    {
        return s1.hash == s2.hash && s1.str == s2.str;
    }

    QString str;
    uint32_t hash{0};
};

// 定义Left: IdData 哈希函数对象
struct IdDataHash
{
    std::size_t operator()(const IdData& d) const noexcept
    {
        return static_cast<std::size_t>(d.hash);
    }
};

using LookupTable = detail::Bimap<IdData, uint32_t, IdDataHash>;
static LookupTable& lookup()
{
    static auto* t = new LookupTable;
    return *t;
}

namespace internal {
uint32_t lookupId(QString original)
{
    if (original.isEmpty()) {
        return 0;
    }
    IdData d(std::move(original));
    if (auto existing = lookup().left().find(d); existing.has_value()) {
        return existing.value();
    }

    // BUGFIX: 使用单调递增的 raw id，而不是直接把 hash 当作 id。
    // 原先用 d.hash 作 id：一旦两个不同字符串碰撞到同一 uint32_t hash，
    // Bimap::insert 会因 right 侧冲突失败，后续 lookupName 也会返回错误名字。
    // 从 1 起分配（0 保留给 invalid），并在极端冲突时线性探测。
    static uint32_t nextId = 1;
    uint32_t candidate     = nextId;
    // 保证 right 侧唯一；正常路径几乎不会进入循环
    while (lookup().containsRight(candidate) || candidate == 0) {
        ++candidate;
        if (candidate == 0) {
            ++candidate; // 跳过 0
        }
    }
    nextId = candidate + 1;
    if (nextId == 0) {
        ++nextId;
    }

    if (!lookup().insert(d, candidate)) {
        // 理论上不应发生（left 已检查不存在，right 已探测）；防御性回退
        qWarning() << "Id::lookupId: unexpected bimap insert failure for" << d.str;
        return 0;
    }
    return candidate;
}

QString lookupName(uint32_t raw)
{
    return raw == 0 ? QString{} : lookup().right().find(raw)->str;
}
} // namespace internal

} // namespace bakuon::gui
