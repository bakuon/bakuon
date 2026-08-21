#pragma once

#include <istream>
#include <memory>
#include <ostream>
#include <type_traits>

#include <QDataStream>

namespace bakuon::gui::detail {

/// 指针

template<typename T>
struct is_shared_ptr : std::false_type
{
};

template<typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type
{
};

template<typename T>
inline constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

template<typename T>
struct is_unique_ptr : std::false_type
{
};

template<typename T, typename D>
struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type
{
};

template<typename T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

template<typename T>
struct is_pointer : std::is_pointer<T>
{
};

template<typename T>
inline constexpr bool is_pointer_v = std::is_pointer_v<T>;

/// IO 流

template<typename T, typename = void>
struct is_iostreamable : std::false_type
{
};

template<typename T>
struct is_iostreamable<
    T, std::void_t<decltype(std::declval<std::ostream &>() << std::declval<const T &>()),
                   decltype(std::declval<std::istream &>() >> std::declval<T &>())>>
    : std::true_type
{
};

template<typename T>
inline constexpr bool is_iostreamable_v = is_iostreamable<T>::value;

// 检测 T 是否支持 << / >> 流操作
template<typename T, typename = void>
struct is_streamable : std::false_type
{
};

template<typename T>
struct is_streamable<
    T, std::void_t<decltype(std::declval<QDataStream &>() << std::declval<const T &>()),
                   decltype(std::declval<QDataStream &>() >> std::declval<T &>())>> : std::true_type
{
};

template<typename T>
inline constexpr bool is_streamable_v = is_streamable<T>::value;

/// 容器

template<typename T, typename = void>
struct is_container : std::false_type
{
};

template<typename T>
struct is_container<
    T, std::void_t<decltype(std::declval<T>().begin()), decltype(std::declval<T>().end()),
                   decltype(std::declval<T>().size()), decltype(std::declval<T>().data())>>
    : std::true_type
{
};

template<typename T>
inline constexpr bool is_container_v = is_container<T>::value;

} // namespace bakuon::gui::detail
