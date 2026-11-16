#include "core/env/app/EngineRuntime.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

namespace krkr::env {

  // Game path handling
  namespace {
    std::filesystem::path PathFromUtf8(const std::string &utf8) {
      const auto *raw = reinterpret_cast<const char8_t *>(utf8.data());
      return std::filesystem::path(std::u8string(raw, utf8.size()));
    }

    std::string PathToUtf8(const std::filesystem::path &path) {
      const std::u8string u8 = path.generic_u8string();
      return std::string(reinterpret_cast<const char *>(u8.data()),u8.size());
    }

    bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix) {
      if (text.size() < suffix.size()) {
        return false;
      }

      text.remove_prefix(text.size() - suffix.size());
      for (std::size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char lhs = static_cast<unsigned char>(text[i]);
        const unsigned char rhs = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
          return false;
        }
      }

      return true;
    }

    bool LooksLikeArchivePath(std::string_view path) {
      constexpr std::string_view kArchiveExtensions[] = { ".xp3", ".zip", ".7z", ".tar" };
      for (const std::string_view extension : kArchiveExtensions) {
        if (EndsWithIgnoreCase(path, extension)) {
          return true;
        }
      }
      return false;
    }
  } // namespace 

  EngineRuntime *EngineRuntime::Create() { return new EngineRuntime(); }

  void EngineRuntime::Destroy(EngineRuntime *engine) { delete engine; }

  EngineResult EngineRuntime::OpenGame(const char *gameRootUtf8) {
    if (gameRootUtf8 == nullptr || gameRootUtf8[0] == '\0') {
      lastError_ = "game root is empty";
      return EngineResult::InvalidArgument;
    }
    if (gameOpen_) {
      lastError_ = "game already open";
      return EngineResult::InvalidState;
    }

    const std::string requested = gameRootUtf8;
    const std::filesystem::path requestedPath = PathFromUtf8(requested);

    std::error_code ec;

    const bool exists = std::filesystem::exists(requestedPath, ec);
    
    if (ec) {
      lastError_ = "cannot stat \"" + requested + "\"" + ec.message();
      return EngineResult::IoError;
    }

    if (!exists) {
      lastError_ = "\"" + requested + "\" does not exists";
      return EngineResult::IoError;
    }

    std::filesystem::path resolved = std::filesystem::weakly_canonical(requestedPath, ec);
    if (ec) {
      ec.clear();
      resolved = std::filesystem::absolute(requestedPath, ec);
      if (ec) {
        ec.clear();
        resolved = requestedPath;
      }
    }

    GameKind kind = GameKind::None;
    const bool isDirectory = std::filesystem::is_directory(resolved, ec);
    if (ec) {
      lastError_ = "cannot inspect \"" + requested + "\": " + ec.message();
      return EngineResult::IoError;
    }

    if (isDirectory) {
      kind = GameKind::Directory;
    } else if (LooksLikeArchivePath(PathToUtf8(resolved))) {
      kind = GameKind::Archive;
    }
    if (kind == GameKind::None) {
      lastError_ = "\"" + requested + "\" is neither a directory nor a known archive (.xp3/.zip/.7z/.tar)";
      return EngineResult::IoError;
    }

    std::string normalized = PathToUtf8(resolved);
    if (kind == GameKind::Directory && (normalized.empty() || normalized.back() != '/')) {
      normalized.push_back('/');
    }

    gameRoot_ = std::move(normalized);
    gameKind_ = kind;
    gameOpen_ = true;
    lastError_.clear();
    return EngineResult::Ok;
  }

  EngineResult EngineRuntime::Tick(double deltaSeconds) {
    if (!gameOpen_) {
      lastError_ = "Tick before OpenGame";
      return EngineResult::InvalidState;
    }
    if (deltaSeconds < 0.0) {
      lastError_ = "negative delta";
      return EngineResult::InvalidArgument;
    }
    ++frameCount_;
    return EngineResult::Ok;
  }
} // namespace krkr::env
