#pragma once

#include "WaveIntf.h"

namespace krkr::sound {

class FFWaveDecoderCreator : public WaveDecoderCreator {
public:
    // VorbisWaveDecoderCreator() {
    // RegisterWaveDecoderCreator(this); }
    WaveDecoder *Create(const ttstr &storagename,
                            const ttstr &extension) override;
};

} // namespace krkr::sound

using krkr::sound::FFWaveDecoderCreator;
