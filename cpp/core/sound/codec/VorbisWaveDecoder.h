#pragma once

#include "WaveIntf.h"

namespace krkr::sound {

class VorbisWaveDecoderCreator : public WaveDecoderCreator {
public:
    // VorbisWaveDecoderCreator() {
    // RegisterWaveDecoderCreator(this); }
    WaveDecoder *Create(const ttstr &storagename,
                            const ttstr &extension) override;
};

class OpusWaveDecoderCreator : public WaveDecoderCreator {
public:
    // VorbisWaveDecoderCreator() {
    // RegisterWaveDecoderCreator(this); }
    WaveDecoder *Create(const ttstr &storagename,
                            const ttstr &extension) override;
};

} // namespace krkr::sound

using krkr::sound::VorbisWaveDecoderCreator;
using krkr::sound::OpusWaveDecoderCreator;