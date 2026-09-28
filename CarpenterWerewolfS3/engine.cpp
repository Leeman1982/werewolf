#include "engine.h"
#include "engine_cs80.h"
#include "minimoog.h"
#include "prophet5.h"
#include "prophet10.h"
#include "arp2600.h"
#include "odyssey.h"
#include "mellotron.h"
#include "oberheim.h"
#include "jupiter8.h"
#include "system100.h"
#include "moogmodular.h"
#include "obx.h"
#include "ppgwave.h"
#include "ms20.h"
#include "dx7.h"
#include "stringens.h"
#include "prophetvs.h"
#include "stub_engine.h"
#include "voicealloc.h"

// ============================================================================
//  engine.cpp — the switch that makes this a multi-synth instrument.
//
//  Every function here is a `switch (currentEngine)` dispatching to one
//  engine module's own `xxxE()` functions, matching the rest of this
//  codebase's style (switches, not virtuals). See engine.h for why exactly
//  one engine is live at a time.
//
//  Carpenter Synth port status: every engine is real. CS-80, Minimoog,
//  Prophet-5, Prophet-10, ARP 2600, ARP Odyssey and Mellotron came from the
//  ESP32-S3 source; Oberheim 4-Voice/SEM, Jupiter-8 and System 100 had none
//  (the source project stubbed all three) and were built from scratch here,
//  from each real instrument's own published architecture -- see
//  oberheim.h/jupiter8.h/system100.h's file banners. Moog Modular, OB-X and
//  PPG Wave 2.3 are a further three engines with no upstream stub slot at
//  all: Moog Modular is the wildcard pick, the actual synth behind both
//  Halloween and Suspiria (moogmodular.h); OB-X and PPG Wave are both
//  Claudio Simonetti/Goblin instruments this project's own influences
//  named but the source project never staged (obx.h/ppgwave.h). Korg MS-20,
//  Yamaha DX7, ARP/Solina String Ensemble and Sequential Prophet-VS are a
//  final four with no upstream stub slot either, added to round the roster
//  out past subtractive analogue polysynths: MS-20 is the vintage Korg mono
//  pick, DX7 is this codebase's only FM/phase-modulation engine, the String
//  Ensemble is its only organ-style divide-down engine, and Prophet-VS is
//  its only vector-synthesis engine (ms20.h/dx7.h/stringens.h/prophetvs.h).
//
//  Sixteen of the seventeen are modelled here, from each machine's own
//  documented architecture.  The DX7 is the exception and deliberately so:
//  it drives the real Dexed/MSFA engine through the vendored Synth_Dexed
//  library, because a digital instrument's sound IS its exact fixed-point
//  arithmetic and an approximation of that is a different synth rather than
//  a less accurate one.  See dx7.h for the full reasoning, and the README's
//  Licensing section for what linking a GPLv3 library means for this
//  firmware as a whole.
// ============================================================================

const char* const ENGINE_NAMES[NUM_ENGINES] = {
    "CS-80", "MINIMOOG", "PROPHET-5", "PROPHET-10", "ARP 2600", "ARP ODYSSEY",
    "MELLOTRON", "OB 4-VOICE/SEM", "JUPITER-8", "SYSTEM 100", "MOOG MODULAR",
    "OBERHEIM OB-X", "PPG WAVE 2.3",
    "KORG MS-20", "YAMAHA DX7", "STRING ENSEMBLE", "PROPHET-VS",
};
const char* const ENGINE_MAKER[NUM_ENGINES] = {
    "Yamaha (Frizzi)",
    "Moog (Carpenter, Simonetti)",
    "Sequential Circuits (Carpenter, Frizzi)",
    "Sequential Circuits (Carpenter)",
    "ARP (Carpenter)",
    "ARP (Carpenter)",
    "(Frizzi)",
    "Oberheim (Carpenter)",
    "Roland (Simonetti)",
    "Roland (Simonetti)",
    "Moog (Carpenter, Fugazza/Goblin)",
    "Oberheim (Simonetti/Goblin)",
    "PPG (Simonetti/Goblin)",
    "Korg (Goblin)",
    "Yamaha (Carpenter)",
    "ARP/Solina (Carpenter)",
    "Sequential Circuits (Carpenter)",
};
// Every engine is real now — nothing left to stub. stub_engine.* stays as
// the dispatch default (see every switch below) in case an eighteenth engine
// is ever staged the same incremental way these seventeen were.
const bool ENGINE_IS_STUB[NUM_ENGINES] = {
    false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false,
};

EngineId currentEngine = ENGINE_CS80;
extern VoiceAlloc va;

// Each engine's own param array is session-persistent RAM, not freed or
// reused by any other engine — so switching away and back preserves
// whatever you'd tweaked, exactly like leaving one real instrument's knobs
// alone while you play another. The only thing that needs doing once per
// engine, ever, is loading its INIT_PATCH the first time it's selected —
// otherwise its param array is still at its static all-zero default and
// nothing downstream (cutoff maps, envelope coefficients, ...) has been
// computed from it yet.
static bool s_engineInitialized[NUM_ENGINES] = { false };

