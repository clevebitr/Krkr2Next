#pragma once

#include "WaveIntf.h"

namespace krkr::sound {

class FFWaveDecoderCreator : public tTVPWaveDecoderCreator {
public:
    // VorbisWaveDecoderCreator() {
    // TVPRegisterWaveDecoderCreator(this); }
    tTVPWaveDecoder *Create(const ttstr &storagename,
                            const ttstr &extension) override;
};

} // namespace krkr::sound

using krkr::sound::FFWaveDecoderCreator;
