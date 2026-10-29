// FxLxr.cpp - the four LXR-02 FX-bus effects (FxBus.cpp, ported from the firmware) as rack modules.
// They run in the hardware's 16-bit domain: the input is scaled to int16 range and saturated, as the
// LXR's FX send buffer is.
// SPDX-License-Identifier: GPL-3.0-only
#include "FxFactories.h"
#include "../dsp/FxBus.h"
#include <algorithm>
#include <cstring>

namespace ferrum::fx
{
namespace
{
class LxrModule : public Module
{
public:
    explicit LxrModule (Type t) : type (t) {}
    void prepare (double s) override { sr = s; bus.prepare (sr); first = true; }
    void reset() override { bus.reset(); }
    void process (float* L, float* R, int n, const float* p) override
    {
        if (first || std::memcmp (last, p, sizeof (last)) != 0)
        {
            std::memcpy (last, p, sizeof (last));
            first = false;
            bus.setSettings (settings (p));
        }
        int done = 0;
        while (done < n)
        {
            const int m = std::min (256, n - done);
            float inL[256], inR[256], outL[256], outR[256];
            for (int i = 0; i < m; ++i)
            {
                inL[i] = std::clamp (L[done + i] * 32768.f, -32768.f, 32767.f);
                inR[i] = std::clamp (R[done + i] * 32768.f, -32768.f, 32767.f);
                outL[i] = outR[i] = 0.f;
            }
            bus.process (inL, inR, outL, outR, m);
            std::memcpy (L + done, outL, sizeof (float) * (size_t) m);
            std::memcpy (R + done, outR, sizeof (float) * (size_t) m);
            done += m;
        }
    }

private:
    FxSettings settings (const float* p) const
    {
        const auto& in = info (type).params;
        auto ci = [&] (int i) { return choiceIndex (in[(size_t) i], p[i]); };
        FxSettings s;
        switch (type)
        {
            case Type::LxrDist:
                s.type = FxType::Distortion; s.distType = (DistType) ci (0);
                for (int i = 0; i < 4; ++i) s.p[i] = p[i + 1];
                s.dryWet = p[5];
                break;
            case Type::LxrRing:
                s.type = FxType::RingMod; s.p[0] = p[0]; s.ringWave = ci (1); s.dryWet = p[2];
                break;
            case Type::LxrComp:
                s.type = FxType::Compressor; s.p[0] = p[0]; s.p[1] = p[1]; s.p[2] = p[2]; s.ratio = ci (3); s.p[3] = p[4]; s.dryWet = p[5];
                break;
            case Type::LxrDelay:
            default:
                s.type = FxType::Delay; s.p[0] = p[0]; s.p[1] = p[1]; s.p[2] = p[2]; s.p[3] = p[3]; s.pingPong = ci (4) == 1; s.dryWet = p[5];
                break;
        }
        return s;
    }
    Type type;
    FxBus bus;
    float last[kMaxParams] {};
    bool first = true;
};
} // namespace

std::unique_ptr<Module> makeLxr (Type t) { return std::make_unique<LxrModule> (t); }
} // namespace ferrum::fx