void engineSwitchTo(EngineId id) {
    if (id >= NUM_ENGINES) return;
    engineAllOff();
    va.reset();
    currentEngine = id;
    switch (currentEngine) {
    case ENGINE_CS80:      cs80VoiceInitE();  break;
    case ENGINE_MINIMOOG:  mmVoiceInitE();    break;
    case ENGINE_PROPHET5:  p5SetPolyphonyE(5);  p5VoiceInitE();  break;
    case ENGINE_PROPHET10: p10VoiceInitE();   break;
    case ENGINE_ARP2600:   a26SetPolyphonyE(1); a26VoiceInitE(); break;
    case ENGINE_ODYSSEY:   odyVoiceInitE();   break;
    case ENGINE_MELLOTRON: melVoiceInitE();   break;
    case ENGINE_OBSEM:    obVoiceInitE(); break;
    case ENGINE_OBX:    obxVoiceInitE(); break;
    case ENGINE_PPGWAVE:    ppgVoiceInitE(); break;
    case ENGINE_MOOGMODULAR:    mm2VoiceInitE(); break;
    case ENGINE_SYSTEM100:    sy1VoiceInitE(); break;
    case ENGINE_JUPITER8:    jp8VoiceInitE(); break;
    case ENGINE_MS20:    ms20VoiceInitE(); break;
    case ENGINE_DX7:    dxVoiceInitE(); break;
    case ENGINE_STRINGENS:    seVoiceInitE(); break;
    case ENGINE_PROPHETVS:    vsVoiceInitE(); break;
    default:               stubVoiceInitE();  break;
    }
    if (!s_engineInitialized[id]) {
        engineParamsInit();
        s_engineInitialized[id] = true;
    }
    va.limit = engineNumVoices();
}

