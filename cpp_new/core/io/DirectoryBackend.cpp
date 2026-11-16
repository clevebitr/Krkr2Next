#include "core/io/DirectoryBackend.h"

#include <cstdio>
#include <fstream>
#include <system_error>

#include "core/io/PathUtil.h"

namespace krkr::io {

    namespace {
        // 只读文件流：读、绝对 seek、报大小。
        class DirectoryStream final : public IStream {
        public:
            static std::unique_ptr<IStream>
            Open(const std::filesystem::path &path, std::string &errorOut) {
                std::error_code ec;

                if(!std::filesystem::exists(path, ec) || ec) {
                    errorOut = ec
                        ? ("cannot stat \"" + PathToUtf8(path) +
                           "\": " + ec.message())
                        : ("no such file: \"" + PathToUtf8(path) + "\"");
                    return nullptr;
                }

                if(std::filesystem::is_directory(path, ec)) {
                    errorOut = "\"" + PathToUtf8(path) +
                        "\" is a directory, not a file";
                    return nullptr;
                }

                const auto size = std::filesystem::file_size(path, ec);
                if(ec) {
                    errorOut = "cannot get size of \"" + PathToUtf8(path) +
                        "\": " + ec.message();
                    return nullptr;
                }
                std::ifstream file(path, std::ios::binary);
                if(!file.is_open()) {
                    errorOut =
                        "cannot open \"" + PathToUtf8(path) + "\" for reading";
                    return nullptr;
                }

                std::unique_ptr<DirectoryStream> stream(new DirectoryStream());
                stream->file_ = std::move(file);
                stream->size_ = static_cast<std::int64_t>(size);
                errorOut.clear();
                return stream;
            }

            std::int64_t Read(void *buffer, std::int64_t bytes) override {
                if(buffer == nullptr || bytes < 0) {
                    return -1;
                }
                if(bytes == 0) {
                    return 0;
                }
                file_.read(static_cast<char *>(buffer),
                           static_cast<std::streamsize>(bytes));
                const auto got = static_cast<std::int64_t>(file_.gcount());
                if(file_.bad()) {
                    return -1;
                }
                position_ += got;
                return got;
            }

            bool Seek(std::int64_t offset) override {
                if(offset < 0 || offset > size_) {
                    return false;
                }

                file_.clear();
                file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
                if(file_.fail()) {
                    return false;
                }
                position_ = offset;
                return true;
            }

            std::int64_t GetSize() const override { return size_; }
            std::int64_t GetPosition() const override { return position_; }

        private:
            DirectoryStream() = default;

            std::ifstream file_;
            std::int64_t size_ = 0;
            std::int64_t position_ = 0;
        };

    } // namespace

    std::unique_ptr<IStream>
    OpenDirectoryFile(const std::filesystem::path &path,
                      std::string &errorOut) {
        return DirectoryStream::Open(path, errorOut);
    }

} // namespace krkr::io
