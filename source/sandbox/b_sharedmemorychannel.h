#pragma once

#include <memory>
#include <optional>

#include <QtCore/QByteArray>
#include <QtCore/QString>

class QSharedMemory;

namespace bakuon::sandbox {

/**
 * @brief 沙箱命令的共享内存数据通道 —— "血肉"部分的核心传输原语。
 *
 * ## 设计动机
 * QtRO (.rep) 契约通道只用来传递控制信令（初始化/运行/停止/进度/日志），刻意
 * 不在契约里携带任何大块业务数据——QtRO 的每次属性/信号同步都要经过
 * QDataStream 序列化 + 管道往返，用它搬运大文本/音视频/点云/图片这类数据会
 * 既慢又占用主控制通道带宽，拖慢状态同步的实时性。
 *
 * 真正的大数据搬运走 QSharedMemory：Host 和 Sandbox 两个进程把同一块共享
 * 内存映射进各自地址空间，数据本身"原地"可见，不需要跨进程拷贝或序列化——
 * QtRO 契约里只传递一个字符串 key（见 PluginSandboxControl.rep 的
 * executeCommand(requestId, commandId, memoryKey, params)），Sandbox 收到
 * key 后自己挂载对应的共享内存块，直接在原地读写。
 *
 * ## 内存布局
 * Header 定长，Payload 变长，整体位于同一块 QSharedMemory 段里：
 * @code
 *   +------------------+------------------------------------------+
 *   |      Header      |                  Payload                 |
 *   | (sizeof(Header)) |         (capacity() - Header 大小)         |
 *   +------------------+------------------------------------------+
 * @endcode
 *
 * ## 就绪/完成的同步策略
 * QSharedMemory 本身只是一块裸内存，不提供"数据是否已就绪"的跨进程通知
 * 机制——这是刻意的设计：就绪/完成的通知全部走控制通道（QtRO 的
 * executeCommand() 调用本身、以及 commandFinished 信号），共享内存全程只
 * 承载数据本身，不重复发明一套跨进程信号量。调用协议：
 *  1. Host 侧 create() 一块共享内存、写入 Header + 输入 Payload，然后才通过
 *     executeCommand() 把 key 发给 Sandbox；
 *  2. Sandbox 收到 executeCommand() 后才 attach()，此时数据保证已经写完
 *     （QtRO 方法调用本身即扮演了"输入数据已就绪"的跨进程内存屏障）；
 *  3. Sandbox 写完结果 Payload 后，通过 commandFinished 信号通知 Host，
 *     Host 才能读取结果区。
 * QSharedMemory::lock()/unlock() 仍然在每次读写 Payload 时配对使用，防止
 * 诊断工具等第三方意外并发访问导致数据竞争，但不承担"等待对方写完"这类
 * 时序协调职责（那始终是控制通道的职责）。
 */
class SharedMemoryChannel
{
public:
    /// 共享内存头部，定长，位于共享内存起始处。
    struct Header
    {
        quint32 magic         = kMagic;   ///< 格式校验魔数，防止误挂载到无关共享内存段
        quint32 version       = kVersion; ///< 布局版本号，为未来扩展预留
        quint32 payloadLength = 0;        ///< 当前 Payload 有效字节数（不含 Header）
        quint32 status        = 0;        ///< 0=待处理 1=处理成功 2=处理失败，调用方可自行扩展约定
    };

    static constexpr quint32 kMagic   = 0x554B4142; // little-endian 'BAKU'
    static constexpr quint32 kVersion = 1;

    enum Status : quint32 { StatusPending = 0, StatusOk = 1, StatusFailed = 2 };

    SharedMemoryChannel();
    ~SharedMemoryChannel();

    SharedMemoryChannel(const SharedMemoryChannel &)            = delete;
    SharedMemoryChannel &operator=(const SharedMemoryChannel &) = delete;

    /**
     * @brief 创建一块新的共享内存段（通常由 Host 侧调用），写入 Header + 初始 Payload。
     * @param key            共享内存唯一标识，见 makeSharedMemoryKey()
     * @param initialPayload 写入的初始数据（例如命令输入参数的序列化结果）；可为空
     * @param extraCapacity  除 initialPayload 外，额外预留的 Payload 容量（供 Sandbox 写回
     *                       结果用；结果与输入共用同一块 Payload 区域，Sandbox 处理完毕后原地
     *                       覆写并更新 payloadLength，这里应预留"输入和输出中较大者"的空间）
     * @return 成功返回 std::nullopt；失败返回错误描述
     */
    [[nodiscard]] std::optional<QString> create(const QString &key,
                                                const QByteArray &initialPayload,
                                                quint32 extraCapacity = 0);

    /**
     * @brief 挂载一块已存在的共享内存段（通常由 Sandbox 侧调用，key 来自 executeCommand 的参数）。
     */
    [[nodiscard]] std::optional<QString> attach(const QString &key);

    /**
     * @brief 读出当前 Payload（拷贝一份出来，跨进程共享内存段本身不适合长期持有裸指针）。
     * @note 调用方需自行保证读取时机在"数据已就绪"之后（见类注释的同步策略）。
     */
    [[nodiscard]] QByteArray readPayload() const;

    /**
     * @brief 原地覆写 Payload（不改变共享内存段的总容量，只能写入不超过剩余容量的数据）。
     * @return 成功返回 std::nullopt；容量不足或未 attach/create 返回错误描述
     */
    [[nodiscard]] std::optional<QString> writePayload(const QByteArray &payload);

    void setStatus(quint32 status);
    [[nodiscard]] quint32 status() const;

    [[nodiscard]] const QString &key() const noexcept { return m_key; }
    [[nodiscard]] bool isAttached() const;
    /// Payload 可用容量（不含 Header），即 create() 时 initialPayload.size() + extraCapacity。
    /// attach() 端由实际共享内存大小推导，可能因平台页对齐略大于创建端请求值。
    [[nodiscard]] qsizetype capacity() const;

    /// 显式分离/释放；析构时会自动调用，提供出来是为了让调用方能提前主动释放。
    void release();

private:
    [[nodiscard]] Header *headerPtr();
    [[nodiscard]] const Header *headerPtr() const;
    [[nodiscard]] uchar *payloadPtr();
    [[nodiscard]] const uchar *payloadPtr() const;

private:
    QString m_key;
    std::unique_ptr<QSharedMemory> m_memory;
};

} // namespace bakuon::sandbox
