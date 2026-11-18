#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/io/IOStream.h"
#include "core/contracts/DebugServices.h"

namespace krkr::io {
    class Storage final {
    public:
        explicit Storage(const std::string &rootUtf8);
        ~Storage();

        Storage(const Storage &) = delete;
        Storage &operator=(const Storage &) = delete;

        bool Mounted() const;

        bool IsArchive() const;

        std::unique_ptr<IStream> Open(const std::string &nameUtf8);

        const std::string &GetRootUtf8() const;

        // 调试器挂接点：为空时不产生任何事件。仅用于观测，不改变打开语义。
        void SetDebugSink(krkr::debug::IDebugSink *sink);

        // 挂载表快照，每条形如 "xp3 <路径> (N entries)" 或 "dir <路径>"。
        std::vector<std::string> DescribeMounts() const;
        const char *GetLastErrorText() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace krkr::io
