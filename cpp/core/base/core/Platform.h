#pragma once
#include "tjsCommHead.h"
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

struct MemoryInfo { // all in kB
    unsigned long MemTotal;
    unsigned long MemFree;
    unsigned long SwapTotal;
    unsigned long SwapFree;
    unsigned long VirtualTotal;
    unsigned long VirtualUsed;
};

void GetMemoryInfo(MemoryInfo &m);
tjs_int GetSystemFreeMemory(); // in MB
tjs_int GetSelfUsedMemory(); // in MB

extern "C" int ShowSimpleMessageBox(const char *text, const char *caption,
                                       unsigned int nButton,
                                       const char **btnText); // C-style

int ShowSimpleMessageBox(const ttstr &text, const ttstr &caption,
                            const std::vector<ttstr> &vecButtons);
int ShowSimpleMessageBox(const ttstr &text, const ttstr &caption);
int ShowSimpleMessageBoxYesNo(const ttstr &text, const ttstr &caption);

int ShowSimpleInputBox(ttstr &text, const ttstr &caption,
                          const ttstr &prompt,
                          const std::vector<ttstr> &vecButtons);

std::vector<std::string> GetDriverPath();
std::vector<std::string> GetAppStoragePath();
bool CheckStartupPath(const std::string &path);
std::string GetPackageVersionString();
void ExitApplication(int code);
void CheckMemory();
const std::string &GetInternalPreferencePath();
bool DeleteLocalFile(const std::string &filename);
bool RenameLocalFile(const std::string &from, const std::string &to);
bool CopyLocalFile(const std::string &from, const std::string &to);

void ShowIME(int x, int y, int w, int h);
void HideIME();

void RelinquishCPU();
void PrintLog(const char *str);

// 宏定义冲突 sys/stat.h
#ifdef st_atime
#undef st_atime
#endif
#ifdef st_mtime
#undef st_mtime
#endif
#ifdef st_ctime
#undef st_ctime
#endif

struct tTVP_stat {
    uint16_t st_mode;
    uint64_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
};

bool KRKR_stat(const tjs_char *name, tTVP_stat &s);
bool KRKR_stat(const char *name, tTVP_stat &s);
bool KRKR_utime(const char *name, time_t modtime);

void SendToOtherApp(const std::string &filename);
