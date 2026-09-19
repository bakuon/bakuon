#pragma once

#include <memory>

#include "core/b_entity.h"
#include "core/b_identifier.h"
#include "core/b_mapper.h"
#include "core/b_stableid.h"

/// 复制粘贴是编辑器最基础的交互，且能验证 Reference 设计是否合理

/**
 * 克隆/深拷贝子树
 * 
 * entt 本身不提供"复制一个实体及其全部组件，生成一批新 StableId"的能力，
 * hierarchy:: 也只有摘除/挂接，没有"整棵子树连带组件一起复制"。
 * 复制/粘贴、预制体实例化都需要它，而且要和 Ref（b_reference.h）联动
 * ——克隆子树内部的相互引用要重定向到新 id，指向子树外部的引用要保持不变。
 */

/**
* @brief 关系一致性批处理克隆：复制一组相互关联的实体时，
* 会重新将指向“批内”位置的任何 StableId 引用重定向至新创
* 建的克隆对象，而指向“批外”位置的引用则继续指向原始共享对象。
*
* src == dst（相同的 Registry/StableIdRegistry）是常见情况
* ——即“在同一文档内重复选择”。跨注册表的构造也受到支持（例如，将
* 一个沙箱文档中的剪贴板批处理粘贴到另一个文档中），因为 src 和 dst 是独立的引用。
*/
namespace bakuon::core {

class Cloner
{
public:
    // TODO: 可尝试使用带状态消息的 core::Result<T>
    struct Result
    {
        Entity clonedEntity{nullentity};
        StableId sourceId{};
        StableId clonedid{};
    };

    struct BatchResult
    {
        std::vector<Result> clones;
        StableIdRemap remap;
    };

    Cloner()
        : m_mapper(std::make_unique<ComponentMapper>())
    {
    }

    Cloner(const Registry &src, const Identifier &srcId, Registry &dst, Identifier &dstId)
        : m_srcReg(&src)
        , m_srcId(&srcId)
        , m_dstReg(&dst)
        , m_dstId(&dstId)
        , m_mapper(std::make_unique<ComponentMapper>())
    {
    }

    void setSource(const Registry &src, const Identifier &srcId)
    {
        m_srcReg = &src;
        m_srcId  = &srcId;
    }

    void setDestination(Registry &dst, Identifier &dstId)
    {
        m_dstReg = &dst;
        m_dstId  = &dstId;
    }

    template<typename Component>
    void addComponent(std::string name)
    {
        m_mapper->addMapping<Component>(std::move(name));
    }

    template<typename Component>
    void removeComponent(std::string name)
    {
        m_mapper->removeMapping<Component>(std::move(name));
    }

    [[nodiscard]] std::size_t totalComponents() const noexcept { return m_mapper->size(); }

    [[nodiscard]] Result clone(Entity source) { return cloneEntity(source, true); }

    [[nodiscard]] Result clone(StableId source)
    {
        if (!m_srcReg || !m_srcId) {
            throw std::runtime_error("Cloner not initialized");
        }

        const auto sourceEntity = m_srcId->find(source).value_or({});
        if (!m_srcReg->valid(sourceEntity)) {
            throw StableIdError("clone source StableId does not resolve to a live entity");
        }

        return cloneEntity(sourceEntity, true);
    }

    [[nodiscard]] BatchResult cloneBatch(std::span<Entity> sources)
    {
        if (!m_srcReg || !m_srcId || !m_dstReg || !m_dstId) {
            throw std::runtime_error("Cloner not initialized");
        }

        BatchResult results;
        results.clones.reserve(sources.size());
        results.remap.reserve(sources.size());

        const auto skip = ComponentMapper::stableidType();

        for (auto srcEntity : sources) {
            if (!m_srcReg->valid(srcEntity)) {
                continue; // 已失效/未知的 StableId，跳过，不是致命错误
            }

            const Entity dstEntity        = m_dstReg->create();
            const std::uint32_t exclude[] = {skip};
            m_mapper->clone(*m_srcReg, srcEntity, *m_dstReg, dstEntity, exclude);

            const StableId sourceId = m_srcId->get(srcEntity)->value();
            const StableId clonedId = m_dstId->ensure(dstEntity);
            results.clones.emplace_back(Result{dstEntity, sourceId, clonedId});
            results.remap.emplace(sourceId, clonedId);
        }

        applyFixup(results);

        return results;
    }

