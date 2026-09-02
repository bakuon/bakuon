#pragma once

#include <optional>

#include <QtCore/QString>

namespace bakuon::sandbox {

/**
 * @brief sandbox_runtime 可执行文件的命令行参数约定。
 * @note 由 SandboxSupervisor 生成、拼接进 QProcess 启动参数；
 *       sandbox_runtime/main.cpp 解析后据此构造 SandboxRuntime。
 */
namespace cli {
/// 沙箱进程应当监听（setHostUrl）的本地地址，如 "local:bakuon-sandbox-<uuid>"。
/// 有了注册中心之后，这个地址依然需要——enableRemoting() 发布的对象最终还是要
/// 有一个具体地址供 Replica 的实际数据连接落地，注册中心只负责"告诉对端去哪连"，
/// 不代替这个地址本身。
inline constexpr auto kListen    = "--sandbox-listen";
/// 注册中心地址，见 registryUrl()；沙箱进程用它 setRegistryUrl()，不再需要知道
/// Host 主程序的地址（Host 也一样，只需要知道这一个地址）。
inline constexpr auto kRegistry  = "--sandbox-registry";
/// Host 分配的沙箱实例 id：现在除了诊断用途外，还是 makeSandboxObjectName() 的输入，
/// 直接决定了这个沙箱在注册中心里发布的对象名，因此变成了必需参数（不再只是可选诊断信息）。
inline constexpr auto kSandboxId = "--sandbox-id";
} // namespace cli

/// QRemoteObject 对象在契约里的名字前缀（对应 .rep 里的 class 名）。
/// 引入 QRemoteObjectRegistryHost 之后，多个沙箱实例共享同一个注册中心，
/// 单纯用类名当对象名会互相覆盖——实际发布/acquire 时用 makeSandboxObjectName()
/// 拼出每个实例专属的名字，这里的常量只作为该函数内部的前缀。
inline constexpr auto kSandboxObjectName = "PluginSandboxControl";

/// 注册中心（QRemoteObjectRegistryHost）监听的固定地址：Host 主程序启动时创建，
/// 生命周期与主程序一致；所有沙箱子进程只需要知道这一个地址，不再需要 Host
/// 反过来知道每个沙箱实例的地址——发现关系完全由注册中心居中代理。
[[nodiscard]] QString registryUrl();

/// 每个沙箱实例在注册中心里发布的对象名必须唯一（同一注册中心下会有多个并发
/// 运行的沙箱实例，都发布同一个契约类型），用 sandboxId 拼出专属名字。
[[nodiscard]] QString makeSandboxObjectName(const QString &sandboxId);

/// makeSandboxObjectName() 的逆操作：从注册中心汇报的对象名解析回 sandboxId。
/// 用于孤儿沙箱发现——SandboxSystem 收到 QRemoteObjectRegistry::remoteObjectAdded
/// 时，只知道对象名字符串，需要反解出 sandboxId 才能判断"这是不是本实例已知的"。
/// 前缀不匹配（比如注册中心里出现了别的、不属于本契约的对象）时返回 std::nullopt。
[[nodiscard]] std::optional<QString> parseSandboxObjectName(const QString &objectName);

/**
 * @brief 生成一个每次调用都不同的本地连接地址（local: scheme）。
 * @details Windows 上映射为命名管道，Unix 上映射为 Unix Domain Socket——
 *          都满足需求里"本地命名管道连接"的约束，同时保持跨平台代码一致。
 *          每个沙箱实例独占一个地址，避免多个并发沙箱互相串话。
 */
[[nodiscard]] QString makeSandboxListenUrl();

/**
 * @brief 生成一次命令执行专属的共享内存段 key。
 * @param sandboxId 沙箱实例 id（诊断用，也参与 key 组成防止跨实例碰撞）
 * @param requestId 单次 executeCommand() 调用的唯一请求 id
 */
[[nodiscard]] QString makeSharedMemoryKey(const QString &sandboxId, const QString &requestId);

} // namespace bakuon::sandbox
