#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace bakuon::gui {

// ============================================================================
// 生产级 Stacktrace 捕获器（可替换实现）
// ============================================================================
namespace dbg {
/**
 * @brief 捕获当前线程的调用堆栈
 * @return 格式化后的堆栈信息字符串
 * @note 生产环境建议替换为 boost::stacktrace 或 cpptrace
 */
inline std::string capture_stacktrace()
{
    // 生产环境实际替换为：return boost::stacktrace::to_string(boost::stacktrace::stacktrace());
    // 或者 cpptrace 或者在支持 C++23 的编译器中切换为 std::stacktrace
    return "  [0] find_user_by_id(uint64_t) at user_service.cpp:42\n"
           "  [1] process_request(uint64_t) at controller.cpp:18\n"
           "  [2] main at main.cpp:10";
}
} // namespace dbg

// ============================================================================
// 状态码定义
// ============================================================================

/** @brief 业务状态码枚举 */
enum class StatusCode : uint8_t {
    Ok                 = 0,  ///< 成功
    Cancelled          = 1,  ///< 操作被取消
    InvalidArgument    = 2,  ///< 无效参数
    DeadlineExceeded   = 3,  ///< 超过截止时间
    NotFound           = 4,  ///< 资源未找到
    Timeout            = 5,  ///< 超时
    Aborted            = 6,  ///< 操作中止
    AlreadyExists      = 7,  ///< 资源已存在
    Unauthenticated    = 8,  ///< 未认证
    PermissionDenied   = 9,  ///< 无权限
    ResourceExhausted  = 10, ///< 资源耗尽
    FailedPrecondition = 11, ///< 前置条件不满足
    Unimplemented      = 12, ///< 未实现
    Unavailable        = 13, ///< 服务不可用
    DataLoss           = 14, ///< 数据丢失
    OutOfRange         = 15, ///< 越界
    InternalError      = 16, ///< 内部错误
    Unknown            = 20  ///< 未知错误
};

/**
 * @brief 将状态码转换为字符串视图（零拷贝）
 * @param code 状态码
 * @return 对应状态码的大写字符串表示
 */
[[nodiscard]] constexpr std::string_view codeStringView(StatusCode code) noexcept
{
    switch (code) {
    case StatusCode::Ok                : return "OK";
    case StatusCode::Cancelled         : return "CANCELLED";
    case StatusCode::InvalidArgument   : return "INVALID_ARGUMENT";
    case StatusCode::DeadlineExceeded  : return "DEADLINE_EXCEEDED";
    case StatusCode::NotFound          : return "NOT_FOUND";
    case StatusCode::Timeout           : return "TIMEOUT";
    case StatusCode::Aborted           : return "ABORTED";
    case StatusCode::AlreadyExists     : return "ALREADY_EXISTS";
    case StatusCode::Unauthenticated   : return "UNAUTHENTICATED";
    case StatusCode::PermissionDenied  : return "PERMISSION_DENIED";
    case StatusCode::ResourceExhausted : return "RESOURCE_EXHAUSTED";
    case StatusCode::FailedPrecondition: return "FAILED_PRECONDITION";
    case StatusCode::Unimplemented     : return "UNIMPLEMENTED";
    case StatusCode::Unavailable       : return "UNAVAILABLE";
    case StatusCode::DataLoss          : return "DATA_LOSS";
    case StatusCode::OutOfRange        : return "OUT_OF_RANGE";
    case StatusCode::InternalError     : return "INTERNAL_ERROR";
    case StatusCode::Unknown           : return "UNKNOWN";
    default                            : return "";
    }
}

/**
 * @brief 将状态码转换为 std::string
 * @param code 状态码
 * @return 对应状态码的字符串副本
 */
[[nodiscard]] inline std::string codeString(StatusCode code)
{
    return std::string(codeStringView(code));
}

/**
 * @brief 状态码流输出运算符
 * @param os 输出流
 * @param code 状态码
 * @return 输出流引用
 */
inline std::ostream& operator<<(std::ostream& os, StatusCode code)
{
    return os << static_cast<int>(code) << ':' << codeString(code);
}

// ============================================================================
// 增强版 Status：自动捕获堆栈并提供格式化输出
// ============================================================================

/**
 * @brief 状态描述结构体
 * @details 包含状态码、错误消息和堆栈跟踪。失败状态构造时自动捕获堆栈，
 *          成功状态不捕获以节约性能。
 */
struct Status
{
    StatusCode code{StatusCode::Ok}; /** 状态码 */
    std::string message{};           /** 错误消息 */
    std::string stacktrace{};        /** 堆栈跟踪（仅失败状态有值） */

