#pragma once

#include <functional>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace bakuon::gui::detail {

/**
 * @brief 通用双向映射容器（轻量级 Boost::Bimap 替代）
 *
 * 维护 Left ↔ Right 的严格一对一双向映射（双射/Bijection）。
 * 两个方向均以 O(1) 均摊复杂度支持查找、插入、删除。
 *
 * 与 Boost::Bimap 的差异：
 *   - 仅支持 unordered（哈希）存储策略
 *   - 仅支持 one-to-one 关系
 *   - 零外部依赖
 *   - 提供 left()/right() 视图代理，const 安全
 *
 * @tparam Left         左键类型
 * @tparam Right        右键类型（必须与 Left 不同）
 * @tparam LeftHash     左键哈希策略
 * @tparam RightHash    右键哈希策略
 * @tparam LeftEqual    左键相等策略
 * @tparam RightEqual   右键相等策略
 *
 * 使用示例：
 * @code
 *  Bimap<std::string, int> bm;
 *  bm.insert("one", 1);
 *  bm.insert("two", 2);
 *
 *  // left 视图（以 string 为键）
 *  auto v = bm.left().find("one");    // std::optional<int>{1}
 *  int  i = bm.left().at("one");      // 1，不存在则抛 std::out_of_range
 *  bool b = bm.left().contains("one");// true
 *
 *  bm.left().erase("one");            // 删除，右侧同步
 *
 *  // right 视图（以 int 为键）
 *  auto k = bm.right().find(2);       // optional<string>{"two"}
 *  bm.right().erase(2);               // 删除，左侧同步
 *
 *  // 直接方法（不经过视图，语义等价）
 *  bm.insert("three", 3);
 *  bm.insertOrReplace("three", 33); // 替换旧值
 *  bm.eraseLeft("three");
 *  bm.eraseRight(33);
 * 
 *  // 遍历（按 left→right 方向遍历）
 *  for (auto& [l, r] : bm) { ... }
 *  
 *  // 迭代右视图（按 right→left 方向）
 *  for (auto& [right, left] : bm.right())
 *
 *  // const bimap（视图自动退化为只读）
 *  const auto& cbm = bm;
 *  cbm.left().find("one");    // OK：只读操作
 *  cbm.left().at("one");      // OK
 *  // cbm.left().erase("one"); // 编译错误：const 视图无 erase
 * @endcode
 */
template<typename Left, typename Right, typename LeftHash = std::hash<Left>,
         typename RightHash = std::hash<Right>, typename LeftEqual = std::equal_to<Left>,
         typename RightEqual = std::equal_to<Right>>
class Bimap
{
    static_assert(!std::is_same_v<Left, Right>,
                  "Bimap: Left and Right must be distinct types to avoid view ambiguity.");

    // 内部存储类型
    using LeftMap  = std::unordered_map<Left, Right, LeftHash, LeftEqual>;
    using RightMap = std::unordered_map<Right, Left, RightHash, RightEqual>;

    // 前置声明，视图类需要访问私有成员
    template<bool IsConst>
    class LeftViewImpl;
    template<bool IsConst>
    class RightViewImpl;

public:
    // 公开类型定义
    using left_type  = Left;
    using right_type = Right;
    using size_type  = std::size_t;
    using value_type = typename LeftMap::value_type; // pair<const Left, Right>

    using left_view        = LeftViewImpl<false>;
    using const_left_view  = LeftViewImpl<true>;
    using right_view       = RightViewImpl<false>;
    using const_right_view = RightViewImpl<true>;

    // 构造与赋值
    Bimap()                        = default;
    Bimap(const Bimap&)            = default;
    Bimap& operator=(const Bimap&) = default;
    Bimap(Bimap&&)                 = default;
    Bimap& operator=(Bimap&&)      = default;
    ~Bimap()                       = default;

    explicit Bimap(std::initializer_list<std::pair<Left, Right>> list)
    {
        m_l2r.reserve(list.size());
        m_r2l.reserve(list.size());
        for (auto& [l, r] : list)
            insert(l, r);
    }

    /**
     * @brief 插入一对映射关系（严格不重复）
     *
     * Left 或 Right 任意一方已存在时，返回 false 且不修改容器。
     * 若需更新已有映射，请先调用 eraseLeft / eraseRight，或使用
     * insert_or_replace。
     *
     * @return true: 成功插入；false: 存在冲突，未插入
     */
    bool insert(Left left, Right right)
    {
        if (m_l2r.count(left) > 0 || m_r2l.count(right) > 0)
            return false;

        m_r2l.emplace(right, left);
        m_l2r.emplace(std::move(left), std::move(right));
        return true;
    }

