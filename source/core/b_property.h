#pragma once

/// 属性面板落地前必须有

/**
 属性反射 / Property 系统

目前每个组件要么手写 to_json/from_json（DocumentSerializer），
要么完全无反射。属性面板（未来 GUI 层必然要做）、脚本绑定、Undo 
的字段级 diff 都依赖某种"知道一个组件有哪些字段、每个字段的类型/
范围/默认值"的元信息。建议一个轻量、非侵入式的方案（类似 component_name<T> 
的 traits 模式），不引入完整反射框架的重量级依赖。
 */
