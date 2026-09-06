#pragma once

#include <gui/b_commandsystem.h>

namespace bakuon::host {

const gui::CommandId kCmdNewTab{"host.tab.new"};
const gui::CommandId kCmdCloseTab{"host.tab.close"};
const gui::CommandId kCmdRestartTab{"host.tab.restart"};
const gui::CommandId kCmdQuit{"host.app.quit"};

inline const gui::ContextId kCtxGlobal
    = gui::CommandSystem::declareContext("global", "bakuon.host", "Host 应用程序全局上下文");

} // namespace bakuon::host
