#include "core/io/Storage.h"

#include <cstddef>
#include <filesystem>
#include <system_error>

#include "core/io/DirectoryBackend.h"
#include "core/io/IoName.h"
#include "core/io/PathUtil.h"

namespace krkr::io {

    class Storage::Impl {
    public:
        std::filesystem::path root;
        std::string rootUtf8;
        std::string lastError;
        bool mounted = false;
    };

    Storage::Storage(const std::string &rootUtf8) :
        impl_(std::make_unique<Impl>()) {
        impl_->rootUtf8 = rootUtf8;
        impl_->root = PathFromUtf8(rootUtf8);
        if(impl_->root.empty()) {
            impl_->lastError = "root is not valid UTF-8: \"" + rootUtf8 + "\"";
            return;
        }

        std::error_code ec;
        if(!std::filesystem::exists(impl_->root, ec) || ec) {
            impl_->lastError = ec
                ? ("cannot stat root \"" + rootUtf8 + "\": " + ec.message())
                : ("root does not exist: \"" + rootUtf8 + "\"");
            return;
        }
        if(!std::filesystem::is_directory(impl_->root, ec) || ec) {
            impl_->lastError = "root is not a directory: \"" + rootUtf8 + "\"";
            return;
        }

        impl_->mounted = true;
        impl_->lastError.clear();
    }

    Storage::~Storage() = default;

    bool Storage::Mounted() const { return impl_->mounted; }

    std::unique_ptr<IStream> Storage::Open(const std::string &nameUtf8) {
        if(!impl_->mounted) {
            impl_->lastError = "storage is not mounted";
            return nullptr;
        }

        const std::string name = NormalizeStorageName(nameUtf8);
        if(name.empty()) {
            impl_->lastError = "empty file name";
            return nullptr;
        }
        const std::filesystem::path namePath = PathFromUtf8(name);
        if(namePath.empty()) {
            impl_->lastError = "file name is not valid UTF-8: \"" + nameUtf8 +
                               "\"";
            return nullptr;
        }

        const std::filesystem::path fullPath = impl_->root / namePath;

        std::string error;
        auto stream = OpenDirectoryFile(fullPath, error);
        impl_->lastError = error;
        return stream;
    }

    const std::string &Storage::GetRootUtf8() const { return impl_->rootUtf8; }

    const char *Storage::GetLastErrorText() const {
        return impl_->lastError.c_str();
    }

} // namespace krkr::io
