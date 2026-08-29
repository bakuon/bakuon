#include "sandbox/b_sharedmemorychannel.h"

#include <cstring>

#include <QtCore/QSharedMemory>

namespace bakuon::sandbox {

SharedMemoryChannel::SharedMemoryChannel() = default;

SharedMemoryChannel::~SharedMemoryChannel()
{
    release();
}

std::optional<QString> SharedMemoryChannel::create(const QString &key,
                                                   const QByteArray &initialPayload,
                                                   quint32 extraCapacity)
{
    release();

    const qsizetype payloadCapacity = initialPayload.size() + static_cast<qsizetype>(extraCapacity);
    const qsizetype totalSize       = static_cast<qsizetype>(sizeof(Header)) + payloadCapacity;

    auto memory = std::make_unique<QSharedMemory>(key);
    // 有些平台（尤其是上一次进程异常退出后残留的 POSIX 共享内存段）会导致 create() 直接失败，
    // 报 AlreadyExists；主动先尝试 attach()+detach() 一次做"认领并释放"清理，
    // 再重新 create()，避免偶发的僵尸段把沙箱启动卡死。
    if (!memory->create(totalSize)) {
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
    auto *hdr          = headerPtr();
    hdr->magic         = kMagic;
    hdr->version       = kVersion;
    hdr->payloadLength = static_cast<quint32>(initialPayload.size());
    hdr->status        = StatusPending;
    if (!initialPayload.isEmpty()) {
        std::memcpy(payloadPtr(),
                    initialPayload.constData(),
                    static_cast<size_t>(initialPayload.size()));
    }
    m_memory->unlock();

    return std::nullopt;
}

std::optional<QString> SharedMemoryChannel::attach(const QString &key)
{
    release();

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
    const auto *hdr  = headerPtr();
    const bool valid = hdr->magic == kMagic;
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
    const auto *hdr = headerPtr();
    QByteArray result(reinterpret_cast<const char *>(payloadPtr()),
                      static_cast<qsizetype>(hdr->payloadLength));
    m_memory->unlock();
    return result;
}

std::optional<QString> SharedMemoryChannel::writePayload(const QByteArray &payload)
{
    if (!isAttached()) {
        return QStringLiteral("共享内存通道尚未 create()/attach()");
    }
    if (payload.size() > capacity()) {
        return QStringLiteral("写入长度 %1 超出共享内存段可用容量 %2")
            .arg(payload.size())
            .arg(capacity());
    }

    m_memory->lock();
    auto *hdr          = headerPtr();
    hdr->payloadLength = static_cast<quint32>(payload.size());
    if (!payload.isEmpty()) {
        std::memcpy(payloadPtr(), payload.constData(), static_cast<size_t>(payload.size()));
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
    headerPtr()->status = status;
    m_memory->unlock();
}

quint32 SharedMemoryChannel::status() const
{
    if (!isAttached()) {
        return StatusPending;
    }
    m_memory->lock();
    const quint32 s = headerPtr()->status;
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

SharedMemoryChannel::Header *SharedMemoryChannel::headerPtr()
{
    return reinterpret_cast<Header *>(m_memory->data());
}

const SharedMemoryChannel::Header *SharedMemoryChannel::headerPtr() const
{
    return reinterpret_cast<const Header *>(m_memory->constData());
}

uchar *SharedMemoryChannel::payloadPtr()
{
    return reinterpret_cast<uchar *>(m_memory->data()) + sizeof(Header);
}

const uchar *SharedMemoryChannel::payloadPtr() const
{
    return reinterpret_cast<const uchar *>(m_memory->constData()) + sizeof(Header);
}

} // namespace bakuon::sandbox
