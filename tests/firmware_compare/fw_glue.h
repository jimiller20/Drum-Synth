/* fw_glue.h - plain C interface to the original LXR-02 voice code (built for the host) */
#ifndef FW_GLUE_H
#define FW_GLUE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum FwParam { FW_OSC_WAVE, FW_COARSE, FW_FINE, FW_MOD_WAVE, FW_MOD_COARSE, FW_MOD_GAIN, FW_MOD2_WAVE, FW_MOD2_COARSE,
               FW_MOD2_GAIN, FW_MIX_MODE, FW_NOISE_FREQ, FW_NOISE_MIX, FW_PITCH_AMT, FW_PITCH_DECAY, FW_PITCH_SLOPE,
               FW_ATTACK, FW_DECAY, FW_SLOPE, FW_REPEAT, FW_FILTER_TYPE, FW_FILTER_FREQ, FW_FILTER_RESO, FW_FILTER_DRIVE,
               FW_TRANS_WAVE, FW_TRANS_VOL, FW_TRANS_FREQ, FW_DRIVE, FW_VOL, FW_PWM, FW_VOLMOD };
/* voice: 0-2 drum, 3 snare, 4 cymbal, 5 hi-hat */
void fw_init (uint32_t rngState);
void fw_set (int voice, int param, int value127);
void fw_settle (int voice);
void fw_trigger (int voice, int vel, int note);
void fw_render (int voice, int16_t* out64);
#ifdef __cplusplus
}
#endif
#endif
