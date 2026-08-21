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

using Cache = detail::Bimap<IdData, uint32_t, IdDataHash>;
static Cache& cache()
{
    static auto* c = new Cache;
    return *c;
}

namespace internal {
uint32_t id_lookdown(QString original)
{
    if (original.isEmpty()) {
        return 0;
    }
    IdData d(std::move(original));
    auto id = cache().left().find(d);
    if (!id.has_value()) {
        id = d.hash;
        cache().insert(d, id.value());
    }
    return id.value();
}

QString id_lookup(uint32_t raw)
{
    return raw == 0 ? QString{} : cache().right().find(raw)->str;
}
} // namespace internal

} // namespace bakuon::gui
