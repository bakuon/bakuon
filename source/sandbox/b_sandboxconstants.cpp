#include "sandbox/b_sandboxconstants.h"

#include <QtCore/QUuid>

namespace bakuon::sandbox {

QString makeSandboxListenUrl()
{
    // QUuid::createUuid() 内部基于系统随机源，跨进程/跨实例碰撞概率可忽略不计；
    // 不用插件 id 做地址的一部分，是因为同一个插件可能被同时起多个沙箱实例
    // （比如故意用于压测/多副本场景），地址必须与"插件是谁"完全解耦。
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QStringLiteral("local:bakuon-sandbox-%1").arg(uuid);
}

QString registryUrl()
{
    // 固定地址，进程内常量：main 程序生命周期内只会有一个 QRemoteObjectRegistryHost。
    // 如果同一台机器上可能同时跑多个 bakuon 主程序实例，需要在这里混入
    // QCoreApplication::applicationPid() 之类的区分符，避免两个主程序抢同一个注册中心地址
    // ——本次先不处理这个场景，按单实例假设走，等真的需要多开时再补。
    return QStringLiteral("local:bakuon-sandbox-registry");
}

QString makeSandboxObjectName(const QString &sandboxId)
{
    return QStringLiteral("%1@%2").arg(QString::fromLatin1(kSandboxObjectName), sandboxId);
}

std::optional<QString> parseSandboxObjectName(const QString &objectName)
{
    const QString prefix = QString::fromLatin1(kSandboxObjectName) + QLatin1Char('@');
    if (!objectName.startsWith(prefix)) {
        return std::nullopt;
    }
    return objectName.mid(prefix.length());
}

QString makeSharedMemoryKey(const QString &sandboxId, const QString &requestId)
{
    return QStringLiteral("bakuon-cmd-%1-%2").arg(sandboxId, requestId);
}

} // namespace bakuon::sandbox
