#pragma once

#include "WaveIntf.h"

namespace krkr::sound {

class VorbisWaveDecoderCreator : public tTVPWaveDecoderCreator {
public:
    // VorbisWaveDecoderCreator() {
    // TVPRegisterWaveDecoderCreator(this); }
    tTVPWaveDecoder *Create(const ttstr &storagename,
                            const ttstr &extension) override;
};

class OpusWaveDecoderCreator : public tTVPWaveDecoderCreator {
public:
    // VorbisWaveDecoderCreator() {
    // TVPRegisterWaveDecoderCreator(this); }
    tTVPWaveDecoder *Create(const ttstr &storagename,
                            const ttstr &extension) override;
};

} // namespace krkr::sound

using krkr::sound::VorbisWaveDecoderCreator;
using krkr::sound::OpusWaveDecoderCreator;