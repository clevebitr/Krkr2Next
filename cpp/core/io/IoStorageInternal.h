#ifndef KRKR_IO_STORAGE_INTERNAL_H
#define KRKR_IO_STORAGE_INTERNAL_H

#include <vector>

#include "tjsCommHead.h"

// IoStorage.cpp 与 IoStorageLocal.cpp 之间共享、但暂时仍保持全局的符号。
// 这两个变量被 IoStorageLocal.cpp 在函数内以全局 extern 使用，等 IoStorageLocal
// 整体迁入 krkr::io 后再一并收进 namespace。
extern std::vector<ttstr> TVPAutoPathList;
extern bool AutoPathTableInit;

#endif // KRKR_IO_STORAGE_INTERNAL_H