    /** @brief 默认构造：成功状态 */
    Status() = default;

    /**
     * @brief 失败状态构造
     * @param c 状态码
     * @param msg 错误消息
     * @note 当 code != Ok 时自动捕获当前线程堆栈
     */
    Status(StatusCode c, std::string msg)
        : code(c)
        , message(std::move(msg))
    {
        if (code != StatusCode::Ok) [[unlikely]] {
            stacktrace = dbg::capture_stacktrace();
        }
    }

    /**
     * @brief 判断是否为成功状态
     * @return true 表示成功
     */
    [[nodiscard]] bool ok() const noexcept { return code == StatusCode::Ok; }

    /** 
     * @brief 流输出运算符（供日志框架使用）
     */
    friend std::ostream& operator<<(std::ostream& os, const Status& status)
    {
        if (status.ok()) {
            os << "Status: Ok";
        } else {
            os << "Status Error [" << status.code << "]: " << status.message << "\n"
               << "--- Error Stacktrace ---\n"
               << status.stacktrace;
        }
        return os;
    }
};

/**
 * @brief Result 抛出的异常类型
 * @details 当对失败的 Result 调用 value() 时抛出，携带完整的 Status 信息
 */
class ResultError : public std::runtime_error
{
public:
    explicit ResultError(Status status)
        : std::runtime_error(status.message)
        , m_status(std::move(status))
    {
    }

    /** 
     * @brief 获取关联的状态对象
     */
    [[nodiscard]] const Status& status() const noexcept { return m_status; }

private:
    Status m_status;
};

// ============================================================================
// 线程安全的通用 Result 类
// ============================================================================
// 前向声明
template<typename T>
class Result;

/**
 * @brief 线程安全的结果类型
 * @tparam T 成功时持有的值类型
 * @details 使用 std::variant 复用内存，成功时持有 T，失败时持有 Status。
 *          内置 shared_mutex 保护，支持多线程并发读取。
 *          被标记为 [[nodiscard]]，防止调用方忽略返回值。
 */
template<typename T>
class [[nodiscard]] Result
{
public:
    // ------------------------------------------------------------------------
    // 类型约束
    // ------------------------------------------------------------------------
    static_assert(std::move_constructible<T> || std::copy_constructible<T>,
                  "Result payload must be move or copy constructible.");

    /** 
     * @brief 值类型
     */
    using value_type = T;

    // ------------------------------------------------------------------------
    // 构造函数群
    // ------------------------------------------------------------------------

    /**
     * @brief 成功值移动构造
     * @param val 成功值（右值）
     */
    Result(T&& val)
        : m_data(std::move(val))
    {
    }

    /**
     * @brief 成功值拷贝构造
     * @param val 成功值（左值）
     */
    Result(const T& val)
        : m_data(val)
    {
    }

    /**
     * @brief 原地构造成功值
     * @tparam Args 构造 T 的参数类型
     * @param args 构造 T 的参数
     */
    template<typename... Args>
    requires std::constructible_from<T, Args...>
    explicit Result(std::in_place_t, Args&&... args)
        : m_data(std::in_place_type<T>, std::forward<Args>(args)...)
    {
    }

    /**
     * @brief 失败状态构造（状态码 + 消息）
     * @param code 错误码
     * @param msg 错误消息
     */
    Result(StatusCode code, std::string msg)
        : m_data(std::in_place_type<Status>, code, std::move(msg))
    {
    }

    /**
     * @brief 失败状态构造（Status 对象）
     * @param status 状态对象
     */
    explicit Result(Status status)
        : m_data(std::in_place_type<Status>, std::move(status))
    {
    }

    // ------------------------------------------------------------------------
    // 拷贝 / 移动构造与赋值（遵循 Rule of Five）
    // ------------------------------------------------------------------------

    /**
     * @brief 拷贝构造函数
     * @param other 源对象
     * @note 构造时本对象无并发访问，只需对源对象加读锁
     */
    Result(const Result& other)
    {
        std::shared_lock<std::shared_mutex> lock(other.m_mutex);
        m_data = other.m_data;
    }

    /**
     * @brief 移动构造函数
     * @param other 源对象（将被移入本对象）
     * @note 对源对象加写锁，确保移动操作的原子性
     */
    Result(Result&& other) noexcept
    {
        std::unique_lock<std::shared_mutex> lock(other.m_mutex);
        m_data = std::move(other.m_data);
    }

