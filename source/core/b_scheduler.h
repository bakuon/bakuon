#pragma once

/// 调度器，管理所有任务的执行和调度。
/// 把 Registry 注释掉的 entt::dispatcher 缺口正式补上
/**
  entt::organizer 系统调度封装

  当业务逻辑开始出现"每帧/每次编辑都要跑一遍的 System"
  （比如自动布局重算、约束求解、脏标记传播）时，手写调用顺序会很快失控。
  建议包一层 entt::organizer 的类型安全门面，风格与 IExtensionPoint<T>
  一致（系统注册、依赖声明、拓扑排序执行）。
 */
