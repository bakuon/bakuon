#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include <bakuon/core/Container.h>
#include <bakuon/core/Identifier.h>
#include <bakuon/core/UndoStack.h>
#include <bakuon/core/Workspace.h>

using namespace bakuon::core;

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

/// 记录"析构时 Registry 是否还活着"——用来钉住服务先于 Container 销毁、且按逆序销毁。
template<int N>
struct Probe
{
    Probe(std::vector<std::string>& log, Registry& registry, Entity entity)
        : m_log(log)
        , m_registry(registry)
        , m_entity(entity)
    {
    }
    ~Probe()
    {
        m_log.push_back("~Probe" + std::to_string(N)
                        + (m_registry.valid(m_entity) ? ":registry-alive" : ":registry-dead"));
    }

    std::vector<std::string>& m_log;
    Registry& m_registry;
    Entity m_entity;
};

} // namespace

static_assert(DomainId::fromName("physics").isNamed());
static_assert(!DomainId::fromName("").isValid());
static_assert(DomainId::fromName("a") != DomainId::fromName("b"));

TEST(WorkspaceTest, RequireCreatesOnDemandAndIsIdempotent)
{
    Workspace ws;
    EXPECT_EQ(ws.count(), 0u);
    EXPECT_EQ(ws.find("physics"), nullptr);

    Domain& a = ws.require("physics");
    Domain& b = ws.require("physics");
    EXPECT_EQ(&a, &b);
    EXPECT_EQ(ws.count(), 1u);
    EXPECT_EQ(a.name(), "physics");
    EXPECT_EQ(ws.find(DomainId::fromName("physics")), &a);
}

TEST(WorkspaceTest, CreateReturnsNullptrWhenAlreadyExistsAndThrowsOnEmptyName)
{
    Workspace ws;
    EXPECT_NE(ws.create("a"), nullptr);
    EXPECT_EQ(ws.create("a"), nullptr);
    EXPECT_THROW(ws.create(""), DomainError);
    EXPECT_THROW(ws.require(""), DomainError);
}

TEST(WorkspaceTest, DomainsAreIsolated)
{
    Workspace ws;
    Domain& a = ws.require("a");
    Domain& b = ws.require("b");

    int constructedInB = 0;
    Connection conn    = b.container().onConstruct<Position>(
        [&](Container&, Entity) { ++constructedInB; });

    a.container().emplace<Position>(a.container().create(), 1.f, 2.f);

    int visitedInB = 0;
    b.container().each<Position>([&](Entity, Position&) { ++visitedInB; });
    EXPECT_EQ(constructedInB, 0);
    EXPECT_EQ(visitedInB, 0);
}

TEST(WorkspaceTest, DomainAddressIsStableAcrossGrowth)
{
    Workspace ws;
    Domain* first = &ws.require("d0");
    for (int i = 1; i < 200; ++i) {
        ws.require("d" + std::to_string(i));
    }
    EXPECT_EQ(ws.count(), 200u);
    EXPECT_EQ(&ws.require("d0"), first);
}

TEST(WorkspaceTest, HandleGoesStaleAfterDestroyEvenIfRecreatedWithSameName)
{
    Workspace ws;
    const DomainHandle old = ws.require("doc").handle();
    ASSERT_EQ(ws.resolve(old), ws.find("doc"));

    ASSERT_TRUE(ws.destroy(old.id()));
    EXPECT_EQ(ws.resolve(old), nullptr);
    EXPECT_FALSE(ws.destroy(old.id())) << "重复销毁应返回 false";

    Domain& reborn = ws.require("doc");
    EXPECT_EQ(ws.resolve(old), nullptr) << "同名重建不应让旧 handle 复活";
    EXPECT_EQ(ws.resolve(reborn.handle()), &reborn);
    EXPECT_NE(reborn.serial(), old.serial());
}

TEST(WorkspaceTest, AnonymousDomainsGetDistinctNonNamedIds)
{
    Workspace ws;
    Domain& a = ws.createAnonymous("doc-1");
    Domain& b = ws.createAnonymous("doc-2");
    EXPECT_NE(a.id(), b.id());
    EXPECT_FALSE(a.id().isNamed());
    EXPECT_TRUE(a.id().isValid());
    EXPECT_EQ(ws.count(), 2u);
}

TEST(WorkspaceTest, HooksFireAndDisconnect)
{
    std::vector<std::string> log;
    Workspace ws;
    Connection created = ws.onDomainCreated(
        [&](Domain& d) { log.push_back("created:" + std::string(d.name())); });
    Connection destroying = ws.onDomainDestroying([&](Domain& d) {
        log.push_back("destroying:" + std::string(d.name())
                      + (ws.find(d.id()) ? ":visible" : ":hidden"));
    });

    ws.require("x");
    ASSERT_TRUE(ws.destroy(DomainId::fromName("x")));
    EXPECT_EQ(log, (std::vector<std::string>{"created:x", "destroying:x:visible"}));

    created.disconnect();
    ws.require("y");
    EXPECT_EQ(log.size(), 2u) << "断开后不应再收到 created 通知";
}

