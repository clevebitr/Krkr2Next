#pragma once
#include "StorageIntf.h"

namespace krkr::base {

iTJSTextReadStream *CreateTextStreamForRead(const ttstr &name,
                                               const ttstr &mode);
iTJSTextWriteStream *CreateTextStreamForWrite(const ttstr &name,
                                                 const ttstr &mode);

std::string checkTextEncoding(const void *buf, size_t size,
                              std::uint8_t &bomSize);

void SetDefaultReadEncoding(const ttstr &encoding);

const tjs_char *GetDefaultReadEncoding();

} // namespace krkr::base

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::base::checkTextEncoding;
