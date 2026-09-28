#include "fx.h"

// Freeverb's comb lengths, scaled from its 44.1 kHz reference to 48 kHz so the
// modal density stays where Schroeder put it.  The allpass pair is deliberately
// different left vs right — that decorrelation is the whole stereo image.
const uint16_t Reverb::CLEN[Reverb::NCOMB]   = { 1214, 1390, 1548, 1694 };
const uint16_t Reverb::ALEN[Reverb::NAP]     = {  605,  480 };
const uint16_t Reverb::ALEN_R[Reverb::NAP]   = {  557,  441 };

// See the comment above the extern declarations in fx.h: one shared
// instance instead of one per engine, ~650KB of SRAM this board doesn't
// have to spare six times over.
Chorus      g_fxChorus;
StereoDelay g_fxDelay;
Reverb      g_fxReverb;
