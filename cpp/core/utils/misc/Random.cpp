//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Simple Pseudo Random Generator
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "Random.h"
#include "md5.h"
#include "tjsUtils.h"

namespace krkr::utils {


// this implements simple environment-noise based pseudo random
// generator using MD5. may not be suitable for high security usage.

namespace { 
    tjs_uint8 randomSeedPool[0x1000 + 512];
    tjs_int randomSeedPoolPos = 0;
    tjs_uint8 randomSeedAtom; // need not to initialize
}
void PushEnvironNoise(const void *buf, tjs_int bufsize) {
    const auto *p = (const tjs_uint8 *)buf;
    for(int i = 0; i < bufsize; i++) {
        randomSeedPool[randomSeedPoolPos++] ^=
            (randomSeedAtom ^= p[i]);
        randomSeedPoolPos &= 0xfff;
    }
    randomSeedPoolPos += (p[0] & 1);
    randomSeedPoolPos &= 0xfff;
}

void GetRandomBits128(void *dest) {
    // retrieve random bits

    // add some noise
    tjs_uint8 buf[16];
    PushEnvironNoise(buf, 16); // dare to use uninitialized buffer
    PushEnvironNoise(&randomSeedPoolPos, sizeof(randomSeedPoolPos));

    // make 128bit hash of randomSeedPool, using MD5 message digest
    md5_state_t state;
    md5_init(&state);
    md5_append(&state, randomSeedPool, 0x1000);
    md5_finish(&state, buf);

    memcpy(dest, buf, 16);

    // push hash itself
    PushEnvironNoise(buf, 16);
}
} //namespace krkr::utils