void engineParamsInit() {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80ParamsInitE(); break;
    case ENGINE_MINIMOOG:  mmParamsInitE();   break;
    case ENGINE_PROPHET5:  p5ParamsInitE();   break;
    case ENGINE_PROPHET10: p10ParamsInitE();  break;
    case ENGINE_ARP2600:   a26ParamsInitE(); break;
    case ENGINE_ODYSSEY:   odyParamsInitE(); break;
    case ENGINE_MELLOTRON: melParamsInitE();  break;
    case ENGINE_OBSEM:    obParamsInitE(); break;
    case ENGINE_OBX:    obxParamsInitE(); break;
    case ENGINE_PPGWAVE:    ppgParamsInitE(); break;
    case ENGINE_MOOGMODULAR:    mm2ParamsInitE(); break;
    case ENGINE_SYSTEM100:    sy1ParamsInitE(); break;
    case ENGINE_JUPITER8:    jp8ParamsInitE(); break;
    case ENGINE_MS20:    ms20ParamsInitE(); break;
    case ENGINE_DX7:    dxParamsInitE(); break;
    case ENGINE_STRINGENS:    seParamsInitE(); break;
    case ENGINE_PROPHETVS:    vsParamsInitE(); break;
    default:               stubParamsInitE(); break;
    }
}
void engineSetParameter(uint8_t id, float v) {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80SetParameterE(id, v); break;
    case ENGINE_MINIMOOG:  mmSetParameterE(id, v);   break;
    case ENGINE_PROPHET5:  p5SetParameterE(id, v);   break;
    case ENGINE_PROPHET10: p10SetParameterE(id, v);  break;
    case ENGINE_ARP2600:   a26SetParameterE(id, v); break;
    case ENGINE_ODYSSEY:   odySetParameterE(id, v); break;
    case ENGINE_MELLOTRON: melSetParameterE(id, v);  break;
    case ENGINE_OBSEM:    obSetParameterE(id, v); break;
    case ENGINE_OBX:    obxSetParameterE(id, v); break;
    case ENGINE_PPGWAVE:    ppgSetParameterE(id, v); break;
    case ENGINE_MOOGMODULAR:    mm2SetParameterE(id, v); break;
    case ENGINE_SYSTEM100:    sy1SetParameterE(id, v); break;
    case ENGINE_JUPITER8:    jp8SetParameterE(id, v); break;
    case ENGINE_MS20:    ms20SetParameterE(id, v); break;
    case ENGINE_DX7:    dxSetParameterE(id, v); break;
    case ENGINE_STRINGENS:    seSetParameterE(id, v); break;
    case ENGINE_PROPHETVS:    vsSetParameterE(id, v); break;
    default:               stubSetParameterE(id, v); break;
    }
}
float engineGetParameter(uint8_t id) {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80GetParameterE(id);
    case ENGINE_MINIMOOG:  return mmGetParameterE(id);
    case ENGINE_PROPHET5:  return p5GetParameterE(id);
    case ENGINE_PROPHET10: return p10GetParameterE(id);
    case ENGINE_ARP2600:   return a26GetParameterE(id);
    case ENGINE_ODYSSEY:   return odyGetParameterE(id);
    case ENGINE_MELLOTRON: return melGetParameterE(id);
    case ENGINE_OBSEM:    return obGetParameterE(id);
    case ENGINE_OBX:    return obxGetParameterE(id);
    case ENGINE_PPGWAVE:    return ppgGetParameterE(id);
    case ENGINE_MOOGMODULAR:    return mm2GetParameterE(id);
    case ENGINE_SYSTEM100:    return sy1GetParameterE(id);
    case ENGINE_JUPITER8:    return jp8GetParameterE(id);
    case ENGINE_MS20:    return ms20GetParameterE(id);
    case ENGINE_DX7:    return dxGetParameterE(id);
    case ENGINE_STRINGENS:    return seGetParameterE(id);
    case ENGINE_PROPHETVS:    return vsGetParameterE(id);
    default:               return stubGetParameterE(id);
    }
}
void engineFormatParameter(uint8_t id, char* buf, size_t n) {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80FormatParameterE(id, buf, n); break;
    case ENGINE_MINIMOOG:  mmFormatParameterE(id, buf, n);   break;
    case ENGINE_PROPHET5:  p5FormatParameterE(id, buf, n);   break;
    case ENGINE_PROPHET10: p10FormatParameterE(id, buf, n);  break;
    case ENGINE_ARP2600:   a26FormatParameterE(id, buf, n); break;
    case ENGINE_ODYSSEY:   odyFormatParameterE(id, buf, n); break;
    case ENGINE_MELLOTRON: melFormatParameterE(id, buf, n);  break;
    case ENGINE_OBSEM:    obFormatParameterE(id, buf, n); break;
    case ENGINE_OBX:    obxFormatParameterE(id, buf, n); break;
    case ENGINE_PPGWAVE:    ppgFormatParameterE(id, buf, n); break;
    case ENGINE_MOOGMODULAR:    mm2FormatParameterE(id, buf, n); break;
    case ENGINE_SYSTEM100:    sy1FormatParameterE(id, buf, n); break;
    case ENGINE_JUPITER8:    jp8FormatParameterE(id, buf, n); break;
    case ENGINE_MS20:    ms20FormatParameterE(id, buf, n); break;
    case ENGINE_DX7:    dxFormatParameterE(id, buf, n); break;
    case ENGINE_STRINGENS:    seFormatParameterE(id, buf, n); break;
    case ENGINE_PROPHETVS:    vsFormatParameterE(id, buf, n); break;
    default:               stubFormatParameterE(id, buf, n); break;
    }
}
bool engineParamIsLockable(uint8_t id) {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80ParamIsLockableE(id);
    case ENGINE_MINIMOOG:  return mmParamIsLockableE(id);
    case ENGINE_PROPHET5:  return p5ParamIsLockableE(id);
    case ENGINE_PROPHET10: return p10ParamIsLockableE(id);
    case ENGINE_ARP2600:   return a26ParamIsLockableE(id);
    case ENGINE_ODYSSEY:   return odyParamIsLockableE(id);
    case ENGINE_MELLOTRON: return melParamIsLockableE(id);
    case ENGINE_OBSEM:    return obParamIsLockableE(id);
    case ENGINE_OBX:    return obxParamIsLockableE(id);
    case ENGINE_PPGWAVE:    return ppgParamIsLockableE(id);
    case ENGINE_MOOGMODULAR:    return mm2ParamIsLockableE(id);
    case ENGINE_SYSTEM100:    return sy1ParamIsLockableE(id);
    case ENGINE_JUPITER8:    return jp8ParamIsLockableE(id);
    case ENGINE_MS20:    return ms20ParamIsLockableE(id);
    case ENGINE_DX7:    return dxParamIsLockableE(id);
    case ENGINE_STRINGENS:    return seParamIsLockableE(id);
    case ENGINE_PROPHETVS:    return vsParamIsLockableE(id);
    default:               return stubParamIsLockableE(id);
    }
}
uint8_t engineNumParams() {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80NumParamsE();
    case ENGINE_MINIMOOG:  return mmNumParamsE();
    case ENGINE_PROPHET5:  return p5NumParamsE();
    case ENGINE_PROPHET10: return p10NumParamsE();
    case ENGINE_ARP2600:   return a26NumParamsE();
    case ENGINE_ODYSSEY:   return odyNumParamsE();
    case ENGINE_MELLOTRON: return melNumParamsE();
    case ENGINE_OBSEM:    return obNumParamsE();
    case ENGINE_OBX:    return obxNumParamsE();
    case ENGINE_PPGWAVE:    return ppgNumParamsE();
    case ENGINE_MOOGMODULAR:    return mm2NumParamsE();
    case ENGINE_SYSTEM100:    return sy1NumParamsE();
    case ENGINE_JUPITER8:    return jp8NumParamsE();
    case ENGINE_MS20:    return ms20NumParamsE();
    case ENGINE_DX7:    return dxNumParamsE();
    case ENGINE_STRINGENS:    return seNumParamsE();
    case ENGINE_PROPHETVS:    return vsNumParamsE();
    default:               return stubNumParamsE();
    }
}
const ParamInfo* engineParamInfo(uint8_t id) {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80ParamInfoE(id);
    case ENGINE_MINIMOOG:  return mmParamInfoE(id);
    case ENGINE_PROPHET5:  return p5ParamInfoE(id);
    case ENGINE_PROPHET10: return p10ParamInfoE(id);
    case ENGINE_ARP2600:   return a26ParamInfoE(id);
    case ENGINE_ODYSSEY:   return odyParamInfoE(id);
    case ENGINE_MELLOTRON: return melParamInfoE(id);
    case ENGINE_OBSEM:    return obParamInfoE(id);
    case ENGINE_OBX:    return obxParamInfoE(id);
    case ENGINE_PPGWAVE:    return ppgParamInfoE(id);
    case ENGINE_MOOGMODULAR:    return mm2ParamInfoE(id);
    case ENGINE_SYSTEM100:    return sy1ParamInfoE(id);
    case ENGINE_JUPITER8:    return jp8ParamInfoE(id);
    case ENGINE_MS20:    return ms20ParamInfoE(id);
    case ENGINE_DX7:    return dxParamInfoE(id);
    case ENGINE_STRINGENS:    return seParamInfoE(id);
    case ENGINE_PROPHETVS:    return vsParamInfoE(id);
    default:               return stubParamInfoE(id);
    }
}
uint8_t engineNumPresets() {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80NumPresetsE();
    case ENGINE_MINIMOOG:  return mmNumPresetsE();
    case ENGINE_PROPHET5:  return p5NumPresetsE();
    case ENGINE_PROPHET10: return p10NumPresetsE();
    case ENGINE_ARP2600:   return a26NumPresetsE();
    case ENGINE_ODYSSEY:   return odyNumPresetsE();
    case ENGINE_MELLOTRON: return melNumPresetsE();
    case ENGINE_OBSEM:    return obNumPresetsE();
    case ENGINE_OBX:    return obxNumPresetsE();
    case ENGINE_PPGWAVE:    return ppgNumPresetsE();
    case ENGINE_MOOGMODULAR:    return mm2NumPresetsE();
    case ENGINE_SYSTEM100:    return sy1NumPresetsE();
    case ENGINE_JUPITER8:    return jp8NumPresetsE();
    case ENGINE_MS20:    return ms20NumPresetsE();
    case ENGINE_DX7:    return dxNumPresetsE();
    case ENGINE_STRINGENS:    return seNumPresetsE();
    case ENGINE_PROPHETVS:    return vsNumPresetsE();
    default:               return stubNumPresetsE();
    }
}
const char* enginePresetName(uint8_t idx) {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80PresetNameE(idx);
    case ENGINE_MINIMOOG:  return mmPresetNameE(idx);
    case ENGINE_PROPHET5:  return p5PresetNameE(idx);
    case ENGINE_PROPHET10: return p10PresetNameE(idx);
    case ENGINE_ARP2600:   return a26PresetNameE(idx);
    case ENGINE_ODYSSEY:   return odyPresetNameE(idx);
    case ENGINE_MELLOTRON: return melPresetNameE(idx);
    case ENGINE_OBSEM:    return obPresetNameE(idx);
    case ENGINE_OBX:    return obxPresetNameE(idx);
    case ENGINE_PPGWAVE:    return ppgPresetNameE(idx);
    case ENGINE_MOOGMODULAR:    return mm2PresetNameE(idx);
    case ENGINE_SYSTEM100:    return sy1PresetNameE(idx);
    case ENGINE_JUPITER8:    return jp8PresetNameE(idx);
    case ENGINE_MS20:    return ms20PresetNameE(idx);
    case ENGINE_DX7:    return dxPresetNameE(idx);
    case ENGINE_STRINGENS:    return sePresetNameE(idx);
    case ENGINE_PROPHETVS:    return vsPresetNameE(idx);
    default:               return stubPresetNameE(idx);
    }
}
void engineLoadPreset(uint8_t idx) {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80LoadPresetE(idx); break;
    case ENGINE_MINIMOOG:  mmLoadPresetE(idx);   break;
    case ENGINE_PROPHET5:  p5LoadPresetE(idx);   break;
    case ENGINE_PROPHET10: p10LoadPresetE(idx);  break;
    case ENGINE_ARP2600:   a26LoadPresetE(idx); break;
    case ENGINE_ODYSSEY:   odyLoadPresetE(idx); break;
    case ENGINE_MELLOTRON: melLoadPresetE(idx);  break;
    case ENGINE_OBSEM:    obLoadPresetE(idx); break;
    case ENGINE_OBX:    obxLoadPresetE(idx); break;
    case ENGINE_PPGWAVE:    ppgLoadPresetE(idx); break;
    case ENGINE_MOOGMODULAR:    mm2LoadPresetE(idx); break;
    case ENGINE_SYSTEM100:    sy1LoadPresetE(idx); break;
    case ENGINE_JUPITER8:    jp8LoadPresetE(idx); break;
    case ENGINE_MS20:    ms20LoadPresetE(idx); break;
    case ENGINE_DX7:    dxLoadPresetE(idx); break;
    case ENGINE_STRINGENS:    seLoadPresetE(idx); break;
    case ENGINE_PROPHETVS:    vsLoadPresetE(idx); break;
    default:               stubLoadPresetE(idx); break;
    }
}
void engineLoadInitPatch() {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80LoadInitPatchE(); break;
    case ENGINE_MINIMOOG:  mmLoadInitPatchE();   break;
    case ENGINE_PROPHET5:  p5LoadInitPatchE();   break;
    case ENGINE_PROPHET10: p10LoadInitPatchE();  break;
    case ENGINE_ARP2600:   a26LoadInitPatchE(); break;
    case ENGINE_ODYSSEY:   odyLoadInitPatchE(); break;
    case ENGINE_MELLOTRON: melLoadInitPatchE();  break;
    case ENGINE_OBSEM:    obLoadInitPatchE(); break;
    case ENGINE_OBX:    obxLoadInitPatchE(); break;
    case ENGINE_PPGWAVE:    ppgLoadInitPatchE(); break;
    case ENGINE_MOOGMODULAR:    mm2LoadInitPatchE(); break;
    case ENGINE_SYSTEM100:    sy1LoadInitPatchE(); break;
    case ENGINE_JUPITER8:    jp8LoadInitPatchE(); break;
    case ENGINE_MS20:    ms20LoadInitPatchE(); break;
    case ENGINE_DX7:    dxLoadInitPatchE(); break;
    case ENGINE_STRINGENS:    seLoadInitPatchE(); break;
    case ENGINE_PROPHETVS:    vsLoadInitPatchE(); break;
    default:               stubLoadInitPatchE(); break;
    }
}
void engineSnapshotParams(uint8_t* out) {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80SnapshotParamsE(out); break;
    case ENGINE_MINIMOOG:  mmSnapshotParamsE(out);   break;
    case ENGINE_PROPHET5:  p5SnapshotParamsE(out);   break;
    case ENGINE_PROPHET10: p10SnapshotParamsE(out);  break;
    case ENGINE_ARP2600:   a26SnapshotParamsE(out); break;
    case ENGINE_ODYSSEY:   odySnapshotParamsE(out); break;
    case ENGINE_MELLOTRON: melSnapshotParamsE(out);  break;
    case ENGINE_OBSEM:    obSnapshotParamsE(out); break;
    case ENGINE_OBX:    obxSnapshotParamsE(out); break;
    case ENGINE_PPGWAVE:    ppgSnapshotParamsE(out); break;
    case ENGINE_MOOGMODULAR:    mm2SnapshotParamsE(out); break;
    case ENGINE_SYSTEM100:    sy1SnapshotParamsE(out); break;
    case ENGINE_JUPITER8:    jp8SnapshotParamsE(out); break;
    case ENGINE_MS20:    ms20SnapshotParamsE(out); break;
    case ENGINE_DX7:    dxSnapshotParamsE(out); break;
    case ENGINE_STRINGENS:    seSnapshotParamsE(out); break;
    case ENGINE_PROPHETVS:    vsSnapshotParamsE(out); break;
    default:               stubSnapshotParamsE(out); break;
    }
}
void engineRestoreParams(const uint8_t* in) {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80RestoreParamsE(in); break;
    case ENGINE_MINIMOOG:  mmRestoreParamsE(in);   break;
    case ENGINE_PROPHET5:  p5RestoreParamsE(in);   break;
    case ENGINE_PROPHET10: p10RestoreParamsE(in);  break;
    case ENGINE_ARP2600:   a26RestoreParamsE(in); break;
    case ENGINE_ODYSSEY:   odyRestoreParamsE(in); break;
    case ENGINE_MELLOTRON: melRestoreParamsE(in);  break;
    case ENGINE_OBSEM:    obRestoreParamsE(in); break;
    case ENGINE_OBX:    obxRestoreParamsE(in); break;
    case ENGINE_PPGWAVE:    ppgRestoreParamsE(in); break;
    case ENGINE_MOOGMODULAR:    mm2RestoreParamsE(in); break;
    case ENGINE_SYSTEM100:    sy1RestoreParamsE(in); break;
    case ENGINE_JUPITER8:    jp8RestoreParamsE(in); break;
    case ENGINE_MS20:    ms20RestoreParamsE(in); break;
    case ENGINE_DX7:    dxRestoreParamsE(in); break;
    case ENGINE_STRINGENS:    seRestoreParamsE(in); break;
    case ENGINE_PROPHETVS:    vsRestoreParamsE(in); break;
    default:               stubRestoreParamsE(in); break;
    }
}