    /**
     * @brief 拷贝赋值运算符
     * @param other 源对象
     * @return *this
     * @note 采用 copy-and-swap 思想，使用 std::lock 同时获取两把锁避免死锁
     */
    Result& operator=(const Result& other)
    {
        if (this != &other) {
            std::unique_lock<std::shared_mutex> lock_this(m_mutex, std::defer_lock);
            std::shared_lock<std::shared_mutex> lock_other(other.m_mutex, std::defer_lock);
            std::lock(lock_this, lock_other);
            m_data = other.m_data;
        }
        return *this;
    }

    /**
     * @brief 移动赋值运算符
     * @param other 源对象（将被移入本对象）
     * @return *this
     * @note 两个对象都加写锁，确保移动操作的原子性
     */
    Result& operator=(Result&& other) noexcept
    {
        if (this != &other) {
            std::unique_lock<std::shared_mutex> lock_this(m_mutex, std::defer_lock);
            std::unique_lock<std::shared_mutex> lock_other(other.m_mutex, std::defer_lock);
            std::lock(lock_this, lock_other);
            m_data = std::move(other.m_data);
        }
        return *this;
    }

    /** @brief 析构函数 */
    ~Result() = default;

    // ------------------------------------------------------------------------
    // 静态辅助工厂方法
    // ------------------------------------------------------------------------

    /**
     * @brief 构造成功的 Result<T>
     * @param val 成功值
     * @return 持有 val 的 Result<T>
     */
    [[nodiscard]] static Result<std::decay_t<T>> Ok(T&& val)
    {
        return Result<std::decay_t<T>>(std::forward<T>(val));
    }

    /**
     * @brief 构造成功的 Result<T>
     * @param val 成功值
     * @return 持有 val 的 Result<T>
     */
    [[nodiscard]] static Result<std::decay_t<T>> Ok(const T& val)
    {
        return Result<std::decay_t<T>>(val);
    }

    /**
    * @brief 构造失败的 Result<T>
    * @param code 错误码
    * @param msg 错误消息
    * @return 失败状态的 Result<T>
    */
    [[nodiscard]] static Result<T> Fail(StatusCode code, std::string msg)
    {
        return Result<T>(code, std::move(msg));
    }

    /**
    * @brief 构造失败的 Result<T>（从 Status 构造）
    * @param status 状态对象
    * @return 失败状态的 Result<T>
    */
    [[nodiscard]] static Result<T> Fail(Status status) { return Result<T>(std::move(status)); }

    // ------------------------------------------------------------------------
    // 状态查询
    // ------------------------------------------------------------------------

    /**
     * @brief 显式布尔转换
     * @return true 表示成功
     */
    explicit operator bool() const noexcept { return success(); }

