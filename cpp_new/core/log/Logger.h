#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <sstream>
#include <utility>

#include "core/log/LogLevel.h"

namespace krkr::log {
    class Logger final {
    public:
      static Logger &Instance();

      Logger(const Logger &) = delete;
      Logger &operator = (const Logger &) = delete;

      void SetLevel(Level level);
      Level GetLevel() const;

      bool IsEnabled(Level level) const;

      bool SetFile(const std::string &pathUtf8);
      const std::string &GetFileError() const;
      const std::string &GetFilePath() const;

      void SetMaxFileBytes(std::size_t bytes);

      void SetFrame(std::uint64_t frame);
      std::uint64_t GetFrame() const;

      std::size_t Drain(char *out, std::size_t capacity);

      void Write(Level level, const std::string &message);
      void Flush();
    
    private:
      Logger();
      ~Logger();

      struct Impl;
      std::unique_ptr<Impl> impl_;
    };

    template <typename ...Args>
    std::string Format(Args &&...args) {
      std::ostringstream out;
      (out << ... << args);
      return out.str();
    }

    template <typename ...Args>
    void Debug(Args &&...args) {
      Logger &logger = Logger::Instance();
      if (!logger.IsEnabled(Level::Debug)) { return; }
      logger.Write(Level::Debug, Format(std::forward<Args>(args)...));
    }

    template <typename ...Args>
    void Info(Args &&...args) {
      Logger &logger = Logger::Instance();
      if (!logger.IsEnabled(Level::Info)) { return; }
      logger.Write(Level::Info, Format(std::forward<Args>(args)...));
    }

    template <typename ...Args>
    void Warn(Args &&...args) {
      Logger &logger = Logger::Instance();
      if (!logger.IsEnabled(Level::Warn)) { return; }
      logger.Write(Level::Warn, Format(std::forward<Args>(args)...));
    }

    template <typename ...Args>
    void Error(Args &&...args) {
      Logger &logger = Logger::Instance();
      if (!logger.IsEnabled(Level::Error)) { return; }
      logger.Write(Level::Error, Format(std::forward<Args>(args)...));
    }

} // namespace krkr::log