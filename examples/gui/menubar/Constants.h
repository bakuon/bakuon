#pragma once

#include <gui/b_commandsystem.h>

namespace bakuon::examples {

/// 全局共用的命令 ID，跨编辑器复用同一份"命令语义"（同一个代理 QAction）

const gui::CommandId kCmdDelete{"edit.delete"};
const gui::CommandId kCmdDuplicate{"edit.duplicate"};
const gui::CommandId kCmdPaste{"edit.paste"};
const gui::CommandId kCmdSave{"file.save"};
const gui::CommandId kCmdCustomize{"view.customize"};

/// 声明命令动作的上下文

inline const gui::ContextId kCtxGlobal = gui::CommandSystem::declareContext("global", "bakuon",
                                                                            "应用程序全局上下文");

inline const gui::ContextId kImageFocused
    = gui::CommandSystem::declareContext("editor.image.focused", "plugin.image",
                                         "图像编辑器获得焦点");
inline const gui::ContextId kImageObjectSelected
    = gui::CommandSystem::declareContext("editor.image.objectSelected", "plugin.image",
                                         "画布上选中了对象");

inline const gui::ContextId kScene3dFocused
    = gui::CommandSystem::declareContext("editor.scene3d.focused", "plugin.scene3d",
                                         "3D 编辑器获得焦点");
inline const gui::ContextId kScene3dObjectSelected
    = gui::CommandSystem::declareContext("editor.scene3d.objectSelected", "plugin.scene3d",
                                         "3D 场景中选中了对象");

} // namespace bakuon::examples