    [[nodiscard]] BatchResult cloneBatch(std::span<StableId> sourceIds)
    {
        if (sourceIds.empty()) {
            return {};
        }

        if (!m_srcReg || !m_srcId || !m_dstReg || !m_dstId) {
            throw std::runtime_error("Cloner not initialized");
        }

        std::vector<Entity> srcEntities;
        srcEntities.reserve(sourceIds.size());

        for (const StableId &oldId : sourceIds) {
            const Entity srcEntity = m_srcId->find(oldId).value_or({});
            if (!m_srcReg->valid(srcEntity)) {
                continue; // 已失效/未知的 StableId，跳过，不是致命错误
            }
            srcEntities.emplace_back(srcEntity);
        }

        return cloneBatch(srcEntities);
    }

    /**
     * @brief 批量克隆实体，返回一个 StableIdRemap，记录了每个实体的旧ID到新ID的映射。
     * @param sourceIds 要作为一组进行克隆的实体的稳定ID  以关系一致的批次形式。
     *        未知或过时的ID将被静默跳过（不构成致命错误）。
     * @return 每个实体实际上都映射了从旧的 StableId 到新的 StableId 的映射  
     *         cloned — 调用者可以重复使用它（例如，将 Selection 重新映射到新克隆上）
     *
     *@note 使用 cloneBatch 代替
     */
    [[nodiscard]] [[deprecated("Replace it with cloneBatch()")]] StableIdRemap clone(
        std::span<StableId> sourceIds)
    {
        if (sourceIds.empty()) {
            return {};
        }

        if (!m_srcReg || !m_srcId || !m_dstReg || !m_dstId) {
            throw std::runtime_error("Cloner not initialized");
        }

        // ---- Phase 1: Lookahead 前瞻: 解析句柄，预先生成旧ID到新ID的映射 ----
        StableIdRemap remap;
        remap.reserve(sourceIds.size());
        std::vector<Entity> srcEntities;
        srcEntities.reserve(sourceIds.size());

        for (const StableId &oldId : sourceIds) {
            const Entity srcEntity = m_srcId->find(oldId).value_or({});
            if (!m_srcReg->valid(srcEntity)) {
                continue; // 已失效/未知的 StableId，跳过，不是致命错误
            }

            remap.emplace(oldId, StableId{m_dstId->mint()});
            srcEntities.emplace_back(srcEntity);
        }

        // ---- Phase 2: Clone & ID Strip — 复制所有已注册的组件,
        //      然后附加预先生成的身份信息（切勿使用复制的那一个 -> sourceIds）----
        std::vector<Entity> newEntities;
        newEntities.reserve(srcEntities.size());

        const auto skip = ComponentMapper::stableidType();
        for (Entity srcEntity : srcEntities) {
            const Entity dstEntity        = m_dstReg->create();
            const std::uint32_t exclude[] = {skip};
            m_mapper->clone(*m_srcReg, srcEntity, *m_dstReg, dstEntity, exclude);

            const StableId oldId = m_srcId->get(srcEntity).value();
            m_dstId->assign(dstEntity, remap.at(oldId)); //  pre-generated in Phase 1
            newEntities.emplace_back(dstEntity);
        }

        // ---- Phase 3: 关系修复 — 重写批处理中的 StableId 引用 ----
        for (Entity dstEntity : newEntities) {
            m_mapper->remap(*m_dstReg, dstEntity, remap);
        }

        return remap;
    }

private:
    Result cloneEntity(Entity source, bool assign)
    {
        if (!m_srcReg || !m_srcId || !m_dstReg || !m_dstId) {
            throw std::runtime_error("Cloner not initialized");
        }

        if (!m_srcReg->valid(source)) {
            throw StableIdError("clone source is not a valid entity");
        }

        const auto sourceId           = m_srcId->get(source).value_or({});
        const auto dstEntity          = m_dstReg->create();
        const std::uint32_t exclude[] = {ComponentMapper::stableidType()};
        m_mapper->clone(*m_srcReg, source, *m_dstReg, dstEntity, exclude);

        Result result;
        result.clonedEntity = dstEntity;
        result.sourceId     = sourceId;

        if (assign) {
            result.clonedid = m_dstId->ensure(dstEntity);
        }
        return result;
    }

    void applyFixup(const BatchResult &results)
    {
        if (results.remap.empty()) {
            return;
        }

        for (const auto &clone : results.clones) {
            m_mapper->remap(*m_dstReg, clone.clonedEntity, results.remap);
        }
    }

private:
    const Registry *m_srcReg  = nullptr;
    const Identifier *m_srcId = nullptr;
    Registry *m_dstReg        = nullptr;
    Identifier *m_dstId       = nullptr;

    std::unique_ptr<ComponentMapper> m_mapper;
};

} // namespace bakuon::core
