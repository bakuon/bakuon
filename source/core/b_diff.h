#pragma once

/// 组件级 Diff/Patch：为将来的协作编辑/网络同步预留接口，哪怕先不接后端，也得先预留接口。
/**
  UndoStack/Serializer 都是"整体快照"路线（类文档里也明确写了这是刻意取舍），
  但下列场景迟早需要增量能力，而不是每次都整帧比对：
    1.网络协作/多端同步（沙箱架构已经为跨进程通信打好了基础，协作编辑是自然延伸）
    2.大文档下 UndoStack 的内存/JSON 序列化开销（historyDepth() 越深、单帧越大，问题越明显）
  不需要一步到位做 OT/CRDT，先做"给定两份 Frame（复用 Serializer/UndoStack 已有的归档格式），
  算出 component 级别的 add/remove/modify 列表"这个纯函数级能力即可。
 */
