#include "core/b_domain.h"

namespace bakuon::core {

Domain::Domain(DomainId id, std::string name, std::uint64_t serial)
    : m_id(id)
    , m_name(std::move(name))
    , m_serial(serial)
{
}

Domain::~Domain()
{
    // 显式逆序：std::vector 析构时元素的销毁顺序标准未规定。
    while (!m_services.empty()) {
        m_services.pop_back();
    }
    // 随后 m_container（即 entt::registry）才被销毁。
}

} // namespace bakuon::core
