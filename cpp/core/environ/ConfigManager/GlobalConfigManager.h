#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <map>

//---------------------------------------------------------------------------
// 壳下发的引擎选项覆盖（`engine_set_option`）
//---------------------------------------------------------------------------
// 为什么需要单独一条通道：壳设的键目前只落进**命令行参数**（`TVPProgramArguments`），
// 而渲染相关键（`ogl_compress_tex` / `ogl_accurate_render` / `ogl_max_texsize` /
// `memusage`）是通过 `IndividualConfigManager::GetValue` 从 `Kirikiroid2Preference.xml`
// / 全局配置读的 —— 两条路互不相通，所以壳设了也不生效（日志 `Specified option(s)`
// 里有值，渲染器仍用默认）。
//
// 为什么不用 `SetValue()` 直接写进 `AllConfig`：`UsePreferenceAt(游戏目录)` 会先
// `Clear()` 再 `Initialize()`，壳在开游戏前设的值会被整份清掉。所以用一张独立的、
// 不随配置文件读写的表，且**优先级最高**（壳显式设的就是"这次启动要用的值"）。
void TVPSetShellOption(const std::string &name, const std::string &value);
bool TVPGetShellOption(const std::string &name, std::string &out);
void TVPClearShellOptions();

class iSysConfigManager {
protected:
    std::unordered_map<std::string, std::string> AllConfig;
    std::vector<std::pair<std::string, std::string>> CustomArguments;
    std::map<int, int> KeyMap;

    bool ConfigUpdated{};

    virtual std::string GetFilePath() = 0;

    void Initialize();

public:
    void SaveToFile();

    bool IsValueExist(const std::string &name);

    template <typename T>
    T GetValue(const std::string &name, const T &defVal);

    void SetValueInt(const std::string &name, int val);
    void SetValueFloat(const std::string &name, float val);
    void SetValue(const std::string &name, const std::string &val);

    std::vector<std::pair<std::string, std::string>> &
    GetCustomArgumentsForModify() {
        ConfigUpdated = true;
        return CustomArguments;
    }

    const std::map<int, int> &GetKeyMap() { return KeyMap; }
    void SetKeyMap(int k, int v /* 0 means remove */);

    [[nodiscard]] const std::vector<std::pair<std::string, std::string>> &
    GetCustomArguments() const {
        return CustomArguments;
    }

    std::vector<std::string> GetCustomArgumentsForPush();
};

template <>
bool iSysConfigManager::GetValue<bool>(const std::string &name,
                                       const bool &defVal);
template <>
int iSysConfigManager::GetValue<int>(const std::string &name,
                                     const int &defVal);
template <>
float iSysConfigManager::GetValue<float>(const std::string &name,
                                         const float &defVal);
template <>
std::string iSysConfigManager::GetValue<std::string>(const std::string &name,
                                                     const std::string &defVal);

class GlobalConfigManager : public iSysConfigManager {

    std::string GetFilePath() override;

    GlobalConfigManager();

public:
    static GlobalConfigManager *GetInstance();
};