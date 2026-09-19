#pragma once

/**
  非 Qt 日志抽象 — b_log.h

  core 严格不依赖 Qt（gui 里到处是 qWarning()/qDebug()，core 目前几乎没有
  任何日志输出，比如 b_identifier.cpp/b_hierarchy.cpp 里的防御性分支都是静默
  返回）。至少需要一个可被 gui/sandbox 注入 sink 的最小日志接口（函数指针或
   std::function），否则 core 层的异常状态完全不可观测。
 */