uint8_t engineNumPages() {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80NumPagesE();
    case ENGINE_MINIMOOG:  return mmNumPagesE();
    case ENGINE_PROPHET5:  return p5NumPagesE();
    case ENGINE_PROPHET10: return p10NumPagesE();
    case ENGINE_ARP2600:   return a26NumPagesE();
    case ENGINE_ODYSSEY:   return odyNumPagesE();
    case ENGINE_MELLOTRON: return melNumPagesE();
    case ENGINE_OBSEM:    return obNumPagesE();
    case ENGINE_OBX:    return obxNumPagesE();
    case ENGINE_PPGWAVE:    return ppgNumPagesE();
    case ENGINE_MOOGMODULAR:    return mm2NumPagesE();
    case ENGINE_SYSTEM100:    return sy1NumPagesE();
    case ENGINE_JUPITER8:    return jp8NumPagesE();
    case ENGINE_MS20:    return ms20NumPagesE();
    case ENGINE_DX7:    return dxNumPagesE();
    case ENGINE_STRINGENS:    return seNumPagesE();
    case ENGINE_PROPHETVS:    return vsNumPagesE();
    default:               return stubNumPagesE();
    }
}
const EnginePageDef* enginePage(uint8_t idx) {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80PageE(idx);
    case ENGINE_MINIMOOG:  return mmPageE(idx);
    case ENGINE_PROPHET5:  return p5PageE(idx);
    case ENGINE_PROPHET10: return p10PageE(idx);
    case ENGINE_ARP2600:   return a26PageE(idx);
    case ENGINE_ODYSSEY:   return odyPageE(idx);
    case ENGINE_MELLOTRON: return melPageE(idx);
    case ENGINE_OBSEM:    return obPageE(idx);
    case ENGINE_OBX:    return obxPageE(idx);
    case ENGINE_PPGWAVE:    return ppgPageE(idx);
    case ENGINE_MOOGMODULAR:    return mm2PageE(idx);
    case ENGINE_SYSTEM100:    return sy1PageE(idx);
    case ENGINE_JUPITER8:    return jp8PageE(idx);
    case ENGINE_MS20:    return ms20PageE(idx);
    case ENGINE_DX7:    return dxPageE(idx);
    case ENGINE_STRINGENS:    return sePageE(idx);
    case ENGINE_PROPHETVS:    return vsPageE(idx);
    default:               return stubPageE(idx);
    }
}
uint8_t engineMasterParamId() {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80MasterParamIdE();
    case ENGINE_MINIMOOG:  return mmMasterParamIdE();
    case ENGINE_PROPHET5:  return p5MasterParamIdE();
    case ENGINE_PROPHET10: return p10MasterParamIdE();
    case ENGINE_ARP2600:   return a26MasterParamIdE();
    case ENGINE_ODYSSEY:   return odyMasterParamIdE();
    case ENGINE_MELLOTRON: return melMasterParamIdE();
    case ENGINE_OBSEM:    return obMasterParamIdE();
    case ENGINE_OBX:    return obxMasterParamIdE();
    case ENGINE_PPGWAVE:    return ppgMasterParamIdE();
    case ENGINE_MOOGMODULAR:    return mm2MasterParamIdE();
    case ENGINE_SYSTEM100:    return sy1MasterParamIdE();
    case ENGINE_JUPITER8:    return jp8MasterParamIdE();
    case ENGINE_MS20:    return ms20MasterParamIdE();
    case ENGINE_DX7:    return dxMasterParamIdE();
    case ENGINE_STRINGENS:    return seMasterParamIdE();
    case ENGINE_PROPHETVS:    return vsMasterParamIdE();
    default:               return stubMasterParamIdE();
    }
}

