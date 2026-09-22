#include "core/b_workspace.h"

#include <algorithm>

namespace bakuon::core {

namespace detail {
struct WorkspaceHooks
{
    using Entry = std::pair<std::size_t, Workspace::DomainCallback>;
    std::vector<Entry> created;
    std::vector<Entry> destroying;
    std::size_t nextId = 1;
};
} // namespace detail

namespace {

using Hooks    = detail::WorkspaceHooks;
using HookList = std::vector<Hooks::Entry> Hooks::*;

Connection addHook(const std::shared_ptr<Hooks>& hooks, HookList list,
                   Workspace::DomainCallback callback)
{
    const std::size_t id = hooks->nextId++;
    (*hooks.*list).emplace_back(id, std::move(callback));
    // 只持 weak_ptr：Workspace 先于 Connection 销毁时，断开是安全的空操作。
    return Connection([weak = std::weak_ptr<Hooks>(hooks), list, id] {
        if (const auto locked = weak.lock()) {
            auto& entries = (*locked).*list;
            entries.erase(std::remove_if(entries.begin(),
                                         entries.end(),
                                         [id](const Hooks::Entry& e) { return e.first == id; }),
                          entries.end());
        }
    });
}

void notify(const Hooks& hooks, HookList list, Domain& domain)
{
    // 拷贝快照：回调里允许 disconnect/新增钩子。
    const std::vector<Hooks::Entry> snapshot = hooks.*list;
    for (const auto& entry : snapshot) {
        if (entry.second) {
            entry.second(domain);
        }
    }
}

void ensureSameName(const Domain& existing, std::string_view name)
{
    if (existing.name() != name) {
        throw DomainError("domain id collision: '" + std::string(name) + "' vs existing '"
                          + std::string(existing.name()) + "'");
    }
}

} // namespace

Workspace::Workspace()
    : m_hooks(std::make_shared<detail::WorkspaceHooks>())
{
}

Workspace::~Workspace()
{
    try {
        clear();
    } catch (...) {
        // 析构中不能抛。钩子抛异常时放弃剩余通知，由 m_domains 的成员析构兜底回收。
    }
}

Domain* Workspace::create(std::string_view name, const Initializer& init)
{
    assertOwnerThread();
    const DomainId id = DomainId::fromName(name);
    if (!id.isValid()) {
        throw DomainError("Workspace::create: domain name must not be empty");
    }
    if (const Domain* existing = find(id)) {
        ensureSameName(*existing, name);
        return nullptr;
    }
    return &insert(id, std::string(name), init);
}

Domain& Workspace::require(std::string_view name, const Initializer& init)
{
    assertOwnerThread();
    const DomainId id = DomainId::fromName(name);
    if (!id.isValid()) {
        throw DomainError("Workspace::require: domain name must not be empty");
    }
    if (Domain* existing = find(id)) {
        ensureSameName(*existing, name);
        if (existing->isClosing()) {
            throw DomainError("Workspace::require: domain '" + std::string(name)
                              + "' is being destroyed");
        }
        return *existing;
    }
    return insert(id, std::string(name), init);
}

Domain& Workspace::createAnonymous(std::string label, const Initializer& init)
{
    assertOwnerThread();
    return insert(DomainId{m_nextAnonymous++}, std::move(label), init);
}

Domain& Workspace::insert(DomainId id, std::string name, const Initializer& init)
{
    auto domain = std::unique_ptr<Domain>(new Domain(id, std::move(name), m_nextSerial++));
    if (init) {
        init(*domain); // 抛异常：domain 随 unique_ptr 析构，服务逆序销毁，Workspace 未被修改
    }
    Domain& ref = *domain;
    m_domains.emplace(id, std::move(domain));
    notify(*m_hooks, &Hooks::created, ref);
    return ref;
}

bool Workspace::destroy(DomainId id)
{
    assertOwnerThread();
    const auto it = m_domains.find(id);
    if (it == m_domains.end() || it->second->m_closing) {
        return false;
    }

    Domain& domain   = *it->second;
    domain.m_closing = true;
    try {
        notify(*m_hooks, &Hooks::destroying, domain);
    } catch (...) {
        domain.m_closing = false; // 钩子否决/失败：域保持可用
        throw;
    }

    // 回调期间 m_domains 可能被修改（rehash 会使 it 失效），重新定位。
    const auto current = m_domains.find(id);
    assert(current != m_domains.end() && current->second.get() == &domain);
    std::unique_ptr<Domain> owned = std::move(current->second);
    m_domains.erase(current);
    return true; // owned 析构：服务逆序 → Container
}

void Workspace::clear()
{
    assertOwnerThread();
    // 逆创建序：后创建的域可能观察/依赖先创建的域。
    // 每轮重新挑 serial 最大者，钩子里新建/销毁域也不会让遍历失效。
    for (;;) {
        Domain* last = nullptr;
        for (const auto entry : m_domains) {
            Domain* candidate = entry.second.get();
            if (!candidate->isClosing() && (!last || candidate->serial() > last->serial())) {
                last = candidate;
            }
        }
        if (!last) {
            break;
        }
        destroy(last->id());
    }
}

Domain* Workspace::find(DomainId id) noexcept
{
    const auto it = m_domains.find(id);
    return it == m_domains.end() ? nullptr : it->second.get();
}

const Domain* Workspace::find(DomainId id) const noexcept
{
    const auto it = m_domains.find(id);
    return it == m_domains.end() ? nullptr : it->second.get();
}

Domain* Workspace::find(std::string_view name) noexcept
{
    Domain* domain = find(DomainId::fromName(name));
    return (domain && domain->name() == name) ? domain : nullptr;
}

const Domain* Workspace::find(std::string_view name) const noexcept
{
    const Domain* domain = find(DomainId::fromName(name));
    return (domain && domain->name() == name) ? domain : nullptr;
}

Domain* Workspace::resolve(DomainHandle handle) noexcept
{
    Domain* domain = find(handle.id());
    return (domain && domain->serial() == handle.serial()) ? domain : nullptr;
}

const Domain* Workspace::resolve(DomainHandle handle) const noexcept
{
    const Domain* domain = find(handle.id());
    return (domain && domain->serial() == handle.serial()) ? domain : nullptr;
}

std::vector<DomainHandle> Workspace::snapshot() const
{
    std::vector<DomainHandle> handles;
    handles.reserve(m_domains.size());
    for (const auto entry : m_domains) {
        handles.push_back(entry.second->handle());
    }
    std::sort(handles.begin(), handles.end(), [](const DomainHandle& a, const DomainHandle& b) {
        return a.serial() < b.serial();
    });
    return handles;
}

Connection Workspace::onDomainCreated(DomainCallback callback)
{
    return addHook(m_hooks, &Hooks::created, std::move(callback));
}

Connection Workspace::onDomainDestroying(DomainCallback callback)
{
    return addHook(m_hooks, &Hooks::destroying, std::move(callback));
}

} // namespace bakuon::core