    bool insert(std::pair<Left, Right> pair)
    {
        return insert(std::move(pair.first), std::move(pair.second));
    }

    /**
     * @brief 插入或替换
     *
     * 若 Left 已存在：先清除旧 (left, oldRight) 及其反向项，再插入新对。
     * 若 Right 已被其他 Left 占用：同样先清除旧项，再插入。
     * 保证容器始终处于一致的双射状态。
     */
    void insertOrReplace(Left left, Right right)
    {
        // 先清除可能冲突的旧映射
        if (auto it = m_r2l.find(right); it != m_r2l.end()) {
            m_l2r.erase(it->second);
            m_r2l.erase(it);
        }
        if (auto it = m_l2r.find(left); it != m_l2r.end()) {
            m_r2l.erase(it->second);
            m_l2r.erase(it);
        }
        m_r2l.emplace(right, left);
        m_l2r.emplace(std::move(left), std::move(right));
    }

    /// 以 Left 键删除，返回是否删除了元素（双侧同步）
    bool eraseLeft(const Left& key) noexcept
    {
        auto it = m_l2r.find(key);
        if (it == m_l2r.end())
            return false;
        m_r2l.erase(it->second);
        m_l2r.erase(it);
        return true;
    }

    /// 以 Right 键删除，返回是否删除了元素（双侧同步）
    bool eraseRight(const Right& key) noexcept
    {
        auto it = m_r2l.find(key);
        if (it == m_r2l.end())
            return false;
        m_l2r.erase(it->second);
        m_r2l.erase(it);
        return true;
    }

    [[nodiscard]] std::optional<Right> findRight(const Left& key) const noexcept
    {
        auto it = m_l2r.find(key);
        return it != m_l2r.end() ? std::optional<Right>{it->second} : std::nullopt;
    }

    [[nodiscard]] std::optional<Left> findLeft(const Right& key) const noexcept
    {
        auto it = m_r2l.find(key);
        return it != m_r2l.end() ? std::optional<Left>{it->second} : std::nullopt;
    }

    [[nodiscard]] bool containsLeft(const Left& key) const noexcept { return m_l2r.count(key) > 0; }
    [[nodiscard]] bool containsRight(const Right& key) const noexcept
    {
        return m_r2l.count(key) > 0;
    }

    // 容量操作
    [[nodiscard]] size_type size() const noexcept { return m_l2r.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_l2r.empty(); }

    void clear() noexcept
    {
        m_l2r.clear();
        m_r2l.clear();
    }

    void reserve(size_type n)
    {
        m_l2r.reserve(n);
        m_r2l.reserve(n);
    }

    /**
     * @brief 获取左键视图代理
     *
     * 非 const Bimap 返回可写视图，const Bimap 返回只读视图。
     * 视图生命周期与 Bimap 绑定，不得在 Bimap 销毁后使用视图。
     *
     * @code
     *   bm.left().find("key");         // optional 查找
     *   bm.left().at("key");           // 抛异常查找
     *   bm.left().erase("key");        // 删除
     *   for (auto& [l,r] : bm.left()) // 迭代
     * @endcode
     */
    [[nodiscard]] left_view left() noexcept { return left_view{*this}; }
    [[nodiscard]] const_left_view left() const noexcept { return const_left_view{*this}; }

    /**
     * @brief 获取右键视图代理
     */
    [[nodiscard]] right_view right() noexcept { return right_view{*this}; }
    [[nodiscard]] const_right_view right() const noexcept { return const_right_view{*this}; }

    // 全局迭代器（遍历 Left→Right 方向）
    auto begin() noexcept { return m_l2r.begin(); }
    auto end() noexcept { return m_l2r.end(); }
    auto begin() const noexcept { return m_l2r.begin(); }
    auto end() const noexcept { return m_l2r.end(); }
    auto cbegin() const noexcept { return m_l2r.cbegin(); }
    auto cend() const noexcept { return m_l2r.cend(); }

    // 比较操作
    bool operator==(const Bimap& rhs) const { return m_l2r == rhs.m_l2r; }
    bool operator!=(const Bimap& rhs) const { return !(*this == rhs); }

    // 交换
    void swap(Bimap& other) noexcept
    {
        m_l2r.swap(other.m_l2r);
        m_r2l.swap(other.m_r2l);
    }

    friend void swap(Bimap& a, Bimap& b) noexcept { a.swap(b); }

private:
    LeftMap m_l2r;  ///< Left  → Right 方向索引
    RightMap m_r2l; ///< Right → Left  方向索引

    /**
     * @brief 左键视图模板实现
     * @tparam IsConst true: 只读视图（const Bimap&）；false: 读写视图（bimap&）
     */
    template<bool IsConst>
    class LeftViewImpl
    {
        friend class Bimap;

