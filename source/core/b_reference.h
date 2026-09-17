#pragma once

/// 跨实体/跨文档引用组件，克隆/diff/GUI 属性面板都要用到
/**
现在唯一的"稳定引用"机制是 StableId，但没有一个组件级别的"这个字段引用另一个实体"的标准表达。
场景树/资源编辑器几乎必然需要"材质引用纹理""节点引用预制体"这类关系，且这些引用必须能扛住：
 1. undo/redo（UndoStack 整体 clear+reload）
 2. 序列化落盘/跨进程传递（DocumentSerializer）
 3. 被引用对象删除时的悬空处理（弱引用语义）

 建议：struct Ref { StableId target; }（配 to_json/from_json），
 外加 reference::resolve(registry, Ref) 之类的查询函数，并在 Identity::的 onDestroy
 钩子基础上提供"删除前收集所有指向它的 Ref"的辅助（不然目前只能业务层各自维护倒排表）。
*/
