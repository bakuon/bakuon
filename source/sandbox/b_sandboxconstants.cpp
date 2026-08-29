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

QString makeSharedMemoryKey(const QString &sandboxId, const QString &requestId)
{
    return QStringLiteral("bakuon-cmd-%1-%2").arg(sandboxId, requestId);
}

} // namespace bakuon::sandbox
