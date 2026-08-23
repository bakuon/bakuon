#pragma once

#include <any>
#include <functional>
#include <optional>
#include <string_view>

namespace bakuon::gui {

enum class StatusCode : int {
    Ok                 = 0,
    Cancelled          = 1,
    InvalidArgument    = 2,
    DeadlineExceeded   = 3,
    NotFound           = 4,
    AlreadyExists      = 5,
    PermissionDenied   = 6,
    ResourceExhausted  = 7,
    FailedPrecondition = 8,
    Aborted            = 9,
    OutOfRange         = 10,
    Unimplemented      = 11,
    Internal           = 12,
    Unavailable        = 13,
    DataLoss           = 14,
    Unauthenticated    = 15,
    Unknown            = 20
};

/**
 * @brief 通常用于在 API 边界之间优雅地处理错误，其中一些错误可能是可恢复的，而另一些则可能无法恢复。
 * @note 起草类，要做到通用需考虑诸多方面，完成后可移动到 core 下作为通用类（纯标准 C++）。
 */
class Status final
{
public:
    // This default constructor creates an OK status with no message or payload.
    Status();

    // Creates a status in the canonical error space with the specified
    // code, and an empty error message.
    explicit Status(StatusCode code);

    // Creates a status in the canonical error space with the specified
    // `StatusCode` and error message.  If `code == StatusCode::Ok`,
    // `msg` is ignored and an object identical to an OK status is constructed.
    Status(StatusCode code, std::string_view message);

    template<typename String>
    Status(StatusCode code, String&& message);

    explicit Status(uintptr_t rep)
        : m_rep(rep)
    {
    }

    Status(const Status&);
    Status& operator=(const Status& o);
    ~Status();

    bool ok() const;
    StatusCode code() const;
    std::string_view message() const;

    std::optional<std::any> payload(std::string_view type) const;
    // NOTE: This function does nothing if the Status is ok.
    void setPayload(std::string_view type_url, std::any payload);

    void eachPayload(std::function<void(std::string_view, const std::any&)> visitor) const;

    void update(const Status& new_status);
    void update(Status&& new_status);

    friend void swap(Status& a, Status& b) noexcept;

    friend bool operator==(const Status&, const Status&);
    friend bool operator!=(const Status&, const Status&);

private:
    uintptr_t m_rep; // supports representations.
};

} // namespace bakuon::gui