TEST(WorkspaceTest, ConnectionMayOutliveWorkspace)
{
    Connection conn;
    {
        Workspace ws;
        conn = ws.onDomainCreated([](Domain&) {});
    }
    conn.disconnect(); // 不应崩溃（ASan 下验证无 use-after-free）
}

TEST(WorkspaceTest, ReentrantDestroyOnClosingDomainIsRejected)
{
    Workspace ws;
    bool nested     = true;
    Connection conn = ws.onDomainDestroying([&](Domain& d) { nested = ws.destroy(d.id()); });

    ws.require("r");
    EXPECT_TRUE(ws.destroy(DomainId::fromName("r")));
    EXPECT_FALSE(nested);
    EXPECT_EQ(ws.count(), 0u);
}

TEST(WorkspaceTest, ClearDestroysInReverseCreationOrder)
{
    std::vector<std::string> log;
    Workspace ws;
    Connection conn = ws.onDomainDestroying(
        [&](Domain& d) { log.push_back(std::string(d.name())); });

    ws.require("a");
    ws.require("b");
    ws.require("c");
    ws.clear();

    EXPECT_EQ(log, (std::vector<std::string>{"c", "b", "a"}));
    EXPECT_EQ(ws.count(), 0u);
}

TEST(WorkspaceTest, EachToleratesDestroyingOtherDomainsDuringIteration)
{
    Workspace ws;
    ws.require("a");
    ws.require("b");
    ws.require("c");

    std::vector<std::string> visited;
    ws.each([&](Domain& d) {
        visited.emplace_back(d.name());
        if (d.name() == "a") {
            ws.destroy(DomainId::fromName("b"));
        }
    });
    EXPECT_EQ(visited, (std::vector<std::string>{"a", "c"}));
}

TEST(WorkspaceTest, ServicesDestroyInReverseOrderBeforeRegistry)
{
    std::vector<std::string> log;
    {
        Workspace ws;
        Domain& d      = ws.require("doc");
        const Entity e = d.container().create();
        d.emplaceService<Probe<1>>(log, d.registry(), e);
        d.emplaceService<Probe<2>>(log, d.registry(), e);
        d.emplaceService<Probe<3>>(log, d.registry(), e);

        EXPECT_TRUE(d.eraseService<Probe<2>>());
        EXPECT_FALSE(d.hasService<Probe<2>>());
        EXPECT_EQ(d.serviceCount(), 2u);
    }
    EXPECT_EQ(log,
              (std::vector<std::string>{"~Probe2:registry-alive",
                                        "~Probe3:registry-alive",
                                        "~Probe1:registry-alive"}));
}

TEST(WorkspaceTest, ServiceApiContracts)
{
    Workspace ws;
    Domain& d = ws.require("doc");
    EXPECT_EQ(d.tryService<Position>(), nullptr);
    EXPECT_THROW((void) d.service<Position>(), DomainError);

    d.emplaceService<Position>(1.f, 2.f);
    EXPECT_EQ(d.service<Position>().y, 2.f);
    EXPECT_THROW(d.emplaceService<Position>(), DomainError) << "重复登记必须显式失败";
}

TEST(WorkspaceTest, InitializerFailureRollsBackCleanly)
{
    std::vector<std::string> log;
    Workspace ws;
    EXPECT_THROW(ws.require("bad",
                            [&](Domain& d) {
                                const Entity e = d.container().create();
                                d.emplaceService<Probe<1>>(log, d.registry(), e);
                                throw std::runtime_error("boom");
                            }),
                 std::runtime_error);

    EXPECT_EQ(ws.count(), 0u);
    EXPECT_EQ(ws.find("bad"), nullptr);
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "~Probe1:registry-alive");
}

TEST(WorkspaceTest, ExistingCoreModulesAttachAsServices)
{
    Workspace ws;
    Domain& d = ws.require("doc", [](Domain& dom) {
        auto& gen = dom.emplaceService<SnowflakeGenerator>(1);
        dom.emplaceService<Identifier>(dom.registry(), &gen);
        dom.emplaceService<UndoStack<Position>>(dom.registry());
    });

    const Entity e   = d.container().create();
    const StableId s = d.service<Identifier>().ensure(e);
    EXPECT_EQ(d.service<Identifier>().find(s), e);
    EXPECT_TRUE(d.service<UndoStack<Position>>().historyDepth() >= 1u);

    EXPECT_TRUE(ws.destroy(d.id())); // ASan：Identifier 的连接应在 Registry 之前释放
}
