#pragma once

#include <gui/detail/b_types.h>

namespace bakuon::examples {

// 全局共用的命令 ID，跨编辑器复用同一份"命令语义"（同一个代理 QAction）
const gui::CommandId kCmdDelete{"edit.delete"};
const gui::CommandId kCmdDuplicate{"edit.duplicate"};
const gui::CommandId kCmdSave{"file.save"};
const gui::CommandId kCmdCustomize{"view.customize"};

const gui::ContextId kCtxGlobal{"global"};

} // namespace bakuon::examples
