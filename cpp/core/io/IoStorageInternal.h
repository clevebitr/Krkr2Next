#ifndef KRKR_IO_STORAGE_INTERNAL_H
#define KRKR_IO_STORAGE_INTERNAL_H

#include <vector>

#include "tjsCommHead.h"

// IoStorage.cpp 与 IoStorageLocal.cpp 之间共享的内部状态。
namespace krkr::io {

extern std::vector<ttstr> TVPAutoPathList;
extern bool AutoPathTableInit;

} // namespace krkr::io

#endif // KRKR_IO_STORAGE_INTERNAL_H
