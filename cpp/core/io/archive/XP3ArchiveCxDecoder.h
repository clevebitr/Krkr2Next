//---------------------------------------------------------------------------
// Built-in decoders for protected XP3 archives.
//---------------------------------------------------------------------------

#ifndef XP3ArchiveCxDecoderH
#define XP3ArchiveCxDecoderH

#include <cstddef>
#include <cstdint>

namespace krkr::io {

bool TVPIsBuiltinXP3CxScheme(std::uint32_t fingerprint);
bool TVPShouldUseBuiltinXP3CxDecoder(std::uint32_t fingerprint,
                                     const void *header,
                                     std::size_t headerSize);
bool TVPActivateBuiltinXP3CxDecoder(std::uint32_t fingerprint);
void TVPResetBuiltinXP3CxDecoder();
bool TVPIsBuiltinXP3CxDecoderActive();
bool TVPDecodeBuiltinXP3Cx(std::uint32_t hash, std::uint64_t offset,
                           void *buffer, std::uint32_t size);

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除
using krkr::io::TVPIsBuiltinXP3CxScheme;
using krkr::io::TVPShouldUseBuiltinXP3CxDecoder;
using krkr::io::TVPActivateBuiltinXP3CxDecoder;
using krkr::io::TVPResetBuiltinXP3CxDecoder;
using krkr::io::TVPIsBuiltinXP3CxDecoderActive;
using krkr::io::TVPDecodeBuiltinXP3Cx;

#endif
