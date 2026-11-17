#pragma once

#include <memory>
#include <string>

#include "core/io/IOStream.h"

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
        const char *GetLastErrorText() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace krkr::io
