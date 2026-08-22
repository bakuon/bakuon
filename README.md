# Bakuon

TODO: 编辑项目描述


## Features

TODO：编辑功能描述

## Usage

* CMake 编译

```shell
# 日常开发：默认行为不变（tests/examples/standalone 全部构建）
cmake -S . -B build

# 只想集成 core+gui+plugin 三个库到别的项目里（比如被上层工程 add_subdirectory）
cmake -S . -B build -DBAKUON_BUILD_TESTS=OFF -DBAKUON_BUILD_EXAMPLES=OFF -DBAKUON_BUILD_STANDALONE=OFF

# core 目录补齐实现代码后
cmake -S . -B build -DBAKUON_BUILD_CORE=ON

# 想把警告当错误处理，尽早暴露隐患（比如 CI）
cmake -S . -B build -DBAKUON_WARNINGS_AS_ERRORS=ON
```

## Install

TODO：编辑安装描述

## Plugin

TODO：编辑开发描述

## FQA
