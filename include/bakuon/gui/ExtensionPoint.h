#pragma once

// ============================================================================
// bakuon::gui 插件开发 SDK —— 门面头文件（facade）
// 参见 include/bakuon/gui/Plugin.h 顶部关于门面 / 内部实现分层的说明。
//
// 只转发 ExtensionPointBase：这是 source/gui/b_extensionpoint.h 里唯一被显式注释为
// “暴露于外部”的类型。AdvancedExtensionPoint / DeferredExtensionPoint 在 source 中
// 被明确标注为“仅内部使用”，因此没有在此转发；如果后续要把它们也开放给插件开发者，
// 需要先去掉 source 里那两句“仅内部使用”的注释、确认这确实是有意为之，
// 再在这里补一份对应的转发头文件，而不要让插件直接 #include "gui/b_xxx.h"。
// ============================================================================

#include "gui/b_extensionpoint.h"
