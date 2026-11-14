//---------------------------------------------------------------------------
// Built-in decoders for protected XP3 archives.
//---------------------------------------------------------------------------

#ifndef XP3ArchiveCxDecoderH
#define XP3ArchiveCxDecoderH

#include <cstddef>
#include <cstdint>

namespace krkr::io {

bool IsBuiltinXP3CxScheme(std::uint32_t fingerprint);
bool ShouldUseBuiltinXP3CxDecoder(std::uint32_t fingerprint,
                                     const void *header,
                                     std::size_t headerSize);
bool ActivateBuiltinXP3CxDecoder(std::uint32_t fingerprint);
void ResetBuiltinXP3CxDecoder();
bool IsBuiltinXP3CxDecoderActive();
bool DecodeBuiltinXP3Cx(std::uint32_t hash, std::uint64_t offset,
                           void *buffer, std::uint32_t size);

} // namespace krkr::io

// TODO: 过渡兼容，调用点迁移完成后删除

#endif
