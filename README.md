# SQL 数据库实验

基于 CMU BusTub 的数据库实验源码，包含当前完成的实验一实现。

## 实验一

- 可扩展哈希表（Extendible Hash Table）
- LRU-K 页面替换器（LRU-K Replacer）
- 缓冲池管理器（Buffer Pool Manager Instance）

本地验证：哈希表 12 项、LRU-K 4 项、缓冲池管理器 13 项测试全部通过，实验一专用 clang-tidy 检查通过。其他实验尚未在本次工作中完成或验证。

## 构建与测试

当前开发环境为 WSL2 / Ubuntu 22.04，使用 Clang 14、CMake 和 Python 3。首次使用需先安装项目依赖，可参考 `build_support/packages.sh`。

在项目根目录执行：

```bash
bash ./build_experiment.sh --target extendible_hash_table_test lru_k_replacer_test buffer_pool_manager_instance_test

./build/test/extendible_hash_table_test --gtest_also_run_disabled_tests
./build/test/lru_k_replacer_test --gtest_also_run_disabled_tests
./build/test/buffer_pool_manager_instance_test --gtest_also_run_disabled_tests

cmake --build build --target check-clang-tidy-p1
```

构建脚本使用 Debug 模式、AddressSanitizer 和 4 个并行编译任务。

## 仓库内容

包含源码、测试、第三方依赖和构建脚本。编译产物、本机编辑器配置、教程 PDF 和单独提供的参考代码未上传。保留上游项目的 `LICENSE` 及源码版权声明。
