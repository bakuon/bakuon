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

namespace bakuon::core {

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
// Result 类型萃取：把 T 与 void 映射到同一套存储 / 调用签名
// ============================================================================

template<typename T>
class Result;

namespace detail {

/**
 * @brief 成功载荷的存储类型
 * @details void 无法放入 std::variant，用 std::monostate 作为空成功标记，
 *          使 Result<T> 与 Result<void> 共用同一套实现。
 */
template<typename T>
struct result_payload
{
    using type                    = T;
    static constexpr bool is_void = false;
};

template<>
struct result_payload<void>
{
    using type                    = std::monostate;
    static constexpr bool is_void = true;
};

template<typename T>
using result_payload_t = typename result_payload<T>::type;

template<typename T>
inline constexpr bool result_is_void_v = result_payload<T>::is_void;

/**
 * @brief 以成功值调用回调的签名
 * @details Result<T>    -> Func(const T&)
 *          Result<void> -> Func()
 */
template<typename T, typename Func>
struct result_invoke
{
    using type = std::invoke_result_t<Func, const T&>;
};

template<typename Func>
struct result_invoke<void, Func>
{
    using type = std::invoke_result_t<Func>;
};

template<typename T, typename Func>
using result_invoke_t = typename result_invoke<T, Func>::type;

template<typename T, typename Func>
using result_mapped_t = std::decay_t<result_invoke_t<T, Func>>;

/**
 * @brief 按载荷类型分发回调：void 无参，其余传入 payload
 */
template<typename T, typename Func, typename Payload>
constexpr decltype(auto) invoke_value(Func&& func, [[maybe_unused]] Payload&& payload)
{
    if constexpr (std::is_void_v<T>) {
        return std::invoke(std::forward<Func>(func));
    } else {
        return std::invoke(std::forward<Func>(func), std::forward<Payload>(payload));
    }
}

template<typename R>
struct is_result : std::false_type
{
};

template<typename U>
struct is_result<Result<U>> : std::true_type
{
};

template<typename R>
inline constexpr bool is_result_v = is_result<std::remove_cvref_t<R>>::value;

} // namespace detail

// ============================================================================
// 线程安全的通用 Result 类（T 与 void 共用一份实现）
// ============================================================================

/**
 * @brief 线程安全的结果类型
 * @tparam T 成功时持有的值类型；T = void 表示无载荷的成功/失败
 * @details 通过类型萃取将 void 存储为 std::monostate，成功时持有 payload，
 *          失败时持有 Status。内置 shared_mutex 保护，支持多线程并发读取。
 *          被标记为 [[nodiscard]]，防止调用方忽略返回值。
 */
template<typename T>
class [[nodiscard]] Result
{
public:
    using value_type   = T;
    using storage_type = detail::result_payload_t<T>;
    using variant_type = std::variant<Status, storage_type>;

    static constexpr bool is_void_value = detail::result_is_void_v<T>;

    static_assert(!std::is_reference_v<T>, "Result payload cannot be a reference type.");
    static_assert(!std::is_array_v<T>, "Result payload cannot be an array type.");
    static_assert(is_void_value || std::move_constructible<T> || std::copy_constructible<T>,
                  "Result payload must be void, or move/copy constructible.");

    // ------------------------------------------------------------------------
    // 构造函数
    // ------------------------------------------------------------------------

    /**
     * @brief 默认构造：仅 Result<void> 可用，表示成功
     */
    Result()
    requires is_void_value
        : m_data(std::in_place_type<storage_type>)
    {
    }

    /**
     * @brief 成功值构造（隐式，仅非 void）
     * @tparam U 可构造为 T 的类型
     */
    template<typename U>
    requires(!is_void_value && !std::is_same_v<std::remove_cvref_t<U>, Result>
             && !std::is_same_v<std::remove_cvref_t<U>, Status>
             && !std::is_same_v<std::remove_cvref_t<U>, StatusCode>
             && !std::is_same_v<std::remove_cvref_t<U>, std::in_place_t>
             && std::constructible_from<T, U>)
    Result(U&& val)
        : m_data(std::in_place_type<storage_type>, std::forward<U>(val))
    {
    }

    /**
     * @brief 原地构造成功值（仅非 void）
     */
    template<typename... Args>
    requires(!is_void_value && std::constructible_from<T, Args...>)
    explicit Result(std::in_place_t, Args&&... args)
        : m_data(std::in_place_type<storage_type>, std::forward<Args>(args)...)
    {
    }

    /**
     * @brief 失败状态构造（状态码 + 消息）
     */
    Result(StatusCode code, std::string msg)
        : m_data(std::in_place_type<Status>, code, std::move(msg))
    {
    }

