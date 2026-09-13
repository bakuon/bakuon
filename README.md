# Bakuon

TODO: 编辑项目描述


## Features

TODO：编辑功能描述

## Usage

* CMake 编译

```bash
# 日常开发：默认行为不变（tests/examples/standalone 全部构建）
cmake -S . -B build

# 只想集成 core+gui+plugin 三个库到别的项目里（比如被上层工程 add_subdirectory）
cmake -S . -B build -DBAKUON_BUILD_TESTS=OFF -DBAKUON_BUILD_EXAMPLES=OFF -DBAKUON_BUILD_STANDALONE=OFF

# core（基于 EnTT 的实体-组件状态容器）默认已随主构建一起打开；
# 只是想临时关掉它（比如排查问题）时才需要显式传 OFF
cmake -S . -B build -DBAKUON_BUILD_CORE=OFF

# 想把警告当错误处理，尽早暴露隐患（比如 CI）
cmake -S . -B build -DBAKUON_WARNINGS_AS_ERRORS=ON
```

* CTest

查看当前的标签分布:

``` bash
ctest --show-only=json-v1
```

  - 持续集成 (CI)：
  
  分批次/分阶段运行在 GitHub Actions 或 GitLab CI 中，为了最快获得反馈，可以先跑轻量级的 P0 测试，通过后再跑完整的流水线：
  
```bash
# 阶段 1：只运行核心 P0 测试（通常几秒钟内完成）
ctest -L p0 --output-on-failure

# 阶段 2：在没有外部图形环境的 CI 容器中，跳过所有 GUI/Qt 界面测试
ctest -E qt --output-on-failure   # -E 代表排除（Exclude）带有该标签的测试

# 阶段 3：在具备完整沙箱和图形环境的专有 Runner 上，运行所有集成测试
ctest -L integration

```

## Install

TODO：编辑安装描述

## Plugin

TODO：编辑开发描述

## FQA
