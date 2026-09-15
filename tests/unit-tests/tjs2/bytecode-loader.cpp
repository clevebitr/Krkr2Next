// TJS2 字节码加载器的头部校验回归。
//
// 字节码直接来自游戏包，长度字段全部不可信：这里构造一批"头部齐全、只把某个
// 长度字段改坏"的缓冲区，断言加载器一律拒绝（返回 nullptr）。改动
// tjsByteCodeLoader.cpp 的边界校验时，这些用例应当一起失败。
//
// 每个用例都在创建 ScriptBlock **之前**返回，所以 owner 传 nullptr 是安全的，
// 也就不需要真的初始化 TJS 引擎——测试因此能在 CI 的 Linux 宿主上跑。
// ReadObjects 内部的越界校验要走到需要真引擎，不在这里覆盖。
//
// 文件布局（little endian）：
//   0  TJS2      文件标签
//   4  100\0     版本
//   8  filesize  必须等于缓冲区实际长度
//   12 DATA      数据区标签
//   16 datasize  数据区长度（含 DATA 标签起的 8 字节头）
//   20 数据区    七张表，每张以一个 int32 count 开头
//   12+datasize OBJS 标签
//   +4          objsize
//   +4          对象区

#include "tjsCommHead.h"
#include "tjsByteCodeLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    constexpr std::uint32_t kFileTag =
        ('T') | ('J' << 8) | ('S' << 16) | ('2' << 24);
    constexpr std::uint32_t kVerTag = ('1') | ('0' << 8) | ('0' << 16);
    constexpr std::uint32_t kDataTag =
        ('D') | ('A' << 8) | ('T' << 16) | ('A' << 24);
    constexpr std::uint32_t kObjTag =
        ('O') | ('B' << 8) | ('J' << 16) | ('S' << 24);

    void put32(std::vector<std::uint8_t> &b, size_t off, std::uint32_t v) {
        for(size_t i = 0; i < 4; i++)
            b[off + i] = (std::uint8_t)((v >> (i * 8)) & 0xFF);
    }

    std::vector<std::uint8_t> makeHeader(std::uint32_t total,
                                         std::uint32_t dataSize) {
        std::vector<std::uint8_t> b(total, 0);
        put32(b, 0, kFileTag);
        put32(b, 4, kVerTag);
        put32(b, 8, total); // 加载器要求这里等于缓冲区实际长度
        put32(b, 12, kDataTag);
        put32(b, 16, dataSize);
        return b;
    }

    bool rejected(const std::vector<std::uint8_t> &b) {
        TJS::tTJSByteCodeLoader loader;
        return loader.ReadByteCode(nullptr, TJS_W("test.bin"), b.data(),
                                   b.size()) == nullptr;
    }

} // namespace

TEST_CASE("bytecode: 头部不完整一律拒绝", "[tjs2][bytecode]") {
    REQUIRE(rejected({}));
    REQUIRE(rejected(std::vector<std::uint8_t>(19, 0)));
    // 正好 20 字节：头部够，数据区读不到第一个 count
    REQUIRE(rejected(makeHeader(20, 0)));
}

TEST_CASE("bytecode: 标签或版本不符", "[tjs2][bytecode]") {
    auto b = makeHeader(64, 24);
    put32(b, 0, 0x30303030u); // 文件标签
    REQUIRE(rejected(b));

    b = makeHeader(64, 24);
    put32(b, 4, 0); // 版本
    REQUIRE(rejected(b));

    b = makeHeader(64, 24);
    put32(b, 12, 0); // DATA 标签
    REQUIRE(rejected(b));
}

TEST_CASE("bytecode: 文件大小与实际长度不符", "[tjs2][bytecode]") {
    auto b = makeHeader(64, 24);
    put32(b, 8, 128); // 声明比实际大
    REQUIRE(rejected(b));

    put32(b, 8, 32); // 声明比实际小
    REQUIRE(rejected(b));
}

TEST_CASE("bytecode: 数据区长度越界", "[tjs2][bytecode]") {
    // 负数（最高位置位）
    REQUIRE(rejected(makeHeader(64, 0x80000000u)));
    // 大到装不下
    REQUIRE(rejected(makeHeader(64, 0x7FFFFFFFu)));
    // 长度本身装得下，但数据区第一张表的 count 越界
    auto b = makeHeader(64, 40);
    put32(b, 20, 0x40000000u);
    REQUIRE(rejected(b));
}

TEST_CASE("bytecode: OBJS 区长度越界", "[tjs2][bytecode]") {
    // 数据区 28 字节（七张表 count 全 0）→ datasize=36 → OBJS 在 48
    auto b = makeHeader(64, 36);
    put32(b, 48, kObjTag);
    put32(b, 52, 0x7FFFFFFFu); // 对象区声明长度超出缓冲区
    REQUIRE(rejected(b));
}

TEST_CASE("bytecode: OBJS 标签错", "[tjs2][bytecode]") {
    auto b = makeHeader(64, 36);
    put32(b, 48, 0);
    REQUIRE(rejected(b));
}

TEST_CASE("bytecode: 对象区被截断", "[tjs2][bytecode]") {
    auto b = makeHeader(64, 36);
    put32(b, 48, kObjTag);
    put32(b, 52, 16); // 56 + 16 = 72 > 64
    REQUIRE(rejected(b));
}
