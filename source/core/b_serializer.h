#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <entt/entity/mixin.hpp>
#include <entt/entity/snapshot.hpp>
#include <nlohmann/json.hpp>

#include "core/b_registry.h"
#include "core/b_result.h"

namespace bakuon::core {

/**
 * @brief 组件类型 -> 落盘时使用的稳定字符串键。
 *
 * @details DocumentSerializer 需要给每个 Components... 类型分配一个 JSON 里的
 * 键名，不能直接用 `typeid(T).name()`——那是编译器相关的、可能被 mangled 过的
 * 字符串，GCC/Clang/MSVC 三个工具链（见项目支持的编译环境）给出的结果互不相同，
 * 直接拿来当持久化格式的字段名会导致同一份文档换个编译器构建出来的程序就读不出来。
 *
 * 用法与 IExtensionPoint.h 里的 extension_iid<T> + BAKUON_DECLARE_EXTENSION_IID
 * 完全同源的设计：
 * @code
 *   struct Position { float x, y; };
 *   BAKUON_DECLARE_COMPONENT_NAME(Position, "Position")
 * @endcode
 */
template<typename T>
struct component_name
{
    static constexpr std::string_view value() noexcept { return {}; }
};

#define BAKUON_DECLARE_COMPONENT_NAME(Type, Name) \
    template<> \
    struct bakuon::core::component_name<Type> \
    { \
        static constexpr std::string_view value() noexcept { return Name; } \
    };

namespace serialize_detail {

/**
 * @brief entt::snapshot/entt::snapshot_loader 的 JSON 归档器：把归档过程中
 * 调用方逐个传进来的值（实体句柄本身、或者具体组件类型）依次追加/读回一个
 * nlohmann::json 数组。
 *
 * @warning 要求每个参与序列化的 Components... 类型本身已经具备 nlohmann::json
 * 认识的 to_json()/from_json()（ADL 自由函数，或者用 NLOHMANN_DEFINE_TYPE_*
 * 系列宏在类型定义里生成）——这是 nlohmann::json 库本身的标准用法，
 * DocumentSerializer 不做任何额外的反射/代码生成，组件类型自己负责这一层。
 */
struct JsonWriter
{
    nlohmann::json array = nlohmann::json::array();

    template<typename T>
    void operator()(const T& value)
    {
        array.push_back(value);
    }
};

struct JsonReader
{
    const nlohmann::json& array;
    std::size_t index = 0;

    template<typename T>
    void operator()(T& value)
    {
        value = array.at(index).template get<T>();
        ++index;
    }
};

} // namespace serialize_detail

/**
 * @brief 基于 entt::snapshot/entt::snapshot_loader 的 JSON 文档序列化器。
 *
 * @tparam Components 参与序列化的组件类型（至少一个），每个类型都必须：
 *   1. 通过 BAKUON_DECLARE_COMPONENT_NAME 声明一个稳定的字符串键；
 *   2. 具备 nlohmann::json 认识的 to_json()/from_json()（见 JsonWriter 的说明）。
 *
 * ## 与 UndoStack 的分工
 * UndoStack（b_undostack.h）同样基于 entt::snapshot/snapshot_loader，但用的是
 * 逐字节内存拷贝的二进制归档器，只支持"可平凡拷贝"的组件、只用于进程内的
 * 撤销/重做历史，从不落盘。DocumentSerializer 换成 JSON 归档器：牺牲一些性能
 * 和体积，换来（a）人类可读、可跨进程/跨版本迁移的落盘格式；（b）支持
 * std::string 等非平凡可拷贝的字段（只要组件类型自己实现了 to_json/from_json）。
 * 两者都不重复发明"怎么遍历整个 Registry 的实体和组件"这件事——那正是
 * entt::snapshot 已经做好、经过 entt 自身测试覆盖的部分。
 *
 * ## 已知限制（v1，刻意不做）
 * 不处理"文档里缺少某个 Components 类型"这类模式演进场景（比如老文档没有
 * 某个新增的组件字段）——load() 遇到缺失字段会失败并返回 Fail<void>，
 * 不会静默跳过。前向/后向兼容的 schema 演进留给未来按实际需要再设计。
 *
 * @code
 *   struct Position { float x, y; };
 *   void to_json(nlohmann::json& j, const Position& p) { j = {{"x", p.x}, {"y", p.y}}; }
 *   void from_json(const nlohmann::json& j, Position& p) { j.at("x").get_to(p.x); j.at("y").get_to(p.y); }
 *   BAKUON_DECLARE_COMPONENT_NAME(Position, "Position")
 *
 *   bakuon::core::Registry registry;
 *   bakuon::core::DocumentSerializer<Position> serializer(registry);
 *
 *   const nlohmann::json doc = serializer.save();
 *   // ... doc.dump() 写入文件 ...
 *
 *   if (auto result = serializer.load(doc); result.error()) {
 *       // result.status().message 里是失败原因
 *   }
 * @endcode
 */
template<typename... Components>
class DocumentSerializer
{
    static_assert(sizeof...(Components) > 0,
                  "DocumentSerializer 至少需要指定一个要序列化的组件类型");

public:
    explicit DocumentSerializer(Registry& registry)
        : m_registry(registry)
    {
    }

