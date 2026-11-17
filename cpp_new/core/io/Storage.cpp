#include "core/io/Storage.h"

#include <cstddef>
#include <filesystem>
#include <system_error>

#include "core/io/DirectoryBackend.h"
#include "core/io/IoName.h"
#include "core/io/PathUtil.h"
#include "core/io/archive/Xp3Index.h"
#include "core/io/archive/Xp3Stream.h"

namespace krkr::io {

    class Storage::Impl {
    public:
        std::filesystem::path root;
        std::string rootUtf8;
        std::string lastError;
        bool mounted = false;
        std::unique_ptr<Xp3Index> index;
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

        if(std::filesystem::is_regular_file(impl_->root, ec) && !ec) {
            auto index = std::make_unique<Xp3Index>();
            if(!index->Load(rootUtf8)) {
                impl_->lastError = index->GetLastErrorText();
                return;
            }
            impl_->index = std::move(index);
            impl_->mounted = true;
            impl_->lastError.clear();
            return;
        }

        if(!std::filesystem::is_directory(impl_->root, ec) || ec) {
            impl_->lastError = "root is neither a directory nor an XP3 archive: \"" +
                               rootUtf8 + "\"";
            return;
        }

        impl_->mounted = true;
        impl_->lastError.clear();
    }

    Storage::~Storage() = default;

    bool Storage::Mounted() const { return impl_->mounted; }

    bool Storage::IsArchive() const { return impl_->index != nullptr; }

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

        if(impl_->index != nullptr) {
            const Xp3Entry *entry = impl_->index->Find(nameUtf8);
            if(entry == nullptr) {
                impl_->lastError = "\"" + nameUtf8 +
                                   "\" is not in the archive \"" +
                                   impl_->rootUtf8 + "\"";
                return nullptr;
            }
            std::string error;
            auto stream = Xp3Stream::Open(impl_->root, *entry, error);
            impl_->lastError = error;
            return stream;
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