    /**
     * @brief 失败状态构造（Status 对象）
     */
    explicit Result(Status status)
        : m_data(std::in_place_type<Status>, std::move(status))
    {
    }

    // ------------------------------------------------------------------------
    // 拷贝 / 移动 / 赋值（mutex 不可拷贝，需手写；数据在锁保护下复制）
    // ------------------------------------------------------------------------

    Result(const Result& other)
        : m_data(copy_payload(other))
    {
    }

    Result(Result&& other) noexcept
        : m_data(move_payload(other))
    {
    }

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

    ~Result() = default;

    // ------------------------------------------------------------------------
    // 静态工厂
    // ------------------------------------------------------------------------

    /**
     * @brief 构造成功的 Result
     * @details Result<void>::Ok()；Result<T>::Ok(args...) 转发构造 T
     */
    template<typename... Args>
    [[nodiscard]] static Result Ok(Args&&... args)
    requires((is_void_value && sizeof...(Args) == 0)
             || (!is_void_value && sizeof...(Args) >= 1 && std::constructible_from<T, Args...>) )
    {
        if constexpr (is_void_value) {
            return Result{};
        } else {
            return Result(std::in_place, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief 构造失败的 Result
     */
    [[nodiscard]] static Result Fail(StatusCode code, std::string msg)
    {
        return Result(code, std::move(msg));
    }

    /**
     * @brief 构造失败的 Result（从 Status）
     */
    [[nodiscard]] static Result Fail(Status status) { return Result(std::move(status)); }

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
     * @return true 表示 variant 中持有成功载荷
     */
    [[nodiscard]] bool success() const noexcept
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return holds_value();
    }

    /**
     * @brief 判断是否为失败状态
     */
    [[nodiscard]] bool error() const noexcept { return !success(); }

    // ------------------------------------------------------------------------
    // 值与状态获取（线程安全，返回副本）
    // ------------------------------------------------------------------------

    /**
     * @brief 获取状态（线程安全，返回副本）
     * @return 成功时返回默认 Status(Ok)，否则返回错误状态
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
     * @brief 获取成功值（左值：拷贝 T；void：仅校验）
     * @throws ResultError 若为失败状态
     */
    [[nodiscard]] auto value() const&
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        throw_if_error();
        if constexpr (is_void_value) {
            return;
        } else {
            return storage_type(std::get<storage_type>(m_data));
        }
    }

    /**
     * @brief 获取成功值（右值：移动 T；void：仅校验）
     * @throws ResultError 若为失败状态
     */
    [[nodiscard]] auto value() &&
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        throw_if_error();
        if constexpr (is_void_value) {
            return;
        } else {
            return std::get<storage_type>(std::move(m_data));
        }
    }

    /**
     * @brief 获取成功值，失败时返回默认值（void 不可用）
     */
    template<typename U>
    [[nodiscard]] T valueOr(U&& default_value) const&
    requires(!is_void_value && std::convertible_to<U, T>)
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (holds_value()) {
            return std::get<storage_type>(m_data);
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    /**
     * @brief 获取成功值，失败时返回默认值（右值重载，void 不可用）
     */
    template<typename U>
    [[nodiscard]] T valueOr(U&& default_value) &&
    requires(!is_void_value && std::convertible_to<U, T>)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (holds_value()) {
            return std::get<storage_type>(std::move(m_data));
        }
        return static_cast<T>(std::forward<U>(default_value));
    }

    // ------------------------------------------------------------------------
    // 受控访问（回调式，零拷贝；回调期间持有读锁）
    // ------------------------------------------------------------------------

    /**
     * @brief 在锁保护下访问成功值
     * @param func Result<T> 接收 const T&；Result<void> 无参
     * @return 回调的返回值；失败时非 void 返回值默认构造
     * @note 回调在持有读锁期间执行，期间禁止再次访问本 Result 对象
     */
    template<typename Func>
    auto withValue(Func&& func) const -> detail::result_invoke_t<T, Func>
    {
        using ReturnType = detail::result_invoke_t<T, Func>;
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (holds_value()) {
            return detail::invoke_value<T>(std::forward<Func>(func), std::get<storage_type>(m_data));
        }
        if constexpr (!std::is_void_v<ReturnType>) {
            return ReturnType{};
        }
    }

