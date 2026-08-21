#pragma once

#include <cstdint>

#include <QtCore/QByteArray>

namespace bakuon::gui::detail {

// MurmurHash3 32位算法
static inline uint32_t hashString(const QByteArray& utf8Bytes) noexcept
{
    // MurmurHash3 32-bit 常量
    constexpr uint32_t C1   = 0xcc9e2d51;
    constexpr uint32_t C2   = 0x1b873593;
    // 固定种子，保证确定性
    // 这意味着相同的字符串在任何时间、任何运行该程序
    // 的实例中都会生成相同的 uint32_t ID。这对于持久
    // 化存储或分布式系统至关重要
    constexpr uint32_t SEED = 0xc70f6907;

    uint32_t hash = SEED;

    // 转换为 UTF-8 字节数组
    // 注意：toUtf8() 会在堆上分配内存，如果该函数在超高频循环中调用，建议在外部转换后传入 QByteArray
    const char* data = utf8Bytes.constData();
    // const auto* data = reinterpret_cast<const uint8_t*>(utf8Bytes.constData());
    const int len    = utf8Bytes.length(); // 字节总数

    // 以 4 字节（32位）为一组进行主循环处理
    const int nblocks = len / 4;
    for (int i = 0; i < nblocks; ++i) {
        // 安全地从字节流中读取一个 32 位整数（考虑小端序）
        uint32_t kv;
        std::memcpy(&kv, data + (i * 4), sizeof(uint32_t));

        kv *= C1;
        kv = (kv << 15) | (kv >> 17); // 循环左移 15 位
        kv *= C2;

        hash ^= kv;
        hash = (hash << 13) | (hash >> 19); // 循环左移 13 位
        hash = hash * 5 + 0xe6546b64;
    }

    // 处理尾部剩余的字节（1 到 3 字节）
    const char* tail = data + (nblocks * 4);
    uint32_t k       = 0;

    switch (len & 3) {
    case 3: k ^= static_cast<uint32_t>(static_cast<uint8_t>(tail[2])) << 16; [[fallthrough]];
    case 2: k ^= static_cast<uint32_t>(static_cast<uint8_t>(tail[1])) << 8; [[fallthrough]];
    case 1:
        k ^= static_cast<uint32_t>(static_cast<uint8_t>(tail[0]));
        k *= C1;
        k = (k << 15) | (k >> 17);
        k *= C2;
        hash ^= k;
    }

    // 最终混淆（Finalization Mix）
    hash ^= static_cast<uint32_t>(len); // 混合 UTF-8 字节长度
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

    return hash;
}

} // namespace bakuon::gui::detail
