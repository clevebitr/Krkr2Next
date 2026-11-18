#include "core/log/Logger.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

namespace krkr::log {
    constexpr std::size_t kDefaultMaxFileBytes = 4u * 1024u * 1024u;
    constexpr int kRotateFiles = 3;

    constexpr std::size_t kRingCapacity = 256;

    struct Logger::Impl {
        mutable std::mutex mutex;
        Level level = Level::Info;
        std::uint64_t frame = 0;
        std::size_t maxFileBytes = kDefaultMaxFileBytes;

        std::FILE *file = nullptr;
        std::size_t fileBytes = 0;
        std::string filePath;
        std::string fileError;

        std::string pending;

        std::deque<std::string> ring;

        void PushRing(const std::string &line) {
            ring.push_back(line);
            while(ring.size() > kRingCapacity) {
                ring.pop_front();
            }
        }

        static std::string RotatePath(const std::string &base, int index) {
            return base + "." + std::to_string(index);
        }

        void CloseFile() {
            if(file != nullptr) {
                std::fclose(file);
                file = nullptr;
            }
        }

        bool OpenFile() {
            CloseFile();
            fileBytes = 0;
            if(filePath.empty()) {
                return true;
            }

            file = std::fopen(filePath.c_str(), "ab");
            if(file == nullptr) {
                fileError =
                    "cannot open log file \"" + filePath + "\" for append";
                return false;
            }

            if(std::fseek(file, 0, SEEK_END) == 0) {
                const long size = std::ftell(file);
                fileBytes = size > 0 ? static_cast<std::size_t>(size) : 0;
            }

            fileError.clear();
            return true;
        }

        void Rotate() {
            CloseFile();
            std::remove(RotatePath(filePath, kRotateFiles).c_str());
            for(int index = kRotateFiles - 1; index >= 1; --index) {
                std::rename(RotatePath(filePath, index).c_str(),
                            RotatePath(filePath, index + 1).c_str());
            }
            std::rename(filePath.c_str(), RotatePath(filePath, 1).c_str());
            OpenFile();
        }

        std::string ComposeLine(Level lineLevel,
                                const std::string &message) const {
            std::string line = "[frame=";
            line += std::to_string(frame);
            line += "][";
            line.append(LevelName(lineLevel));
            line += "] ";
            line.append(message);
            line.push_back('\n');
            return line;
        }

        void FlushLocked() {
            if(pending.empty()) {
                return;
            }
            std::fwrite(pending.data(), 1, pending.size(), stdout);
            std::fflush(stdout);

            if(file != nullptr) {
                if(fileBytes > 0 && fileBytes + pending.size() > maxFileBytes) {
                    Rotate();
                }
                if(file != nullptr) {
                    fileBytes +=
                        std::fwrite(pending.data(), 1, pending.size(), file);
                    std::fflush(file);
                }
            }
            pending.clear();
        }
    };

    Logger::Logger() : impl_(std::make_unique<Impl>()) {}

    Logger &Logger::Instance() {
        static Logger instance;
        return instance;
    }

    Logger::~Logger() {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->FlushLocked();
        impl_->CloseFile();
    }

    void Logger::SetLevel(Level level) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->level = level;
    }

    Level Logger::GetLevel() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->level;
    }

    bool Logger::IsEnabled(Level level) const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return static_cast<std::int32_t>(level) >=
            static_cast<std::int32_t>(impl_->level);
    }

    bool Logger::SetFile(const std::string &pathUtf8) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->FlushLocked();
        impl_->CloseFile();
        impl_->filePath = pathUtf8;
        return impl_->OpenFile();
    }

    const std::string &Logger::GetFileError() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->fileError;
    }

    const std::string &Logger::GetFilePath() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->filePath;
    }

    void Logger::SetMaxFileBytes(std::size_t bytes) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->maxFileBytes = bytes;
    }

    void Logger::SetFrame(std::uint64_t frame) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->frame = frame;
    }

    std::uint64_t Logger::GetFrame() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->frame;
    }

    void Logger::Write(Level level, const std::string &message) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if(static_cast<std::int32_t>(level) <
           static_cast<std::int32_t>(impl_->level)) {
            return;
        }
        const std::string line = impl_->ComposeLine(level, message);
        impl_->pending += line;
        impl_->PushRing(line);

        if(level == Level::Warn || level == Level::Error) {
            impl_->FlushLocked();
        }
    }

    void Logger::Flush() {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->FlushLocked();
    }

    std::size_t Logger::Drain(char *out, std::size_t capacity) {
        if(out == nullptr || capacity == 0) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(impl_->mutex);
        std::size_t written = 0;
        while(!impl_->ring.empty()) {
            const std::string &line = impl_->ring.front();
            if(written + line.size() > capacity) {
                break;
            }
            std::memcpy(out + written, line.data(), line.size());
            written += line.size();
            impl_->ring.pop_front();
        }
        return written;
    }

    std::string Logger::GetRecentLines(std::size_t maxLines) const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const std::size_t count = maxLines < impl_->ring.size() ? maxLines : impl_->ring.size();
        std::string out;
        for(std::size_t i = impl_->ring.size() - count; i < impl_->ring.size(); ++i) {
            out += impl_->ring[i];
        }
        return out;
    }
} // namespace krkr::log