    /**
     * @brief 判断是否为成功状态
     * @return true 表示 variant 中持有 T
     */
    [[nodiscard]] bool success() const noexcept
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return std::holds_alternative<T>(m_data);
    }

    /**
     * @brief 判断是否为失败状态（success 的反义，提高可读性）
     * @return true 表示失败
     */
    [[nodiscard]] bool error() const noexcept { return !success(); }

    // ------------------------------------------------------------------------
    // 值与状态获取（线程安全，返回副本）
    // ------------------------------------------------------------------------

    /**
     * @brief 获取状态（线程安全，返回副本）
     * @return 若成功返回默认 Status(Ok)，否则返回错误状态
     */
    [[nodiscard]] Status status() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (std::holds_alternative<Status>(m_data)) {
            return std::get<Status>(m_data);
        }
        return Status{};
    }

    /**
     * @brief 获取成功值（左值版本，返回拷贝）
     * @return 成功值的拷贝
     * @throws ResultError 若为失败状态
     */
    [[nodiscard]] T value() const&
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (!std::holds_alternative<T>(m_data)) [[unlikely]] {
            throw ResultError(std::get<Status>(m_data));
        }
        return std::get<T>(m_data);
    }

    /**
     * @brief 获取成功值（右值版本，移动返回）
     * @return 成功值（通过移动转出）
     * @throws ResultError 若为失败状态
     * @note 对本对象加写锁确保移动时无并发读取
     */
    [[nodiscard]] T value() &&
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (!std::holds_alternative<T>(m_data)) [[unlikely]] {
            throw ResultError(std::get<Status>(m_data));
        }
        return std::get<T>(std::move(m_data));
    }

    /**
     * @brief 获取成功值，失败时返回默认值
     * @tparam U 默认值类型（可转换为 T）
     * @param default_value 失败时的默认值
     * @return 成功值或默认值
     */
    template<typename U>
    [[nodiscard]] T valueOr(U&& default_value) const&
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (std::holds_alternative<T>(m_data)) {
            return std::get<T>(m_data);
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    /** @brief 获取成功值，失败时返回默认值（右值重载） */
    template<typename U>
    [[nodiscard]] T valueOr(U&& default_value) &&
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (std::holds_alternative<T>(m_data)) {
            return std::get<T>(std::move(m_data));
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    // ------------------------------------------------------------------------
    // 受控访问（回调式，零拷贝）
    // ------------------------------------------------------------------------

    /**
     * @brief 在锁保护下访问成功值
     * @tparam Func 回调类型
     * @param func 回调函数，接收 const T& 作为参数
     * @return 回调的返回值，失败时返回默认构造值
     * @note 回调在持有读锁期间执行，期间禁止再次访问本 Result 对象
     */
    template<typename Func>
    auto withValue(Func&& func) const -> std::invoke_result_t<Func, const T&>
    {
        using ReturnType = std::invoke_result_t<Func, const T&>;
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (std::holds_alternative<T>(m_data)) {
            return std::invoke(std::forward<Func>(func), std::get<T>(m_data));
        }
        if constexpr (!std::is_void_v<ReturnType>) {
            return ReturnType{};
        }
    }

    /**
     * @brief 在锁保护下访问失败状态
     * @tparam Func 回调类型
     * @param func 回调函数，接收 const Status& 作为参数
     * @return 回调的返回值，成功时返回默认构造值
     */
    template<typename Func>
    auto withError(Func&& func) const -> std::invoke_result_t<Func, const Status&>
    {
        using ReturnType = std::invoke_result_t<Func, const Status&>;
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (std::holds_alternative<Status>(m_data)) {
            return std::invoke(std::forward<Func>(func), std::get<Status>(m_data));
        }
        if constexpr (!std::is_void_v<ReturnType>) {
            return ReturnType{};
        }
    }

    // ------------------------------------------------------------------------
    // 函数式链式调用（Monadic 操作）
    // ------------------------------------------------------------------------

    /**
     * @brief 转换成功值（类似 Rust 的 map / transform）
     * @tparam Func 转换函数类型
     * @param func 转换函数，接收 const T& 返回 U
     * @return Result<U>，失败时原样传递错误
     */
    template<typename Func>
    [[nodiscard]] auto transform(Func&& func) const
        -> Result<std::decay_t<std::invoke_result_t<Func, const T&>>>
    {
        using U = std::decay_t<std::invoke_result_t<Func, const T&>>;
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (std::holds_alternative<T>(m_data)) {
            return Result<U>(std::invoke(std::forward<Func>(func), std::get<T>(m_data)));
        }
        return Result<U>(std::get<Status>(m_data));
    }

    /**
     * @brief 链式调用（类似 Rust 的 and_then）
     * @tparam Func 转换函数类型
     * @param func 接收 const T& 返回 Result<U>
     * @return Result<U>，失败时原样传递错误
     */
    template<typename Func>
    [[nodiscard]] auto andThen(Func&& func) const
        -> std::decay_t<std::invoke_result_t<Func, const T&>>
    {
        using ResultU = std::decay_t<std::invoke_result_t<Func, const T&>>;
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (std::holds_alternative<T>(m_data)) {
            return std::invoke(std::forward<Func>(func), std::get<T>(m_data));
        }
        return ResultU(std::get<Status>(m_data));
    }

    /**
     * @brief 失败时的恢复操作（类似 Rust 的 or_else）
     * @tparam Func 恢复函数类型
     * @param func 接收 const Status& 返回 Result<T>
     * @return Result<T>，成功时原样返回，失败时调用 func 恢复
     */
    template<typename Func>
    [[nodiscard]] Result orElse(Func&& func) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (std::holds_alternative<T>(m_data)) {
            return *this;
        }
        return std::invoke(std::forward<Func>(func), std::get<Status>(m_data));
    }

    // ------------------------------------------------------------------------
    // 其他工具方法
    // ------------------------------------------------------------------------

    /**
     * @brief 交换两个 Result
     * @param other 另一个 Result 对象
     */
    void swap(Result& other) noexcept
    {
        if (this != &other) {
            std::unique_lock<std::shared_mutex> lock_this(m_mutex, std::defer_lock);
            std::unique_lock<std::shared_mutex> lock_other(other.m_mutex, std::defer_lock);
            std::lock(lock_this, lock_other);
            using std::swap;
            swap(m_data, other.m_data);
        }
    }

    /** @brief 全局 swap 重载 */
    friend void swap(Result& a, Result& b) noexcept { a.swap(b); }

private:
    /**
     * @brief 数据存储：成功时为 T，失败时为 Status
     * @note 顺序不重要，holds_alternative 和 get 都可正常工作
     */
    std::variant<Status, T> m_data;

    /** @brief 读写锁：支持多线程并发读取，独占写入/移动 */
    mutable std::shared_mutex m_mutex;
};

