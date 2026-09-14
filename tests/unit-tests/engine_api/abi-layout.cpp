/**
 * @file abi-layout.cpp
 * @brief engine_api.h 的取值与内存布局回归守卫
 *
 * C ABI 是对外壳的稳定契约。Kotlin 侧 dev.kirinext.core.NativeEngine 与
 * EngineSession.InputEvent **硬编码了下面的枚举取值**（JNI 只传裸 int，编译器
 * 无法替我们检查）。改了 C 头而没同步 Kotlin，不会编译报错，只会在运行时
 * 表现为"事件发出去没反应"或"状态判断永远不成立"。
 *
 * 本测试把这些取值钉住：任何改动都会在这里失败，逼开发者去同步外壳。
 *
 * 只依赖 engine_api.h，不链接引擎——配置快、无 vcpkg 依赖。
 */

#include <catch2/catch_all.hpp>

#include <cstddef>
#include <type_traits>

#include "engine_api.h"

// ---------------------------------------------------------------------------
// 版本
// ---------------------------------------------------------------------------

TEST_CASE("C ABI 版本常量固定", "[abi]") {
    // 改这个值意味着对外壳的破坏性变更：外壳会用它做兼容性判断
    STATIC_REQUIRE(ENGINE_API_VERSION == 0x01000000u);

    // 版本号编码约定：major(8) | minor(8) | patch(16)
    STATIC_REQUIRE(ENGINE_API_MAKE_VERSION(1, 0, 0) == 0x01000000u);
    STATIC_REQUIRE(ENGINE_API_MAKE_VERSION(0, 1, 0) == 0x00010000u);
}

// ---------------------------------------------------------------------------
// 枚举取值
// ---------------------------------------------------------------------------

TEST_CASE("engine_result_t 取值固定", "[abi]") {
    STATIC_REQUIRE(ENGINE_RESULT_OK == 0);
    STATIC_REQUIRE(ENGINE_RESULT_INVALID_ARGUMENT == -1);
    STATIC_REQUIRE(ENGINE_RESULT_INVALID_STATE == -2);
    STATIC_REQUIRE(ENGINE_RESULT_NOT_SUPPORTED == -3);
    STATIC_REQUIRE(ENGINE_RESULT_IO_ERROR == -4);
    STATIC_REQUIRE(ENGINE_RESULT_INTERNAL_ERROR == -5);

    // 约定：0 为成功，负值为错误。外壳依赖这一点判断成败。
    STATIC_REQUIRE(ENGINE_RESULT_OK == 0);
}

TEST_CASE("输入事件类型取值固定", "[abi][input]") {
    // 对应 Kotlin 侧 EngineSession.InputEvent 的常量。
    // 取值即引擎内部 kEngineInput* 的分派依据，改动会导致事件被丢弃。
    STATIC_REQUIRE(ENGINE_INPUT_EVENT_POINTER_DOWN == 1);
    STATIC_REQUIRE(ENGINE_INPUT_EVENT_POINTER_MOVE == 2);
    STATIC_REQUIRE(ENGINE_INPUT_EVENT_POINTER_UP == 3);
    STATIC_REQUIRE(ENGINE_INPUT_EVENT_POINTER_SCROLL == 4);
    STATIC_REQUIRE(ENGINE_INPUT_EVENT_KEY_DOWN == 5);
    STATIC_REQUIRE(ENGINE_INPUT_EVENT_KEY_UP == 6);
    STATIC_REQUIRE(ENGINE_INPUT_EVENT_TEXT_INPUT == 7);
    STATIC_REQUIRE(ENGINE_INPUT_EVENT_BACK == 8);
}

TEST_CASE("启动状态取值固定", "[abi][startup]") {
    // 对应 Kotlin 侧 NativeEngine.STARTUP_*。外壳轮询这个值做进度覆盖层，
    // 并把 SUCCEEDED 当作"首帧已出"的判据。
    STATIC_REQUIRE(ENGINE_STARTUP_STATE_IDLE == 0);
    STATIC_REQUIRE(ENGINE_STARTUP_STATE_RUNNING == 1);
    STATIC_REQUIRE(ENGINE_STARTUP_STATE_SUCCEEDED == 2);
    STATIC_REQUIRE(ENGINE_STARTUP_STATE_FAILED == 3);
}

