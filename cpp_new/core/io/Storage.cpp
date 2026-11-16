#include "core/io/Storage.h"

#include <cstddef>
#include <filesystem>
#include <system_error>

#include "core/io/DirectoryBackend.h"
#include "core/io/PathUtil.h"

namespace krkr::io {

    namespace {
        std::string NormalizeName(const std::string &rawName) {
            std::string slashed;
            slashed.reserve(rawName.size());
            for(const char ch : rawName) {
                slashed.push_back(ch == '\\' ? '/' : ch);
            }

            std::string cleaned;
            cleaned.reserve(slashed.size());
            for(const char ch : slashed) {
                if(ch == '/' && !cleaned.empty() && cleaned.back() == '/') {
                    continue;
                }
                cleaned.push_back(ch);
            }

            while(cleaned.size() >= 2 && cleaned[0] == '.' &&
                  cleaned[1] == '/') {
                cleaned.erase(0, 2);
            }
            return cleaned;
        }

    } // namespace

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

        const std::string name = NormalizeName(nameUtf8);
        if(name.empty()) {
            impl_->lastError = "empty file name";
            return nullptr;
        }

        const std::filesystem::path fullPath = impl_->root / PathFromUtf8(name);

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
