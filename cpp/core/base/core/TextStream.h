#pragma once
#include "StorageIntf.h"

namespace krkr::base {

iTJSTextReadStream *TVPCreateTextStreamForRead(const ttstr &name,
                                               const ttstr &mode);
iTJSTextWriteStream *TVPCreateTextStreamForWrite(const ttstr &name,
                                                 const ttstr &mode);

std::string checkTextEncoding(const void *buf, size_t size,
                              std::uint8_t &bomSize);

void TVPSetDefaultReadEncoding(const ttstr &encoding);

const tjs_char *TVPGetDefaultReadEncoding();

} // namespace krkr::base

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::base::TVPCreateTextStreamForRead;
using krkr::base::TVPCreateTextStreamForWrite;
using krkr::base::checkTextEncoding;
using krkr::base::TVPSetDefaultReadEncoding;
using krkr::base::TVPGetDefaultReadEncoding;