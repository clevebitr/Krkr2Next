#include "IndividualConfigManager.h"
#include "LocaleConfigManager.h"
#include "Platform.h"

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace {
    // 壳下发的选项覆盖（见 GlobalConfigManager.h 的说明）。独立于 AllConfig，
    // 因此 `UsePreferenceAt()` 的 Clear()/Initialize() 不会把它清掉。
    std::mutex &ShellOptionsMutex() {
        static std::mutex mtx;
        return mtx;
    }
    std::unordered_map<std::string, std::string> &ShellOptions() {
        static std::unordered_map<std::string, std::string> map;
        return map;
    }
    bool LookupShellOption(const std::string &name, std::string &out) {
        std::lock_guard<std::mutex> lock(ShellOptionsMutex());
        auto it = ShellOptions().find(name);
        if(it == ShellOptions().end())
            return false;
        out = it->second;
        return true;
    }
} // namespace

void TVPSetShellOption(const std::string &name, const std::string &value) {
    if(name.empty())
        return;
    std::lock_guard<std::mutex> lock(ShellOptionsMutex());
    ShellOptions()[name] = value;
}

bool TVPGetShellOption(const std::string &name, std::string &out) {
    return LookupShellOption(name, out);
}

void TVPClearShellOptions() {
    std::lock_guard<std::mutex> lock(ShellOptionsMutex());
    ShellOptions().clear();
}

#define FILENAME "Kirikiroid2Preference.xml"

IndividualConfigManager *IndividualConfigManager::GetInstance() {
    static IndividualConfigManager instance;
    return &instance;
}

std::string IndividualConfigManager::GetFilePath() { return CurrentPath; }

void IndividualConfigManager::Clear() {
    AllConfig.clear();
    CustomArguments.clear();
    ConfigUpdated = false;
    CurrentPath.clear();
}

bool IndividualConfigManager::CheckExistAt(const std::string &folder) {
    std::string fullpath = folder + "/" FILENAME;
    std::error_code ec;
    return std::filesystem::exists(fullpath, ec);
}

bool IndividualConfigManager::CreatePreferenceAt(const std::string &folder) {
    std::string fullpath = folder + "/" FILENAME;
    // 	FILE *fp =
    // #ifdef _MSC_VER
    // 		_wfopen(ttstr(fullpath).c_str(), TJS_W("w"));
    // #else
    // 		fopen(fullpath.c_str(), "w");
    // #endif
    Clear();
    // 	if (!fp) {
    // 		TVPShowSimpleMessageBox(
    // 			LocaleConfigManager::GetInstance()->GetText("cannot_create_preference"),
    // 			LocaleConfigManager::GetInstance()->GetText("readonly_storage"));
    // 		return false;
    // 	}
    CurrentPath = fullpath;
    return true;
}

bool IndividualConfigManager::UsePreferenceAt(const std::string &folder) {
    std::string fullpath = folder + "/" FILENAME;
    if(CurrentPath == fullpath)
        return true;
    Clear();
    std::error_code ec;
    if(!std::filesystem::exists(fullpath, ec))
        return false;
    CurrentPath = fullpath;
    Initialize();
    return true;
}

template <>
bool IndividualConfigManager::GetValue<bool>(const std::string &name,
                                             const bool &defVal /*= false*/) {
    // 壳下发的值优先（见 GlobalConfigManager.h 的说明）。
    std::string raw;
    if(LookupShellOption(name, raw)) {
        return raw == "1" || raw == "true" || raw == "yes" || raw == "on";
    }
    return inherit::GetValue<bool>(
        name, GlobalConfigManager::GetInstance()->GetValue<bool>(name, defVal));
}
template <>
int IndividualConfigManager::GetValue<int>(const std::string &name,
                                           const int &defVal /*= 0*/) {
    std::string raw;
    if(LookupShellOption(name, raw)) {
        return std::atoi(raw.c_str());
    }
    return inherit::GetValue<int>(
        name, GlobalConfigManager::GetInstance()->GetValue<int>(name, defVal));
}
template <>
float IndividualConfigManager::GetValue<float>(const std::string &name,
                                               const float &defVal /*= 0*/) {
    std::string raw;
    if(LookupShellOption(name, raw)) {
        return static_cast<float>(std::atof(raw.c_str()));
    }
    return inherit::GetValue<float>(
        name,
        GlobalConfigManager::GetInstance()->GetValue<float>(name, defVal));
}
template <>
std::string IndividualConfigManager::GetValue<std::string>(
    const std::string &name, const std::string &defVal /*= ""*/) {
    std::string raw;
    if(LookupShellOption(name, raw)) {
        return raw;
    }
    return inherit::GetValue<std::string>(
        name,
        GlobalConfigManager::GetInstance()->GetValue<std::string>(name,
                                                                  defVal));
}

std::vector<std::string> IndividualConfigManager::GetCustomArgumentsForPush() {
    if(CustomArguments.empty()) {
        return GlobalConfigManager::GetInstance()->GetCustomArgumentsForPush();
    }
    return inherit::GetCustomArgumentsForPush();
}
