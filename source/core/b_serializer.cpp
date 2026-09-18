#include "core/b_serializer.h"

namespace bakuon::core::serializer_detail {

std::string describeCountMismatch(std::size_t expected, std::uint32_t actual)
{
    return "归档里的组件类型数量(" + std::to_string(actual)
           + ")与 Serializer<Components...> 声明的数量(" + std::to_string(expected)
           + ")不匹配——多半是 save() 和 load() 两端用了不同的模板参数列表";
}

std::string describeNameMismatch(std::string_view expected, std::string_view actual)
{
    return "归档中某个位置的组件类型名为 '" + std::string(actual) + "'，与期望的 '"
           + std::string(expected)
           + "' 不一致。Serializer<Components...> 按模板参数声明顺序线性读写（不支持像旧版 "
             "DocumentSerializer 那样按名字随机访问），save()/load() 两端必须使用完全相同顺序的 "
             "Components... 列表";
}

} // namespace bakuon::core::serializer_detail