uint8_t engineFilterCutoffParamId() {
    switch (currentEngine) {
    case ENGINE_CS80:        return cs80FilterCutoffParamIdE();
    case ENGINE_MINIMOOG:    return mmFilterCutoffParamIdE();
    case ENGINE_PROPHET5:    return p5FilterCutoffParamIdE();
    case ENGINE_PROPHET10:   return p10FilterCutoffParamIdE();
    case ENGINE_ARP2600:     return a26FilterCutoffParamIdE();
    case ENGINE_ODYSSEY:     return odyFilterCutoffParamIdE();
    case ENGINE_MELLOTRON:   return melFilterCutoffParamIdE();
    case ENGINE_OBSEM:       return obFilterCutoffParamIdE();
    case ENGINE_OBX:         return obxFilterCutoffParamIdE();
    case ENGINE_PPGWAVE:     return ppgFilterCutoffParamIdE();
    case ENGINE_MOOGMODULAR: return mm2FilterCutoffParamIdE();
    case ENGINE_SYSTEM100:   return sy1FilterCutoffParamIdE();
    case ENGINE_JUPITER8:    return jp8FilterCutoffParamIdE();
    case ENGINE_MS20:        return ms20FilterCutoffParamIdE();
    case ENGINE_DX7:         return dxFilterCutoffParamIdE();
    case ENGINE_STRINGENS:   return seFilterCutoffParamIdE();
    case ENGINE_PROPHETVS:   return vsFilterCutoffParamIdE();
    default:                 return stubFilterCutoffParamIdE();
    }
}

