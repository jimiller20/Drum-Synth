// LxrVoice.cpp - see LxrVoice.h
// Copyright (C) 2012-2026 Erica Synths; port (C) 2026 Ferrum 8 contributors.
// SPDX-License-Identifier: GPL-3.0-only
#include "LxrVoice.h"

namespace ferrum
{
using namespace lxr;

void Voice::prepare (double sampleRate, uint32_t seed)
{
    sr = sampleRate;
    srRatio = (float) (kLxrFs / sampleRate);
    ctrlLen = std::clamp ((int) std::lround (kLxrBlock * sampleRate / kLxrFs), 8, kMaxCtrlLen);
    filter.srRatio = srRatio;
    volSmooth.setCoefAt44k (0.01, srRatio);
    dist.smooth.setCoefAt44k (0.01, srRatio);
    rng.seed (seed * 2654435761u + 1u);
    hitRng.seed (seed * 747796405u + 3u);
    shelfA = (float) (1.0 - std::exp (-2.0 * 3.14159265358979323846 * 2200.0 / sampleRate));
    lfoRng.seed (seed * 40503u + 7u);
    reset();
    for (int p = 0; p < NUM_PATCH_PARAMS; ++p) applyParam (p, base[(size_t) p]);
    volSmooth.b = vol;
    dist.smooth.b = dist.shape;
}

void Voice::reset()
{
    volEg = SlopeEg2 {};
    pitchEg.value = 0;
    filter.reset();
    trans.phase = kTransientEnd;
    snap.value = 0;
    ampIn = lastGain = 0;
    blockPos = blockLen = 0;
    decimHold = 0; decimCnt = 0;
    chokeGain = 1.f; chokeStep = 0.f;
    silent = true;
    hitOff.fill (0.f);
    hitGain = hitPitch = shelfG = rampPos = 1.f; rampInc = 0.f; shelfZ = 0.f;
    // the SlopeEg2 defaults were replaced above; restore the patch's envelope settings
    applyParam (P_ATTACK, base[P_ATTACK]);
    applyParam (P_DECAY, base[P_DECAY]);
    applyParam (P_SLOPE, base[P_SLOPE]);
    applyParam (P_REPEAT, base[P_REPEAT]);
}

void Voice::setModelState()
{
    // per-model oscillator setup from Snare_init(), Cymbal_init(), HiHat_init()
    osc.fmMod = (currentModel == Model::Cymbal || currentModel == Model::HiHat) ? 1.0f : 0.0f;
    osc.pitchMod = modOsc.pitchMod = modOsc2.pitchMod = noiseOsc.pitchMod = 1.0f;
    noiseOsc.waveform = WAVE_TRI; // Snare_init: the noise "osc" is waveform 1, so its rate uses the wavetable formula
    reset();
}

void Voice::setParam (int p, float v)
{
    v = clamp01 (v);
    base[(size_t) p] = v;
    applyParam (p, eff (p));
}

void Voice::setHitOffset (int p, float off)
{
    if (off == hitOff[(size_t) p]) return;
    hitOff[(size_t) p] = off;
    applyParam (p, eff (p));
}

void Voice::applyParam (int p, float v)
{
    const float d2 = v * 127.0f;
    switch (p)
    {
        case P_MODEL:
        {
            const Model m = (Model) choiceIndex (v, 4);
            if (m != currentModel)
            {
                currentModel = m;
                setModelState();
                for (int q = 0; q < NUM_PATCH_PARAMS; ++q) if (q != P_MODEL) applyParam (q, eff (q));
            }
            break;
        }
        case P_OSC_WAVE:    osc.waveform = (uint8_t) choiceIndex (v, NUM_OSC_WAVES); break;
        case P_OSC_COARSE:  osc.coarse = d2; osc.recalcFreq(); break;
        case P_OSC_FINE:    osc.fine = d2; osc.recalcFreq(); break;
        case P_PWM:         pwm = v; break;
        case P_MOD_WAVE:    modOsc.waveform = (uint8_t) choiceIndex (v, NUM_OSC_WAVES); break;
        case P_MOD_COARSE:  modOsc.coarse = d2; modOsc.recalcFreq(); break;
        case P_MOD_GAIN:    fmModAmount = v; break;
        case P_MOD2_WAVE:   modOsc2.waveform = (uint8_t) choiceIndex (v, NUM_OSC_WAVES); break;
        case P_MOD2_COARSE: modOsc2.coarse = d2; modOsc2.recalcFreq(); break;
        case P_MOD2_GAIN:   fmModAmount2 = v; break;
        case P_FM_MODE:     mixOscs = choiceIndex (v, 2) == 1; break;
        case P_NOISE_FREQ:  noiseOsc.freq = v * 22000.0f; break;
        case P_NOISE_MIX:   noiseMix = v; break;
        case P_PITCH_AMT:   egPitchModAmount = v * v * 32.0f; break; // calcPitchModAmount
        case P_PITCH_DECAY: pitchEg.setDecay (d2); break;
        case P_PITCH_SLOPE: pitchEg.setSlope (d2); break;
        case P_ATTACK:      volEg.setAttack (d2); break;
        case P_DECAY:       volEg.setDecay (d2); break;
        case P_SLOPE:       volEg.setSlope (d2); break;
        case P_REPEAT:      volEg.repeat = (int) std::lround (v * v * 127.0f); break;
        case P_FILTER_TYPE: filterType = (uint8_t) (choiceIndex (v, 8) + 1); break;
        case P_FILTER_FREQ: filter.setValue (valueShaperF2F (v, -0.9f)); break; // FILTER_SHAPER
        case P_FILTER_RESO: filter.setReso (v); break;
        case P_FILTER_DRIVE: filter.setDrive (v); break;
        case P_TRANS_WAVE:  trans.waveform = (uint8_t) choiceIndex (v, 14); break;
        case P_TRANS_VOL:   trans.volume = v; break;
        case P_TRANS_FREQ:  trans.pitch = 1.f + ((d2 / 33.9f) - 0.75f); break;
        case P_DRIVE:       dist.setShape (v); break;
        case P_DECIMATE:    decimRate = valueShaperF2F (v, -0.7f); break;
        case P_LEVEL:       vol = v; break;
        case P_PAN:         pan = d2; break;
        case P_VEL_VOL:     velToVol = v; break;
        case P_VEL_AMT:     velAmount = v; break;
        case P_VEL_DEST:    velDest = modTargets()[(size_t) choiceIndex (v, modTargetCount())]; break;
        case P_LFO_WAVE:    lfo.waveform = (uint8_t) choiceIndex (v, NUM_LFO_WAVES); break;
        case P_LFO_FREQ:    lfo.setFreq (v); lfo.updateInc (bpm); break;
        case P_LFO_SYNC:    lfo.sync = (uint8_t) choiceIndex (v, 12); lfo.updateInc (bpm); break;
        case P_LFO_PHASE:   lfo.phaseOffset = armF2U (v * 4294967295.0); break;
        case P_LFO_AMT:     lfoAmount = v; break;
        case P_LFO_DEST:    lfoDest = modTargets()[(size_t) choiceIndex (v, modTargetCount())]; break;
        default: break; // P_OUTPUT, P_CHOKE, P_LFO_RETRIG are read directly from base
    }
}

void Voice::choke()
{
    if (! silent && chokeStep == 0.f)
        chokeStep = (float) (1.0 / (0.003 * sr)); // ~3 ms fade
}

void Voice::trigger (float velocity01, int note, const Articulation& art)
{
    // ---- articulation: ghost level + expression lanes + humanize + variance -> this hit only.
    // With the defaults every term below is exactly neutral, so the LXR sound is unchanged.
    const int L = std::clamp ((int) art.ghost, 0, 3);
    const float k = 2.0f * base[P_GHOST];                        // ghost depth (1 = default)
    static const float kGainDb[4]  = { 0.f, -3.f, -6.5f, -11.f }; // softer...
    static const float kShelfDb[4] = { 0.f, -3.f, -6.5f, -11.f }; // ...less top end...
    static const float kAttMs[4]   = { 0.f, 0.6f, 1.6f, 3.2f };   // ...rounder attack...
    static const float kDecay[4]   = { 0.f, -0.015f, -0.035f, -0.06f }; // ...a bit shorter...
    static const float kTrans[4]   = { 1.f, 0.7f, 0.45f, 0.25f }; // ...less click / snap...
    static const float kPAmt[4]    = { 1.f, 0.85f, 0.7f, 0.55f }; // ...less pitch bend (softer stick)...
    static const float kPitch[4]   = { 0.f, -0.05f, -0.1f, -0.18f }; // ...and a hair lower

    float gainDb = k * kGainDb[L];
    float toneDb = k * kShelfDb[L] + (art.tone < 0 ? 1.5f : 0.75f) * (float) art.tone;
    const float attMs = k * kAttMs[L];
    float decayOff = k * kDecay[L] + 0.035f * (float) art.decay;
    float transMul = std::max (0.f, 1.f - k * (1.f - kTrans[L]));
    const float pAmtMul = std::max (0.f, 1.f - k * (1.f - kPAmt[L]));
    float semis = (float) art.pitch + k * kPitch[L];
    float filterOff = 0.f;
    const float ghostVel = L > 0 ? std::pow (10.f, gainDb / 20.f) : 1.f; // velocity seen by Vel > target

    const float human = base[P_HUMANIZE];
    if (human > 0.f) // dynamics that wander from hit to hit like a player's (random walk, not white noise)
    {
        for (auto& d : drift) d = 0.65f * d + 0.35f * bip();
        gainDb += human * 3.0f * drift[0];
        toneDb += human * 2.5f * drift[1];
        semis += human * 0.1f * drift[2];
        decayOff += human * 0.025f * drift[3];
    }
    const float vary = base[P_VARIANCE];
    if (vary > 0.f) // independent per-hit randomness: happy accidents
    {
        decayOff += vary * 0.12f * bip();
        semis += vary * 0.8f * bip();
        toneDb += vary * 6.f * bip();
        transMul *= std::max (0.f, 1.f + vary * 0.7f * bip());
        filterOff += vary * 0.12f * bip();
        gainDb += vary * 1.5f * bip();
    }
    // metals (cymbal, hi-hat): with Vary, every hit strikes the metal a little differently - the
    // modulators drift against the carrier, their FM depth changes and (below) they start at random
    // phases, so the partials and the wash differ from hit to hit like a real cymbal
    const bool metal = currentModel == Model::Cymbal || currentModel == Model::HiHat;
    float modDetune = 0.f, mod2Detune = 0.f, modDepth = 0.f, mod2Depth = 0.f;
    if (vary > 0.f && metal)
    {
        modDetune = vary * (0.9f / 127.f) * bip();   // up to +-0.9 semitone
        mod2Detune = vary * (0.9f / 127.f) * bip();
        modDepth = vary * 0.12f * bip();
        mod2Depth = vary * 0.12f * bip();
    }
    setHitOffset (P_MOD_COARSE, modDetune);
    setHitOffset (P_MOD2_COARSE, mod2Detune);
    setHitOffset (P_MOD_GAIN, modDepth);
    setHitOffset (P_MOD2_GAIN, mod2Depth);

    hitGain = gainDb == 0.f ? 1.f : std::pow (10.f, gainDb / 20.f);
    shelfG = toneDb == 0.f ? 1.f : std::pow (10.f, std::clamp (toneDb, -24.f, 9.f) / 20.f);
    rampInc = attMs > 0.f ? (float) (1.0 / (attMs * 0.001 * sr)) : 0.f;
    rampPos = attMs > 0.f ? 0.f : 1.f;
    hitPitch = semis == 0.f ? 1.f : std::pow (2.f, semis / 12.f);
    setHitOffset (P_DECAY, decayOff);
    setHitOffset (P_TRANS_VOL, base[P_TRANS_VOL] * (transMul - 1.f));
    setHitOffset (P_PITCH_AMT, base[P_PITCH_AMT] * (pAmtMul - 1.f));
    setHitOffset (P_FILTER_FREQ, filterOff);

    veloVol = clamp01 (velocity01);
    velo = clamp01 (velocity01 * ghostVel);
    float offset = 1;
    if (trans.waveform == 1) offset -= trans.volume; // "offset" transient = start phase
    osc.resetPhaseForTrigger (offset);

    switch (currentModel)
    {
        case Model::Drum:
            modOsc.resetPhaseForTrigger (1.0f);
            osc.setBaseNote (note); modOsc.setBaseNote (note);
            pitchEg.trigger();
            filter.reset(); // reset filter to prevent wrong transient
            break;
        case Model::Snare:
            pitchEg.trigger();
            osc.setBaseNote (note);
            break;
        case Model::Cymbal:
        case Model::HiHat:
        default:
            modOsc.phase = 0; modOsc2.phase = 0;
            if (vary > 0.f) { modOsc.phase = hitRng.next(); modOsc2.phase = hitRng.next(); }
            osc.setBaseNote (note); modOsc.setBaseNote (note); modOsc2.setBaseNote (note);
            break;
    }
    volEg.trigger();
    trans.trigger();
    snap.trigger();

    // USE_TRIGGERED_PAN: pan is sampled when the voice is triggered
    triggeredPanL = std::sqrt ((127.f - pan) / 127.f);
    triggeredPanR = std::sqrt (pan / 127.f);

    chokeGain = 1.f; chokeStep = 0.f;
    silent = false;
    blockPos = blockLen; // start a fresh control block on this exact sample
}

void Voice::applyModulation()
{
    // modNode_resetTargets: give previous targets back their own value
    if (prevLfoDest >= 0 && prevLfoDest != lfoDest && prevLfoDest != velDest) applyParam (prevLfoDest, eff (prevLfoDest));
    if (prevVelDest >= 0 && prevVelDest != velDest && prevVelDest != lfoDest) applyParam (prevVelDest, eff (prevVelDest));
    prevLfoDest = lfoDest; prevVelDest = velDest;

    lfoValue = lfo.calc (lfoRng);
    auto modded = [this] (int d)
    {
        float v = base[(size_t) d] + hitOff[(size_t) d];
        if (d == lfoDest) v += lfoAmount * (lfoValue * 2.f - 1.f);
        if (d == velDest) v += velAmount * (velo * 2.f - 1.f);
        applyParam (d, clamp01 (v));
    };
    if (lfoDest >= 0) modded (lfoDest);
    if (velDest >= 0 && velDest != lfoDest) modded (velDest);
}

void Voice::controlBlock()
{
    applyModulation();
    filter.recalc();

    switch (currentModel)
    {
        case Model::Drum: // calcDrumVoiceAsync
        {
            const float egPitchVal = pitchEg.calc();
            osc.pitchMod = 1 + egPitchVal * egPitchModAmount;
            if (trans.waveform == 0) osc.pitchMod += snap.calc (trans.pitch) * trans.volume;
            osc.fmMod = fmModAmount * egPitchVal;
            const bool instantAttack = volEg.attack >= 1.0 && (volEg.state == SlopeEg2::ATTACK || volEg.state == SlopeEg2::REPEAT);
            if (instantAttack) { ampIn = volEg.calc(); lastGain = ampIn; }
            else { lastGain = ampIn; ampIn = volEg.calc(); }
            osc.pitchMod *= bendFactor * hitPitch;
            modOsc.pitchMod = bendFactor * hitPitch;
            osc.setFreq (sr); modOsc.setFreq (sr);
            break;
        }
        case Model::Snare: // Snare_calcAsync - the amp envelope is stepped once per block
        {
            const float egPitchVal = pitchEg.calc();
            osc.pitchMod = 1 + egPitchVal * egPitchModAmount;
            ampIn = lastGain = volEg.calc();
            if (trans.waveform == 0) osc.pitchMod += snap.calc (trans.pitch) * trans.volume;
            osc.pitchMod *= bendFactor * hitPitch;
            osc.setFreq (sr); noiseOsc.setFreq (sr);
            break;
        }
        case Model::Cymbal: // Cymbal_calcAsync / HiHat_calcAsync
        case Model::HiHat:
        default:
        {
            ampIn = lastGain = volEg.calc();
            osc.pitchMod = 1.0f;
            if (trans.waveform == 0) osc.pitchMod = 1 + snap.calc (trans.pitch) * trans.volume;
            osc.pitchMod *= bendFactor * hitPitch;
            modOsc.pitchMod = modOsc2.pitchMod = bendFactor * hitPitch;
            osc.setFreq (sr); modOsc.setFreq (sr); modOsc2.setFreq (sr);
            break;
        }
    }
}

void Voice::renderBlock (int16_t* buf, int n)
{
    int16_t a[kMaxCtrlLen], b[kMaxCtrlLen];
    const float velGain = 1.f - velToVol * (1.f - veloVol);

    switch (currentModel)
    {
        case Model::Drum: // calcDrumVoiceSyncBlock
        {
            modOsc.block (a, n, fmModAmount, pwm, rng);
            if (mixOscs)
            {
                osc.block (buf, n, 1.f - fmModAmount, pwm, rng);
                for (int i = 0; i < n; ++i) buf[i] = qadd16 (buf[i], a[i]);
            }
            else osc.fmBlock (a, buf, n, 1.0f, pwm, rng);
            trans.block (a, n, srRatio);
            for (int i = 0; i < n; ++i) buf[i] = qadd16 (buf[i], a[i]);
            filter.process (filterType, buf, n);
            for (int i = 0; i < n; ++i) // bufferTool_addGainInterpolated
            {
                const double frac = n > 1 ? i / (n - 1.0) : 1.0;
                buf[i] = d2s16 (buf[i] * (lastGain + frac * (ampIn - lastGain)));
            }
            for (int i = 0; i < n; ++i) buf[i] = f2s16 (buf[i] * velGain);
            dist.process (buf, n);
            for (int i = 0; i < n; ++i) buf[i] = f2s16 (buf[i] * volSmooth.process (vol));
            break;
        }
        case Model::Snare: // Snare_calcSyncBlock
        {
            noiseOsc.noiseBlock (buf, n, 0.9f, rng);
            filter.process (filterType, buf, n);
            osc.block (a, n, 1.f - noiseMix, pwm, rng);
            trans.block (b, n, srRatio);
            const float eg = (float) ampIn * velGain;
            for (int j = 0; j < n; ++j)
            {
                float tmp = std::fma ((float) buf[j], noiseMix, (float) a[j]);
                tmp += b[j];
                buf[j] = f2s16 (volSmooth.process (vol) * eg * tmp);
            }
            dist.process (buf, n);
            break;
        }
        case Model::Cymbal:
        case Model::HiHat:
        default:
        {
            modOsc.block (a, n, fmModAmount, pwm, rng);
            modOsc2.block (b, n, fmModAmount2, pwm, rng);
            for (int i = 0; i < n; ++i) a[i] = qadd16 (a[i], b[i]);
            osc.fmBlock (a, buf, n, currentModel == Model::HiHat ? 0.5f : 1.0f, pwm, rng);
            filter.process (filterType, buf, n);
            trans.block (a, n, srRatio);
            const float eg = (float) ampIn * velGain;
            for (int j = 0; j < n; ++j)
            {
                const int16_t s = qadd16 (buf[j], a[j]);
                buf[j] = f2s16 (s * eg * volSmooth.process (vol));
            }
            dist.process (buf, n);
            break;
        }
    }
}

void Voice::render (float* out, int n)
{
    int i = 0;
    while (i < n)
    {
        if (blockPos >= blockLen)
        {
            if (silent)
            {
                std::fill (out + i, out + n, 0.0f);
                return;
            }
            controlBlock();
            renderBlock (block, ctrlLen);
            blockLen = ctrlLen; blockPos = 0;
            if (volEg.isIdle() && ampIn <= 0.0 && lastGain <= 0.0)
            {
                silent = true; // the envelope has closed: nothing more to hear after this block
                decimHold = 0;
            }
        }
        const int k = std::min (n - i, blockLen - blockPos);
        const float inc = decimRate * srRatio;
        for (int j = 0; j < k; ++j)
        {
            const int16_t s = block[blockPos + j];
            if (decimRate >= 0.999f) decimHold = s; // mixer_decimateBlock (bypassed at full rate)
            else
            {
                decimCnt += inc;
                if (decimCnt >= 1.f) { decimCnt -= 1.f; decimHold = s; }
            }
            float g = 1.0f;
            if (chokeStep > 0.f)
            {
                chokeGain -= chokeStep;
                if (chokeGain <= 0.f)
                {
                    chokeGain = 0.f; chokeStep = 0.f;
                    volEg.state = SlopeEg2::STOPPED; volEg.value = 0; ampIn = lastGain = 0;
                    silent = true; decimHold = 0;
                }
                g = chokeGain;
            }
            else if (chokeGain <= 0.f) g = 0.f;
            float x = (float) decimHold * (1.0f / 32768.0f);
            shelfZ += shelfA * (x - shelfZ);                        // tone / ghost high shelf
            if (shelfG != 1.f) x = shelfZ + shelfG * (x - shelfZ);
            if (rampPos < 1.f) { x *= rampPos; rampPos = std::min (1.f, rampPos + rampInc); } // softened attack
            out[i + j] = x * g * hitGain;
        }
        blockPos += k;
        i += k;
        if (silent && chokeGain <= 0.f) { std::fill (out + i, out + n, 0.0f); blockPos = blockLen; return; }
    }
}
} // namespace ferrum
