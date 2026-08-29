#pragma once

#include <QtCore/QString>

namespace bakuon::sandbox {

/**
 * @brief sandbox_runtime 可执行文件的命令行参数约定。
 * @note 由 SandboxSupervisor 生成、拼接进 QProcess 启动参数；
 *       sandbox_runtime/main.cpp 解析后据此构造 SandboxRuntime。
 */
namespace cli {
/// 沙箱进程应当监听（setHostUrl）的本地地址，如 "local:bakuon-sandbox-<uuid>"。
/// 见 PluginSandboxControl.rep 顶部注释：Sandbox 是监听/发布端，Host 是连接/acquire 端。
inline constexpr auto kListen    = "--sandbox-listen";
/// Host 分配的沙箱实例 id（纯诊断/日志用途，不参与寻址）。
inline constexpr auto kSandboxId = "--sandbox-id";
} // namespace cli

/// QRemoteObject 对象在契约里的名字（对应 .rep 里的 class 名），
/// enableRemoting()/acquire<Replica>() 两侧必须使用同一个名字才能匹配上。
inline constexpr auto kSandboxObjectName = "PluginSandboxControl";

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