uint8_t engineFilterResonanceParamId() {
    switch (currentEngine) {
    case ENGINE_CS80:        return cs80FilterResonanceParamIdE();
    case ENGINE_MINIMOOG:    return mmFilterResonanceParamIdE();
    case ENGINE_PROPHET5:    return p5FilterResonanceParamIdE();
    case ENGINE_PROPHET10:   return p10FilterResonanceParamIdE();
    case ENGINE_ARP2600:     return a26FilterResonanceParamIdE();
    case ENGINE_ODYSSEY:     return odyFilterResonanceParamIdE();
    case ENGINE_MELLOTRON:   return melFilterResonanceParamIdE();
    case ENGINE_OBSEM:       return obFilterResonanceParamIdE();
    case ENGINE_OBX:         return obxFilterResonanceParamIdE();
    case ENGINE_PPGWAVE:     return ppgFilterResonanceParamIdE();
    case ENGINE_MOOGMODULAR: return mm2FilterResonanceParamIdE();
    case ENGINE_SYSTEM100:   return sy1FilterResonanceParamIdE();
    case ENGINE_JUPITER8:    return jp8FilterResonanceParamIdE();
    case ENGINE_MS20:        return ms20FilterResonanceParamIdE();
    case ENGINE_DX7:         return dxFilterResonanceParamIdE();
    case ENGINE_STRINGENS:   return seFilterResonanceParamIdE();
    case ENGINE_PROPHETVS:   return vsFilterResonanceParamIdE();
    default:                 return stubFilterResonanceParamIdE();
    }
}

