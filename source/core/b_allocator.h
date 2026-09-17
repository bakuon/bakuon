#pragma once

/**
  自定义分配器 / 对象池 — b_allocator.h

  entt::registry 支持自定义分配器；大规模场景（数万实体）下默认
  分配器可能成为瓶颈。目前没有必要抢先做，但应该在 Registry 构造
  函数上预留分配器注入点，避免以后是 ABI/接口破坏性改动。
 */
