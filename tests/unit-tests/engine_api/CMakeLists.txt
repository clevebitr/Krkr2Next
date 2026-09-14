# C ABI 布局与取值回归守卫。只依赖 engine_api.h，不链接引擎——因此配置快、
# 不需要 vcpkg 依赖，是最廉价的一层防线。
add_executable(engine-api-abi abi-layout.cpp)

target_link_libraries(engine-api-abi PRIVATE Catch2::Catch2WithMain)

target_include_directories(engine-api-abi PRIVATE
    "${CMAKE_SOURCE_DIR}/bridge/engine_api/include"
)

catch_discover_tests(engine-api-abi)