uint8_t engineNumVoices() {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80NumVoicesE();
    case ENGINE_MINIMOOG:  return mmNumVoicesE();
    case ENGINE_PROPHET5:  return p5NumVoicesE();
    case ENGINE_PROPHET10: return p10NumVoicesE();
    case ENGINE_ARP2600:   return a26NumVoicesE();
    case ENGINE_ODYSSEY:   return odyNumVoicesE();
    case ENGINE_MELLOTRON: return melNumVoicesE();
    case ENGINE_OBSEM:    return obNumVoicesE();
    case ENGINE_OBX:    return obxNumVoicesE();
    case ENGINE_PPGWAVE:    return ppgNumVoicesE();
    case ENGINE_MOOGMODULAR:    return mm2NumVoicesE();
    case ENGINE_SYSTEM100:    return sy1NumVoicesE();
    case ENGINE_JUPITER8:    return jp8NumVoicesE();
    case ENGINE_MS20:    return ms20NumVoicesE();
    case ENGINE_DX7:    return dxNumVoicesE();
    case ENGINE_STRINGENS:    return seNumVoicesE();
    case ENGINE_PROPHETVS:    return vsNumVoicesE();
    default:               return stubNumVoicesE();
    }
}
void engineVoiceInit() {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80VoiceInitE(); break;
    case ENGINE_MINIMOOG:  mmVoiceInitE();   break;
    case ENGINE_PROPHET5:  p5VoiceInitE();   break;
    case ENGINE_PROPHET10: p10VoiceInitE();  break;
    case ENGINE_ARP2600:   a26VoiceInitE();  break;
    case ENGINE_ODYSSEY:   odyVoiceInitE();  break;
    case ENGINE_MELLOTRON: melVoiceInitE();  break;
    case ENGINE_OBSEM:    obVoiceInitE(); break;
    case ENGINE_OBX:    obxVoiceInitE(); break;
    case ENGINE_PPGWAVE:    ppgVoiceInitE(); break;
    case ENGINE_MOOGMODULAR:    mm2VoiceInitE(); break;
    case ENGINE_SYSTEM100:    sy1VoiceInitE(); break;
    case ENGINE_JUPITER8:    jp8VoiceInitE(); break;
    case ENGINE_MS20:    ms20VoiceInitE(); break;
    case ENGINE_DX7:    dxVoiceInitE(); break;
    case ENGINE_STRINGENS:    seVoiceInitE(); break;
    case ENGINE_PROPHETVS:    vsVoiceInitE(); break;
    default:               stubVoiceInitE(); break;
    }
}
void engineNoteOn(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80NoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_MINIMOOG:  mmNoteOnE(voiceIdx, note, vel, trigEnv);   break;
    case ENGINE_PROPHET5:  p5NoteOnE(voiceIdx, note, vel, trigEnv);   break;
    case ENGINE_PROPHET10: p10NoteOnE(voiceIdx, note, vel, trigEnv);  break;
    case ENGINE_ARP2600:   a26NoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_ODYSSEY:   odyNoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_MELLOTRON: melNoteOnE(voiceIdx, note, vel, trigEnv);  break;
    case ENGINE_OBSEM:    obNoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_OBX:    obxNoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_PPGWAVE:    ppgNoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_MOOGMODULAR:    mm2NoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_SYSTEM100:    sy1NoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_JUPITER8:    jp8NoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_MS20:    ms20NoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_DX7:    dxNoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_STRINGENS:    seNoteOnE(voiceIdx, note, vel, trigEnv); break;
    case ENGINE_PROPHETVS:    vsNoteOnE(voiceIdx, note, vel, trigEnv); break;
    default:               stubNoteOnE(voiceIdx, note, vel, trigEnv); break;
    }
}
void engineNoteOff(uint8_t voiceIdx, int note) {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80NoteOffE(voiceIdx); break;
    case ENGINE_MINIMOOG:  mmNoteOffE(voiceIdx);   break;
    case ENGINE_PROPHET5:  p5NoteOffE(voiceIdx, note);   break;
    case ENGINE_PROPHET10: p10NoteOffE(voiceIdx, note);  break;
    case ENGINE_ARP2600:   a26NoteOffE(voiceIdx); break;
    case ENGINE_ODYSSEY:   odyNoteOffE(voiceIdx); break;
    case ENGINE_MELLOTRON: melNoteOffE(voiceIdx);  break;
    case ENGINE_OBSEM:    obNoteOffE(voiceIdx); break;
    case ENGINE_OBX:    obxNoteOffE(voiceIdx, note); break;
    case ENGINE_PPGWAVE:    ppgNoteOffE(voiceIdx); break;
    case ENGINE_MOOGMODULAR:    mm2NoteOffE(voiceIdx); break;
    case ENGINE_SYSTEM100:    sy1NoteOffE(voiceIdx); break;
    case ENGINE_JUPITER8:    jp8NoteOffE(voiceIdx); break;
    case ENGINE_MS20:    ms20NoteOffE(voiceIdx); break;
    case ENGINE_DX7:    dxNoteOffE(voiceIdx); break;
    case ENGINE_STRINGENS:    seNoteOffE(voiceIdx, note); break;
    case ENGINE_PROPHETVS:    vsNoteOffE(voiceIdx); break;
    default:               stubNoteOffE(voiceIdx); break;
    }
}
void engineAllOff() {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80AllOffE(); break;
    case ENGINE_MINIMOOG:  mmAllOffE();   break;
    case ENGINE_PROPHET5:  p5AllOffE();   break;
    case ENGINE_PROPHET10: p10AllOffE();  break;
    case ENGINE_ARP2600:   a26AllOffE(); break;
    case ENGINE_ODYSSEY:   odyAllOffE(); break;
    case ENGINE_MELLOTRON: melAllOffE();  break;
    case ENGINE_OBSEM:    obAllOffE(); break;
    case ENGINE_OBX:    obxAllOffE(); break;
    case ENGINE_PPGWAVE:    ppgAllOffE(); break;
    case ENGINE_MOOGMODULAR:    mm2AllOffE(); break;
    case ENGINE_SYSTEM100:    sy1AllOffE(); break;
    case ENGINE_JUPITER8:    jp8AllOffE(); break;
    case ENGINE_MS20:    ms20AllOffE(); break;
    case ENGINE_DX7:    dxAllOffE(); break;
    case ENGINE_STRINGENS:    seAllOffE(); break;
    case ENGINE_PROPHETVS:    vsAllOffE(); break;
    default:               stubAllOffE(); break;
    }
}
void engineSetVoiceAftertouch(uint8_t voiceIdx, uint8_t pressure) {
    switch (currentEngine) {
    case ENGINE_CS80:      cs80SetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_MINIMOOG:  mmSetVoiceAftertouchE(voiceIdx, pressure);   break;
    case ENGINE_PROPHET5:  p5SetVoiceAftertouchE(voiceIdx, pressure);   break;
    case ENGINE_PROPHET10: p10SetVoiceAftertouchE(voiceIdx, pressure);  break;
    case ENGINE_ARP2600:   a26SetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_ODYSSEY:   odySetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_MELLOTRON: melSetVoiceAftertouchE(voiceIdx, pressure);  break;
    case ENGINE_OBSEM:    obSetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_OBX:    obxSetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_PPGWAVE:    ppgSetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_MOOGMODULAR:    mm2SetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_SYSTEM100:    sy1SetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_JUPITER8:    jp8SetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_MS20:    ms20SetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_DX7:    dxSetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_STRINGENS:    seSetVoiceAftertouchE(voiceIdx, pressure); break;
    case ENGINE_PROPHETVS:    vsSetVoiceAftertouchE(voiceIdx, pressure); break;
    default:               stubSetVoiceAftertouchE(voiceIdx, pressure); break;
    }
}

int engineRenderBlock(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    switch (currentEngine) {
    case ENGINE_CS80:      return cs80RenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_MINIMOOG:  return mmRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_PROPHET5:  return p5RenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_PROPHET10: return p10RenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_ARP2600:   return a26RenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_ODYSSEY:   return odyRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_MELLOTRON: return melRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_OBSEM:    return obRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_OBX:    return obxRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_PPGWAVE:    return ppgRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_MOOGMODULAR:    return mm2RenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_SYSTEM100:    return sy1RenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_JUPITER8:    return jp8RenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_MS20:    return ms20RenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_DX7:    return dxRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_STRINGENS:    return seRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    case ENGINE_PROPHETVS:    return vsRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    default:               return stubRenderBlockE(audioBuf, pitchBend, modWheel, chanAftertouch);
    }
}
