#pragma once

/// 多文档骨架，其余很多能力都依赖"文档"这个边界概念
/**
core 层完全没有"同时管理多个 Registry、处理跨文档引用"的概念。IDE 场景（多文件同时打开、跨文件资源引用）迟早需要：
Workspace 持有 std::unordered_map<StableId/Id, std::unique_ptr<Registry>>
跨 Registry 的引用解析（不是 identity::find，那是同一个 Registry 内的）
文档打开/关闭的生命周期钩子（对应未来 RegistryBridge 一类的桥接需要感知"哪个文档"）。
Workspace 本身不管理 Registry，而是通过 RegistryBridge 进行跨 Registry 的引用解析。
*/
