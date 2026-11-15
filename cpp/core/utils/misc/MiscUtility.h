#ifndef KRKR_UTILS_MISCUTILITY_H
#define KRKR_UTILS_MISCUTILITY_H

#include <string>

#include "tjsCommHead.h"

namespace krkr::utils {

bool EncodeUTF8ToUTF16(ttstr &output, const std::string &source);

} // namespace krkr::utils

// TODO: 过渡兼容，调用点迁移完成后删除

#endif // KRKR_UTILS_MISCUTILITY_H