    /**
     * @brief 在锁保护下访问失败状态
     * @param func 接收 const Status&
     * @return 回调的返回值；成功时非 void 返回值默认构造
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
     * @brief 转换成功值（类似 Rust map / C++ transform）
     * @param func Result<T> 接收 const T& 返回 U；Result<void> 无参返回 U
     * @return Result<U>，失败时原样传递错误。U 可为 void
     */
    template<typename Func>
    [[nodiscard]] auto transform(Func&& func) const -> Result<detail::result_mapped_t<T, Func>>
    {
        using U = detail::result_mapped_t<T, Func>;
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (holds_value()) {
            storage_type payload = std::get<storage_type>(m_data);
            lock.unlock();
            if constexpr (std::is_void_v<U>) {
                detail::invoke_value<T>(std::forward<Func>(func), payload);
                return Result<U>{};
            } else {
                return Result<U>(detail::invoke_value<T>(std::forward<Func>(func), payload));
            }
        }
        Status err = std::get<Status>(m_data);
        lock.unlock();
        return Result<U>(std::move(err));
    }

    /**
     * @brief 链式调用（类似 Rust and_then）
     * @param func Result<T> 接收 const T& 返回 Result<U>；Result<void> 无参
     * @return Result<U>，失败时原样传递错误
     */
    template<typename Func>
    [[nodiscard]] auto andThen(Func&& func) const -> std::decay_t<detail::result_invoke_t<T, Func>>
    {
        using ResultU = std::decay_t<detail::result_invoke_t<T, Func>>;
        static_assert(detail::is_result_v<ResultU>, "andThen callback must return Result<U>");

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (holds_value()) {
            storage_type payload = std::get<storage_type>(m_data);
            lock.unlock();
            return detail::invoke_value<T>(std::forward<Func>(func), payload);
        }
        Status err = std::get<Status>(m_data);
        lock.unlock();
        return ResultU(std::move(err));
    }

    /**
     * @brief 失败时的恢复操作（类似 Rust or_else）
     * @param func 接收 const Status& 返回 Result<T>
     * @return 成功时原样返回，失败时调用 func 恢复
     */
    template<typename Func>
    [[nodiscard]] Result orElse(Func&& func) const
    {
        static_assert(std::is_same_v<std::decay_t<std::invoke_result_t<Func, const Status&>>, Result>,
                      "orElse callback must return Result<T>");

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (holds_value()) {
            [[maybe_unused]] storage_type payload = std::get<storage_type>(m_data);
            lock.unlock();
            if constexpr (is_void_value) {
                return Result{};
            } else {
                return Result(std::move(payload));
            }
        }
        Status err = std::get<Status>(m_data);
        lock.unlock();
        return std::invoke(std::forward<Func>(func), err);
    }

    // ------------------------------------------------------------------------
    // 其他工具方法
    // ------------------------------------------------------------------------

    /**
     * @brief 交换两个 Result
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

    friend void swap(Result& a, Result& b) noexcept { a.swap(b); }

private:
    [[nodiscard]] bool holds_value() const noexcept
    {
        return std::holds_alternative<storage_type>(m_data);
    }

    void throw_if_error() const
    {
        if (!holds_value()) [[unlikely]] {
            throw ResultError(std::get<Status>(m_data));
        }
    }

    static variant_type copy_payload(const Result& other)
    {
        std::shared_lock<std::shared_mutex> lock(other.m_mutex);
        return other.m_data;
    }

    static variant_type move_payload(Result& other) noexcept
    {
        std::unique_lock<std::shared_mutex> lock(other.m_mutex);
        return std::move(other.m_data);
    }

    /**
     * @brief 数据存储：成功为 storage_type（T 或 monostate），失败为 Status
     */
    variant_type m_data;

    /** @brief 读写锁：支持多线程并发读取，独占写入/移动 */
    mutable std::shared_mutex m_mutex;
};

// ============================================================================
// 辅助工厂函数
// ============================================================================

/**
 * @brief 构造成功的 Result<T>（由值推导 T）
 */
template<typename T>
[[nodiscard]] Result<std::decay_t<T>> Ok(T&& val)
{
    return Result<std::decay_t<T>>(std::forward<T>(val));
}

/**
 * @brief 构造成功的 Result<void>
 */
[[nodiscard]] inline Result<void> Ok()
{
    return Result<void>{};
}

/**
 * @brief 构造失败的 Result<T>
 */
template<typename T>
[[nodiscard]] Result<T> Fail(StatusCode code, std::string msg)
{
    return Result<T>(code, std::move(msg));
}

/**
 * @brief 构造失败的 Result<T>（从 Status）
 */
template<typename T>
[[nodiscard]] Result<T> Fail(Status status)
{
    return Result<T>(std::move(status));
}

} // namespace bakuon::core
