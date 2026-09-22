#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/b_connection.h"
#include "core/b_domain.h"

namespace bakuon::core {

namespace detail {
struct WorkspaceHooks;
}

/**
 * @brief 域隔离的多路复用器：按需创建/持有/销毁多个相互独立的 Domain（各自一个 registry）。
 *
 * ## 线程模型
 * create/require/destroy/clear/onDomain* 只能在创建 Workspace 的线程调用（debug 下断言）。
 * find/resolve/each 在没有并发拓扑修改时可读。Domain 内部数据不加锁，线程亲和性由调用方决定。
 *
 * ## 引用有效期
 * 返回的 Domain& / Domain* 在该域被 destroy()/clear() 之前一直有效（地址稳定，与其它域的
 * 增删无关）。需要跨越"可能被销毁"的边界持有时，存 DomainHandle，用 resolve() 取回。
 *
 * ## 销毁时序
 * destroy(id)：标记 closing → 触发 onDomainDestroying（此时仍可 find）→ 从表中摘除 →
 * 逆序销毁服务 → 销毁 Container。~Workspace()/clear() 按域创建的逆序销毁。
 * 钩子里可以创建其它域、销毁其它域；对正在销毁的域再次 destroy 返回 false。
 *
 * 不可拷贝、不可移动（与 Container/CommandWorkspace 一致）。
 */
class Workspace
{
public:
    using Initializer    = std::function<void(Domain&)>;
    using DomainCallback = std::function<void(Domain&)>;

    Workspace();
    ~Workspace();

    Workspace(const Workspace&)            = delete;
    Workspace& operator=(const Workspace&) = delete;
    Workspace(Workspace&&)                 = delete;
    Workspace& operator=(Workspace&&)      = delete;

    // ---- 创建 ----

    /// 创建命名域；同名已存在返回 nullptr；名字为空或与另一个已存在域发生哈希碰撞抛 DomainError。
    /// init 在域插入表之前执行，抛异常时域被丢弃、Workspace 状态不变。
    Domain* create(std::string_view name, const Initializer& init = {});

    /// 按需分配：存在则返回，否则创建（并执行 init）。域正在销毁时抛 DomainError。
    Domain& require(std::string_view name, const Initializer& init = {});

    /// 创建匿名域（典型：每个打开的文档一个），id 由 Workspace 分配；label 仅用于诊断。
    Domain& createAnonymous(std::string label = {}, const Initializer& init = {});

    // ---- 销毁 ----

    /// 不存在或已在销毁中返回 false。
    bool destroy(DomainId id);
    /// 逆创建序销毁全部域。
    void clear();

    // ---- 查询 ----

    [[nodiscard]] Domain* find(DomainId id) noexcept;
    [[nodiscard]] const Domain* find(DomainId id) const noexcept;
    [[nodiscard]] Domain* find(std::string_view name) noexcept;
    [[nodiscard]] const Domain* find(std::string_view name) const noexcept;
    [[nodiscard]] Domain* resolve(DomainHandle handle) noexcept;
    [[nodiscard]] const Domain* resolve(DomainHandle handle) const noexcept;

    [[nodiscard]] bool contains(DomainId id) const noexcept { return find(id) != nullptr; }
    [[nodiscard]] std::size_t count() const noexcept { return m_domains.size(); }

    /// 按创建顺序遍历。先对 handle 做快照再逐个 resolve()，回调里销毁其它域是安全的
    /// （已销毁的域会被跳过）；回调里新建的域不会被本次遍历访问。
    template<typename Func>
    void each(Func&& func)
    {
        for (const DomainHandle& handle : snapshot()) {
            if (Domain* domain = resolve(handle)) {
                func(*domain);
            }
        }
    }

    // ---- 生命周期钩子 ----

    /// 域已完整构造（含 init）并插入表之后触发。
    [[nodiscard]] Connection onDomainCreated(DomainCallback callback);
    /// 域即将销毁时触发，此时域仍完整可用；回调抛出的异常会中止本次销毁并向上传播。
    /// 返回的 Connection 可以比 Workspace 活得更久。
    [[nodiscard]] Connection onDomainDestroying(DomainCallback callback);

private:
    Domain& insert(DomainId id, std::string name, const Initializer& init);
    [[nodiscard]] std::vector<DomainHandle> snapshot() const;
    void assertOwnerThread() const noexcept
    {
        assert(m_ownerThread == std::this_thread::get_id()
               && "Workspace 的拓扑操作只能在创建它的线程调用");
    }

private:
    entt::dense_map<DomainId, std::unique_ptr<Domain>> m_domains;
    std::shared_ptr<detail::WorkspaceHooks> m_hooks;
    std::uint64_t m_nextSerial           = 1;
    DomainId::value_type m_nextAnonymous = 1;
    std::thread::id m_ownerThread        = std::this_thread::get_id();
};

} // namespace bakuon::core
