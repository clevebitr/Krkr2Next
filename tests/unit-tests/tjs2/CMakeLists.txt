# TJS2 字节码加载器的边界校验守卫。字节码来自游戏包，长度字段全部不可信。
#
# tjs2 是自包含的静态库（不引用 base/ 的符号），只链它即可拿到 public include
# 与 Boost/fmt/spdlog/oniguruma 依赖，测试不需要引擎的其余部分。用例只走
# "拒绝"路径，因此不初始化 TJS 引擎。
add_executable(tjs2-bytecode-loader bytecode-loader.cpp)

target_link_libraries(tjs2-bytecode-loader PRIVATE Catch2::Catch2WithMain tjs2)

catch_discover_tests(tjs2-bytecode-loader)
