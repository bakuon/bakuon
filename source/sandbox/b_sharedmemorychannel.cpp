#include "sandbox/b_sharedmemorychannel.h"

#include <cstring>

#include <QtCore/QSharedMemory>

namespace bakuon::sandbox {

namespace {
constexpr quint32 kMagic   = 0x554B4142; // little-endian 'BAKU'/ 0x424B5348 -'BKSH'
constexpr quint32 kVersion = 1;
} // namespace

/// 共享内存头部，定长，位于共享内存起始处。
struct SharedMemoryChannel::Header
{
    quint32 magic   = kMagic;     ///< 格式校验魔数，防止误挂载到无关共享内存段
    quint32 version = kVersion;   ///< 布局版本号，为未来扩展预留
    quint32 status{0};            ///< 0=待处理 1=处理成功 2=处理失败，调用方可自行扩展约定
    quint32 payloadSize{0};       ///< 当前 Payload 有效字节数（不含 Header）
    qsizetype payloadCapacity{0}; ///< 额外预留的 Payload 容量回写结果
};

SharedMemoryChannel::SharedMemoryChannel() = default;

SharedMemoryChannel::~SharedMemoryChannel()
{
    release();
}

SharedMemoryChannel::SharedMemoryChannel(SharedMemoryChannel &&o) noexcept
    : m_key(std::move(o.m_key))
    , m_memory(std::move(o.m_memory))
{
    o.m_key.clear();
}

SharedMemoryChannel &SharedMemoryChannel::operator=(SharedMemoryChannel &&o) noexcept
{
    if (this != &o) {
        release();
        m_key    = std::move(o.m_key);
        m_memory = std::move(o.m_memory);
        o.m_key.clear();
    }
    return *this;
}

std::optional<QString> SharedMemoryChannel::create(const QString &key, const QByteArray &input,
                                                   quint32 extraCapacity)
{
    release();

    if (key.isEmpty()) {
        return QStringLiteral("共享内存 key 为空");
    }

    const auto payloadCapacity = input.size() + static_cast<qsizetype>(extraCapacity);
    const auto totalSize       = static_cast<qsizetype>(sizeof(Header)) + payloadCapacity;

    auto memory = std::make_unique<QSharedMemory>(key);
    // 有些平台（尤其是上一次进程异常退出后残留的 POSIX 共享内存段）会导致 create() 直接失败，
    // 报 AlreadyExists；主动先尝试 attach()+detach() 一次做"认领并释放"清理，
    // 再重新 create()，避免偶发的僵尸段把沙箱启动卡死。
    if (!memory->create(totalSize)) {
        // 崩溃残留：先 attach/detach 再试一次 create。
        if (memory->error() == QSharedMemory::AlreadyExists) {
            QSharedMemory stale(key);
            if (stale.attach()) {
                stale.detach();
            }
        }
        if (!memory->create(totalSize)) {
            return QStringLiteral("QSharedMemory::create(%1, %2 bytes) 失败：%3")
                .arg(key)
                .arg(totalSize)
                .arg(memory->errorString());
        }
    }

    m_key    = key;
    m_memory = std::move(memory);

    m_memory->lock();
    auto *h            = header();
    h->magic           = kMagic;
    h->version         = kVersion;
    h->status          = StatusPending;
    h->payloadSize     = static_cast<quint32>(input.size());
    h->payloadCapacity = payloadCapacity;
    if (!input.isEmpty()) {
        std::memcpy(payload(), input.constData(), static_cast<size_t>(input.size()));
    }
    m_memory->unlock();

    return std::nullopt;
}

std::optional<QString> SharedMemoryChannel::attach(const QString &key)
{
    release();

    if (key.isEmpty()) {
        return QStringLiteral("共享内存 key 为空");
    }

    auto memory = std::make_unique<QSharedMemory>(key);
    if (!memory->attach()) {
        return QStringLiteral("QSharedMemory::attach(%1) 失败：%2").arg(key, memory->errorString());
    }
    if (memory->size() < static_cast<qsizetype>(sizeof(Header))) {
        return QStringLiteral("共享内存段 %1 大小(%2)小于 Header，格式非法")
            .arg(key)
            .arg(memory->size());
    }

    m_key    = key;
    m_memory = std::move(memory);

    m_memory->lock();
    const auto *h    = header();
    const bool valid = h->magic == kMagic;
    m_memory->unlock();

    if (!valid) {
        QString err = QStringLiteral("共享内存段 %1 魔数校验失败，可能挂载到了无关的段").arg(key);
        release();
        return err;
    }
    return std::nullopt;
}

QByteArray SharedMemoryChannel::readPayload() const
{
    if (!isAttached()) {
        return {};
    }
    m_memory->lock();
    const auto *h = header();
    QByteArray result(reinterpret_cast<const char *>(payload()),
                      static_cast<qsizetype>(h->payloadCapacity));
    m_memory->unlock();
    return result;
}

std::optional<QString> SharedMemoryChannel::writePayload(const QByteArray &payload)
{
    if (!isAttached()) {
        return QStringLiteral("共享内存通道尚未挂载: create()/attach()");
    }

    m_memory->lock();
    auto *h = header();
    if (payload.size() > static_cast<qsizetype>(h->payloadCapacity)) {
        return QStringLiteral("写入长度 %1 超出共享内存段可用容量 %2")
            .arg(payload.size())
            .arg(h->payloadCapacity);
    }

    h->payloadSize = static_cast<quint32>(payload.size());
    if (!payload.isEmpty()) {
        std::memcpy(this->payload(), payload.constData(), static_cast<size_t>(payload.size()));
    }
    m_memory->unlock();
    return std::nullopt;
}

void SharedMemoryChannel::setStatus(quint32 status)
{
    if (!isAttached()) {
        return;
    }
    m_memory->lock();
    header()->status = status;
    m_memory->unlock();
}

quint32 SharedMemoryChannel::status() const
{
    if (!isAttached()) {
        return StatusPending;
    }
    m_memory->lock();
    const quint32 s = header()->status;
    m_memory->unlock();
    return s;
}

bool SharedMemoryChannel::isAttached() const
{
    return m_memory && m_memory->isAttached();
}

qsizetype SharedMemoryChannel::capacity() const
{
    if (!isAttached()) {
        return 0;
    }

    return m_memory->size() - static_cast<qsizetype>(sizeof(Header));
}

void SharedMemoryChannel::release()
{
    if (m_memory) {
        if (m_memory->isAttached()) {
            m_memory->detach();
        }
        m_memory.reset();
    }
    m_key.clear();
}

SharedMemoryChannel::Header *SharedMemoryChannel::header()
{
    return reinterpret_cast<Header *>(m_memory->data());
}

const SharedMemoryChannel::Header *SharedMemoryChannel::header() const
{
    return reinterpret_cast<const Header *>(m_memory->constData());
}

uchar *SharedMemoryChannel::payload()
{
    return reinterpret_cast<uchar *>(m_memory->data()) + sizeof(Header);
}

const uchar *SharedMemoryChannel::payload() const
{
    return reinterpret_cast<const uchar *>(m_memory->constData()) + sizeof(Header);
}

} // namespace bakuon::sandbox
