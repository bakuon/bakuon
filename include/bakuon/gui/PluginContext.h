#pragma once

// ============================================================================
// bakuon::gui 插件开发 SDK —— 门面头文件（facade）
// 参见 include/bakuon/gui/Plugin.h 顶部关于门面 / 内部实现分层的说明。
//
// PluginContext 在 Plugin::initialize(PluginContext&) 中传入，
// 插件开发者需要它才能拿到启动参数等能力，因此和 Plugin 一起被列入公开门面。
// ============================================================================

#include "gui/b_plugincontext.h"
