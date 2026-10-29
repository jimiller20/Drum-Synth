/* fw_glue.c - drives the original LXR-02 voice code the way MidiParser.c's CC handler does */
#include "fw_glue.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/MIDI/valueShaper.h"

/* ---- stubs for the parts of the firmware we do not build ---- */
RNG_HandleTypeDef hrng;
static uint32_t rngState = 0x9E3779B9u;
uint32_t GetRngValue (void) { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }
void initRng (void) {}
float adc_getFaderVolume (uint8_t v) { (void) v; return 1.0f; }
void trigger_triggerVoice (uint8_t v, uint8_t o) { (void) v; (void) o; }
uint8_t trigger_isGateModeOn (void) { return 0; }
void voiceControl_noteOff (uint8_t v) { (void) v; }
float seq_getBpm (void) { return 120.f; }
ModulationNode velocityModulators[6];
void modNode_init (ModulationNode* vm) { (void) vm; }
void modNode_updateValue (ModulationNode* vm, float val) { (void) vm; (void) val; }
void modNode_resetTargets (void) {}
void modNode_reassignVeloMod (void) {}
void modNode_originalValueChanged (uint16_t i) { (void) i; }
void modNode_setDestination (ModulationNode* vm, uint16_t d) { (void) vm; (void) d; }
#define SEMITONE_UP 1.0594630943592952645618252949463f
float midiParser_calcDetune (uint8_t value) { float frac = (value / 127.f - 0.5f); return 1 + frac * (SEMITONE_UP - 1); }

void fw_init (uint32_t s)
{
    rngState = s ? s : 0x9E3779B9u;
    /* the firmware keeps voices in zero-initialised RAM; start every scenario from that state */
    memset (voiceArray, 0, sizeof (voiceArray));
    memset (&snareVoice, 0, sizeof (snareVoice));
    memset (&cymbalVoice, 0, sizeof (cymbalVoice));
    memset (&hatVoice, 0, sizeof (hatVoice));
    initDrumVoice(); Snare_init(); Cymbal_init(); HiHat_init();
}

static OscInfo* oscOf (int v) { return v < 3 ? &voiceArray[v].osc : v == 3 ? &snareVoice.osc : v == 4 ? &cymbalVoice.osc : &hatVoice.osc; }
static ResonantFilter* filterOf (int v) { return v < 3 ? &voiceArray[v].filter : v == 3 ? &snareVoice.filter : v == 4 ? &cymbalVoice.filter : &hatVoice.filter; }
static SlopeEg2* egOf (int v) { return v < 3 ? &voiceArray[v].oscVolEg : v == 3 ? &snareVoice.oscVolEg : v == 4 ? &cymbalVoice.oscVolEg : &hatVoice.oscVolEg; }
static TransientGenerator* transOf (int v) { return v < 3 ? &voiceArray[v].transGen : v == 3 ? &snareVoice.transGen : v == 4 ? &cymbalVoice.transGen : &hatVoice.transGen; }
static Distortion* distOf (int v) { return v < 3 ? &voiceArray[v].distortion : v == 3 ? &snareVoice.distortion : v == 4 ? &cymbalVoice.distortion : &hatVoice.distortion; }
static uint8_t* ftypeOf (int v) { return v < 3 ? &voiceArray[v].filterType : v == 3 ? &snareVoice.filterType : v == 4 ? &cymbalVoice.filterType : &hatVoice.filterType; }
static OscInfo* modOf (int v) { return v < 3 ? &voiceArray[v].modOsc : v == 4 ? &cymbalVoice.modOsc : &hatVoice.modOsc; }
static OscInfo* mod2Of (int v) { return v == 4 ? &cymbalVoice.modOsc2 : &hatVoice.modOsc2; }