// ---------------------------------------------------------------------------
// 结构体布局
//
// 以下断言限定 LP64（Android arm64-v8a 与 Linux x86_64 都是）。本项目只构建
// 这两个目标；若将来加入 32 位 ABI，这里需要按平台分支。
// ---------------------------------------------------------------------------

TEST_CASE("engine_input_event_t 字段布局固定", "[abi][input]") {
    REQUIRE(sizeof(void *) == 8); // 本测试仅对 LP64 有效

    STATIC_REQUIRE(sizeof(engine_input_event_t) == 104);

    // 字段顺序即 ABI。JNI 包装虽按字段逐个传参，但 Dart 时代的 FFI 调用方与
    // 引擎内部 struct_size 校验都依赖它是这个顺序。
    CHECK(offsetof(engine_input_event_t, struct_size) == 0);
    CHECK(offsetof(engine_input_event_t, type) == 4);
    CHECK(offsetof(engine_input_event_t, timestamp_micros) == 8);
    CHECK(offsetof(engine_input_event_t, x) == 16);
    CHECK(offsetof(engine_input_event_t, y) == 24);
    CHECK(offsetof(engine_input_event_t, delta_x) == 32);
    CHECK(offsetof(engine_input_event_t, delta_y) == 40);
    CHECK(offsetof(engine_input_event_t, pointer_id) == 48);
    CHECK(offsetof(engine_input_event_t, button) == 52);
    CHECK(offsetof(engine_input_event_t, key_code) == 56);
    CHECK(offsetof(engine_input_event_t, modifiers) == 60);
    CHECK(offsetof(engine_input_event_t, unicode_codepoint) == 64);
    CHECK(offsetof(engine_input_event_t, reserved_u32) == 68);
    CHECK(offsetof(engine_input_event_t, reserved_u64) == 72);
    CHECK(offsetof(engine_input_event_t, reserved_ptr) == 88);
}

TEST_CASE("engine_create_desc_t 字段布局固定", "[abi]") {
    REQUIRE(sizeof(void *) == 8);

    // struct_size 必须是首个字段：引擎靠它做前向兼容判断
    CHECK(offsetof(engine_create_desc_t, struct_size) == 0);
    CHECK(offsetof(engine_create_desc_t, api_version) == 4);
    CHECK(offsetof(engine_create_desc_t, writable_path_utf8) == 8);
    CHECK(offsetof(engine_create_desc_t, cache_path_utf8) == 16);
    CHECK(offsetof(engine_create_desc_t, user_data) == 24);
    CHECK(offsetof(engine_create_desc_t, reserved_u64) == 32);
    CHECK(offsetof(engine_create_desc_t, reserved_ptr) == 64);
}

TEST_CASE("engine_option_t 字段布局固定", "[abi]") {
    REQUIRE(sizeof(void *) == 8);

    CHECK(offsetof(engine_option_t, key_utf8) == 0);
    CHECK(offsetof(engine_option_t, value_utf8) == 8);
    CHECK(offsetof(engine_option_t, reserved_u64) == 16);
    CHECK(offsetof(engine_option_t, reserved_ptr) == 32);
}

TEST_CASE("带版本号的调用方结构体以 struct_size 开头", "[abi]") {
    // 约定：所有"调用方先初始化"的结构体都以 uint32_t struct_size 打头，
    // 引擎据此区分新旧调用方。此约定一旦破坏，旧外壳会在运行时静默错位。
    STATIC_REQUIRE(std::is_same<decltype(engine_input_event_t::struct_size),
                                uint32_t>::value);
    STATIC_REQUIRE(std::is_same<decltype(engine_create_desc_t::struct_size),
                                uint32_t>::value);
    STATIC_REQUIRE(
        std::is_same<decltype(engine_option_t::key_utf8), const char *>::value);
}