    /// 把 Registry 当前的完整实体集合与全部追踪组件序列化成一份 JSON 文档。
    [[nodiscard]] nlohmann::json save() const
    {
        nlohmann::json doc;
        doc["version"] = kFormatVersion;

        serialize_detail::JsonWriter entityWriter;
        entt::snapshot{m_registry.native()}.template get<entt::entity>(entityWriter);
        doc["entities"] = std::move(entityWriter.array);

        nlohmann::json componentsObj = nlohmann::json::object();
        (writeComponentInto<Components>(componentsObj), ...);
        doc["components"] = std::move(componentsObj);

        return doc;
    }

    /**
     * @brief 从 save() 产出的 JSON 文档整体恢复 Registry 的状态（覆盖当前内容）。
     * @return 成功返回 Ok()；文档格式不对（缺字段/字段类型不匹配/版本不认识）
     *         返回 Fail<void>(StatusCode::InvalidArgument, 具体原因)，此时
     *         Registry 的状态是未定义的部分恢复结果——调用方应当把这种失败
     *         当作"整份文档不可用"处理，不要假设失败后 Registry 还是干净的
     *         旧状态（毕竟 clear() 已经先发生了）。
     */
    [[nodiscard]] Result<void> load(const nlohmann::json& doc)
    {
        try {
            if (!doc.contains("version") || !doc.contains("entities")
                || !doc.contains("components")) {
                return Fail<void>(StatusCode::InvalidArgument,
                                  "文档缺少 version/entities/components 字段之一");
            }
            if (doc.at("version").get<int>() != kFormatVersion) {
                return Fail<void>(StatusCode::InvalidArgument,
                                  "文档格式版本不受支持（期望 " + std::to_string(kFormatVersion)
                                      + "）");
            }

            m_registry.native().clear();

            entt::snapshot_loader loader{m_registry.native()};
            serialize_detail::JsonReader entityReader{doc.at("entities")};
            loader.template get<entt::entity>(entityReader);

            const nlohmann::json& componentsObj = doc.at("components");
            (readComponentFrom<Components>(loader, componentsObj), ...);

            return Ok();
        } catch (const nlohmann::json::exception& e) {
            return Fail<void>(StatusCode::InvalidArgument, e.what());
        }
    }

private:
    template<typename T>
    [[nodiscard]] static constexpr std::string_view keyOf()
    {
        static_assert(!component_name<T>::value().empty(),
                      "DocumentSerializer 的每个 Components 类型都必须先用 "
                      "BAKUON_DECLARE_COMPONENT_NAME 声明一个非空的字符串键");
        return component_name<T>::value();
    }

    template<typename T>
    void writeComponentInto(nlohmann::json& componentsObj) const
    {
        serialize_detail::JsonWriter writer;
        entt::snapshot{m_registry.native()}.template get<T>(writer);
        componentsObj[std::string(keyOf<T>())] = std::move(writer.array);
    }

    template<typename T>
    void readComponentFrom(entt::snapshot_loader& loader, const nlohmann::json& componentsObj)
    {
        // componentsObj.at() 对缺失的键会自己抛出 nlohmann::json::out_of_range，
        // 附带清晰的错误信息；不需要我们手工构造异常，外层 load() 的
        // catch (const nlohmann::json::exception&) 会统一接住。
        serialize_detail::JsonReader reader{componentsObj.at(std::string(keyOf<T>()))};
        loader.template get<T>(reader);
    }

private:
    static constexpr int kFormatVersion = 1;

    Registry& m_registry;
};

} // namespace bakuon::core