void fw_set (int v, int param, int itg)
{
    const float flt = itg / 127.f;
    OscInfo* o = oscOf (v);
    switch (param)
    {
        case FW_OSC_WAVE: o->waveform = (uint8_t) itg; break;
        case FW_COARSE: o->midiFreq &= 0x00ff; o->midiFreq |= (uint16_t) (itg << 8); osc_recalcFreq (o); break;
        case FW_FINE: o->midiFreq &= 0xff00; o->midiFreq |= (uint16_t) itg; osc_recalcFreq (o); break;
        case FW_MOD_WAVE: modOf (v)->waveform = (uint8_t) itg; break;
        case FW_MOD_COARSE: { OscInfo* m = modOf (v); m->midiFreq &= 0x00ff; m->midiFreq |= (uint16_t) (itg << 8); osc_recalcFreq (m); break; }
        case FW_MOD_GAIN: if (v < 3) voiceArray[v].fmModAmount = flt; else if (v == 4) cymbalVoice.fmModAmount1 = flt; else hatVoice.fmModAmount1 = flt; break;
        case FW_MOD2_WAVE: mod2Of (v)->waveform = (uint8_t) itg; break;
        case FW_MOD2_COARSE: { OscInfo* m = mod2Of (v); m->midiFreq &= 0x00ff; m->midiFreq |= (uint16_t) (itg << 8); osc_recalcFreq (m); break; }
        case FW_MOD2_GAIN: if (v == 4) cymbalVoice.fmModAmount2 = flt; else hatVoice.fmModAmount2 = flt; break;
        case FW_MIX_MODE: voiceArray[v].mixOscs = itg != 0; break;
        case FW_NOISE_FREQ: snareVoice.noiseOsc.freq = flt * 22000; break;
        case FW_NOISE_MIX: snareVoice.mix = flt; break;
        case FW_PITCH_AMT: { const float a = flt * flt * PITCH_AMOUNT_FACTOR; if (v < 3) voiceArray[v].egPitchModAmount = a; else snareVoice.egPitchModAmount = a; break; }
        case FW_PITCH_DECAY: DecayEg_setDecay (v < 3 ? &voiceArray[v].oscPitchEg : &snareVoice.oscPitchEg, (uint8_t) itg); break;
        case FW_PITCH_SLOPE: DecayEg_setSlope (v < 3 ? &voiceArray[v].oscPitchEg : &snareVoice.oscPitchEg, (uint8_t) itg); break;
        case FW_ATTACK: slopeEg2_setAttack (egOf (v), (uint8_t) itg, 0); break;
        case FW_DECAY:
            if (v == 5) { hatVoice.decayClosed = slopeEg2_calcDecay ((uint8_t) itg); hatVoice.decayOpen = hatVoice.decayClosed; }
            else slopeEg2_setDecay (egOf (v), (uint8_t) itg, 0);
            break;
        case FW_SLOPE: slopeEg2_setSlope (egOf (v), (uint8_t) itg); break;
        case FW_REPEAT: egOf (v)->repeat = (uint8_t) itg; break;
        case FW_FILTER_TYPE: *ftypeOf (v) = (uint8_t) (itg + 1); break;
        case FW_FILTER_FREQ: SVF_directSetFilterValue (filterOf (v), valueShaperF2F (flt, FILTER_SHAPER)); break;
        case FW_FILTER_RESO: SVF_setReso (filterOf (v), flt); break;
        case FW_FILTER_DRIVE: SVF_setDrive (filterOf (v), flt); break;
        case FW_TRANS_WAVE: transient_setWaveform (transOf (v), (uint8_t) itg); break;
        case FW_TRANS_VOL: transOf (v)->volume = flt; break;
        case FW_TRANS_FREQ: transOf (v)->pitch = 1.f + ((itg / 33.9f) - 0.75f); break;
        case FW_DRIVE: setDistortionShape (distOf (v), flt); break;
        case FW_VOL: if (v < 3) voiceArray[v].vol = flt; else if (v == 3) snareVoice.vol = flt; else if (v == 4) cymbalVoice.vol = flt; else hatVoice.vol = flt; break;
        case FW_PWM: if (v < 3) voiceArray[v].pwm = flt; else if (v == 3) snareVoice.pwm = flt; else if (v == 4) cymbalVoice.pwm = flt; else hatVoice.pwm = flt; break;
        case FW_VOLMOD: if (v < 3) voiceArray[v].volumeMod = (uint8_t) itg; else if (v == 3) snareVoice.volumeMod = (uint8_t) itg; else if (v == 4) cymbalVoice.volumeMod = (uint8_t) itg; else hatVoice.volumeMod = (uint8_t) itg; break;
        default: break;
    }
}

/* the hardware's parameter smoothers have long settled by the time a note plays */
void fw_settle (int v)
{
    if (v < 3)
    {
        voiceArray[v].FaderVolumeFilter.b = 1.0;
        voiceArray[v].MixerPageFilter[0].b = voiceArray[v].vol;
        voiceArray[v].distortion.DriveParamFilter.b = voiceArray[v].distortion.shape;
    }
    else if (v == 3) snareVoice.FaderVolumeFilter.b = 1.0;
    else if (v == 4) cymbalVoice.FaderVolumeFilter.b = 1.0;
    else hatVoice.FaderVolumeFilter.b = 1.0;
}

void fw_trigger (int v, int vel, int note)
{
    if (v < 3) Drum_trigger ((uint8_t) v, (uint8_t) vel, (uint8_t) note);
    else if (v == 3) Snare_trigger ((uint8_t) vel, (uint8_t) note);
    else if (v == 4) Cymbal_trigger ((uint8_t) vel, (uint8_t) note);
    else HiHat_trigger ((uint8_t) vel, 0, (uint8_t) note);
}

void fw_render (int v, int16_t* out)
{
    SVF_recalcFreq (filterOf (v));
    if (v < 3) { calcDrumVoiceAsync ((uint8_t) v); calcDrumVoiceSyncBlock ((uint8_t) v, out, 64); }
    else if (v == 3) { Snare_calcAsync(); Snare_calcSyncBlock (out, 64); }
    else if (v == 4) { Cymbal_calcAsync(); Cymbal_calcSyncBlock (out, 64); }
    else { HiHat_calcAsync(); HiHat_calcSyncBlock (out, 64); }
}
