#pragma once

// ============================================================================
// bakuon::core 门面头文件（facade）
//
// 参见 include/bakuon/gui/IPlugin.h 顶部关于门面 / 内部实现分层的说明：
// source/core/ 下的 b_ 前缀头文件是内部实现，include/bakuon/core/ 是面向消费者
// （gui/plugin/sandbox，以及未来第三方插件）的稳定转发层。
//
// 与 gui 门面的一个关键差异：core 目前完全由模板/内联组成（EnTT 本身也是
// header-only 模板库），没有跨动态库边界的符号导出问题（core 仍然是 STATIC 库），
// 因此这里的"门面"就是直接 #include 对应的内部头文件，不需要另外包一层适配代码。
// 保留这一层转发是为了让消费者始终写：
//   #include "bakuon/core/Entity.h"
// 而不是：
//   #include "core/b_entity.h"
// 一旦将来 core 的内部实现分层策略发生变化（例如引入非模板的 PIMPL 部分），
// 只需要调整这一层转发，消费者的 #include 路径不受影响。
// ============================================================================

#include "core/b_handle.h" // IWYU pragma: export