        using BimapRef = std::conditional_t<IsConst, const Bimap&, Bimap&>;
        BimapRef m_bm;

        explicit LeftViewImpl(BimapRef bm) noexcept
            : m_bm(bm)
        {
        }

    public:
        using iterator       = std::conditional_t<IsConst, typename LeftMap::const_iterator,
                                                  typename LeftMap::iterator>;
        using const_iterator = typename LeftMap::const_iterator;

        // 查找
        /// optional 查找（推荐，无异常）
        [[nodiscard]] std::optional<Right> find(const Left& key) const noexcept
        {
            auto it = m_bm.m_l2r.find(key);
            return it != m_bm.m_l2r.end() ? std::optional<Right>{it->second} : std::nullopt;
        }

        /// 引用查找（不存在抛 std::out_of_range）
        [[nodiscard]] const Right& at(const Left& key) const
        {
            auto it = m_bm.m_l2r.find(key);
            if (it == m_bm.m_l2r.end())
                throw std::out_of_range("Bimap::left::at: key not found");
            return it->second;
        }

        /// 是否包含
        [[nodiscard]] bool contains(const Left& key) const noexcept
        {
            return m_bm.m_l2r.count(key) > 0;
        }

        // 仅 IsConst=false 可用

        /// 以 Left 键删除，返回是否删除了元素
        template<bool C = IsConst>
        std::enable_if_t<!C, bool> erase(const Left& key) noexcept
        {
            return m_bm.eraseLeft(key);
        }

        // 迭代器
        template<bool C = IsConst>
        std::enable_if_t<!C, iterator> begin() noexcept
        {
            return m_bm.m_l2r.begin();
        }

        template<bool C = IsConst>
        std::enable_if_t<!C, iterator> end() noexcept
        {
            return m_bm.m_l2r.end();
        }

        const_iterator begin() const noexcept { return m_bm.m_l2r.cbegin(); }
        const_iterator end() const noexcept { return m_bm.m_l2r.cend(); }
        const_iterator cbegin() const noexcept { return m_bm.m_l2r.cbegin(); }
        const_iterator cend() const noexcept { return m_bm.m_l2r.cend(); }

        // 容量
        [[nodiscard]] size_type size() const noexcept { return m_bm.m_l2r.size(); }
        [[nodiscard]] bool empty() const noexcept { return m_bm.m_l2r.empty(); }
    };

    /**
     * @brief 右键视图模板实现
     */
    template<bool IsConst>
    class RightViewImpl
    {
        friend class Bimap;

        using BimapRef = std::conditional_t<IsConst, const Bimap&, Bimap&>;
        BimapRef m_bm;

        explicit RightViewImpl(BimapRef bm) noexcept
            : m_bm(bm)
        {
        }

    public:
        using iterator       = std::conditional_t<IsConst, typename RightMap::const_iterator,
                                                  typename RightMap::iterator>;
        using const_iterator = typename RightMap::const_iterator;

        [[nodiscard]] std::optional<Left> find(const Right& key) const noexcept
        {
            auto it = m_bm.m_r2l.find(key);
            return it != m_bm.m_r2l.end() ? std::optional<Left>{it->second} : std::nullopt;
        }

        [[nodiscard]] const Left& at(const Right& key) const
        {
            auto it = m_bm.m_r2l.find(key);
            if (it == m_bm.m_r2l.end())
                throw std::out_of_range("Bimap::right::at: key not found");
            return it->second;
        }

        [[nodiscard]] bool contains(const Right& key) const noexcept
        {
            return m_bm.m_r2l.count(key) > 0;
        }

        template<bool C = IsConst>
        std::enable_if_t<!C, bool> erase(const Right& key) noexcept
        {
            return m_bm.eraseRight(key);
        }

        template<bool C = IsConst>
        std::enable_if_t<!C, iterator> begin() noexcept
        {
            return m_bm.m_r2l.begin();
        }

        template<bool C = IsConst>
        std::enable_if_t<!C, iterator> end() noexcept
        {
            return m_bm.m_r2l.end();
        }

        const_iterator begin() const noexcept { return m_bm.m_r2l.cbegin(); }
        const_iterator end() const noexcept { return m_bm.m_r2l.cend(); }
        const_iterator cbegin() const noexcept { return m_bm.m_r2l.cbegin(); }
        const_iterator cend() const noexcept { return m_bm.m_r2l.cend(); }

        [[nodiscard]] size_type size() const noexcept { return m_bm.m_r2l.size(); }
        [[nodiscard]] bool empty() const noexcept { return m_bm.m_r2l.empty(); }
    };
};

} // namespace bakuon::gui::detail