// ============================================================================
// Result<void> 特化（无返回值场景）
// ============================================================================

/**
 * @brief void 类型的 Result 特化
 * @details 用于无返回值的函数，仍然携带成功/失败状态信息
 */
template<>
class [[nodiscard]] Result<void>
{
public:
    /**
     * @brief 值类型
     */
    using value_type = void;

    /**
     * @brief 默认构造：成功状态
     */
    Result() = default;

    /**
     * @brief 失败状态构造
     * @param code 错误码
     * @param msg 错误消息
     */
    Result(StatusCode code, std::string msg)
        : m_status(code, std::move(msg))
    {
    }

    /**
     * @brief 失败状态构造（Status 对象）
     * @param status 状态对象
     */
    explicit Result(Status status)
        : m_status(std::move(status))
    {
    }

    /** @brief 显式布尔转换 */
    explicit operator bool() const noexcept { return success(); }

    /** @brief 判断是否成功 */
    [[nodiscard]] bool success() const noexcept { return m_status.ok(); }

    /** @brief 判断是否失败 */
    [[nodiscard]] bool error() const noexcept { return !m_status.ok(); }

    /** @brief 获取状态 */
    [[nodiscard]] const Status& status() const noexcept { return m_status; }

    /** @brief 无操作（用于统一接口，void 无值可获取） */
    void value() const
    {
        if (!m_status.ok()) [[unlikely]] {
            throw ResultError(m_status);
        }
    }

    /**
     * @brief 失败时的恢复操作
     * @tparam Func 恢复函数类型
     * @param func 接收 const Status& 返回 Result<void>
     * @return 成功时原样返回，失败时调用 func 恢复
     */
    template<typename Func>
    [[nodiscard]] Result orElse(Func&& func) const
    {
        if (m_status.ok()) {
            return *this;
        }
        return std::invoke(std::forward<Func>(func), m_status);
    }

    /**
     * @brief 链式调用（void 版本）
     * @tparam Func 函数类型
     * @param func 无参数，返回 Result<void>
     * @return 成功时继续执行 func，失败时传递错误
     */
    template<typename Func>
    [[nodiscard]] Result andThen(Func&& func) const
    {
        if (m_status.ok()) {
            return std::invoke(std::forward<Func>(func));
        }
        return *this;
    }

    // ------------------------------------------------------------------------
    // 静态辅助工厂方法
    // ------------------------------------------------------------------------

    /**
     * @brief 创建成功的 void Result
     * @return 成功状态的 Result<void>
     */
    [[nodiscard]] static Result OK() { return {}; }

    /**
    * @brief 构造失败的 Result<void>
    * @param code 错误码
    * @param msg 错误消息
    * @return 失败状态的 Result<void>
    */
    [[nodiscard]] static Result<void> Fail(StatusCode code, std::string msg)
    {
        return {code, std::move(msg)};
    }

    /**
    * @brief 构造失败的 Result<void>（从 Status 构造）
    * @param status 状态对象
    * @return 失败状态的 Result<void>
    */
    [[nodiscard]] static Result<void> Fail(Status status)
    {
        return Result<void>(std::move(status));
    }

private:
    Status m_status; /** 状态对象 */
};

// ============================================================================
// 辅助工厂函数 ---- 注：类自身也提供了工厂成员函数
// ============================================================================

/**
 * @brief 构造成功的 Result<T>
 * @tparam T 值类型（自动推导）
 * @param val 成功值
 * @return 持有 val 的 Result<T>
 */
template<typename T>
[[nodiscard]] Result<std::decay_t<T>> Ok(T&& val)
{
    return Result<std::decay_t<T>>(std::forward<T>(val));
}

/**
 * @brief 构造成功的 Result<void>
 * @return 成功状态的 Result<void>
 */
[[nodiscard]] inline Result<void> Ok()
{
    return Result<void>{};
}

/**
 * @brief 构造失败的 Result<T>
 * @tparam T 值类型（通常由调用方显式指定或推导）
 * @param code 错误码
 * @param msg 错误消息
 * @return 失败状态的 Result<T>
 */
template<typename T>
[[nodiscard]] Result<T> Fail(StatusCode code, std::string msg)
{
    return Result<T>(code, std::move(msg));
}

/**
 * @brief 构造失败的 Result<T>（从 Status 构造）
 * @tparam T 值类型
 * @param status 状态对象
 * @return 失败状态的 Result<T>
 */
template<typename T>
[[nodiscard]] Result<T> Fail(Status status)
{
    return Result<T>(std::move(status));
}

} // namespace bakuon::gui
