// Randomizer.h - patch and pattern randomisation.
// Patches are drawn from per-instrument "recipes" written in LXR parameter units, so a random
// kick is still a kick; "Chaos" ignores the recipes. Pattern randomisation picks rhythms that suit
// each channel's sound (kick on downbeats, hats on eighths, euclidean percussion ...).
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../dsp/PatchParams.h"
#include "Patterns.h"
#include <random>

namespace ferrum
{
enum class Flavor : int { Kick = 0, Tom, Perc, Zap, Snare, Clap, ClosedHat, OpenHat, Cymbal, Chaos, Count };
extern const char* const kFlavorNames[(int) Flavor::Count];

// genres for the sound dice: each one shapes the recipes (a Drum & Bass snare is tight, high and
// cracking, a Trap kick is a long 808, a Boom Bap hat is dusty ...)
enum class Genre : int { Any = 0, House, Techno, DnB, Jungle, Trap, BoomBap, LoFi, Electro, Garage, Dubstep, Industrial, Dembow, Footwork, Count };
extern const char* const kGenreNames[(int) Genre::Count];
// curated cymbals: each type has its own carrier / modulator ratios, filter and decay ranges
enum class CymbalType : int { Crash = 0, Ride, Splash, China, Cymbal808, Bell, Count };
extern const char* const kCymbalTypeNames[(int) CymbalType::Count];

// rhythm styles for the beat generator
enum class BeatStyle : int { Auto = 0, House, Breakbeat, DnB, Trap, Dembow, Electro, Footwork, Euclid, Glitch, Count };
extern const char* const kBeatStyleNames[(int) BeatStyle::Count];
// a character applied to every sound of a random kit so the eight sounds belong together
enum class KitCharacter : int { Auto = 0, Clean, Analog, Gritty, LoFi, Metallic, Count };
extern const char* const kKitCharacterNames[(int) KitCharacter::Count];
extern const char* const kKeyNames[12];

struct DiceSettings
{
    BeatStyle style = BeatStyle::Auto;
    float density = 0.5f;       // sparse .. busy
    float complexity = 0.5f;    // straight .. syncopated
    float expression = 0.5f;    // how many ghosts / nudges / chance / rolls
    float mutation = 0.35f;     // size of changes for variations and Evolve
    KitCharacter character = KitCharacter::Auto;
    int key = -1;               // -1 off, 0..11 = C..B: pitched drums snap to a minor pentatonic in that key
    int evolve = 0;             // 0 off, 1 orbit (variations around the original), 2 drift (keeps wandering)
    int evolveEvery = 2;        // pattern loops between changes
    bool allPatterns = false;   // Beat button: false = selected pattern, true = A-L as a set of related patterns
    int genre = -1;             // sound genre: -1 follows the beat style, else a Genre
};
using ChannelMask = std::array<bool, kNumChannels>; // true = leave this channel alone

class Randomizer
{
public:
    Randomizer();
    explicit Randomizer (uint32_t seed) : rng (seed) {}

    // routing (output, choke) and the LFO retrigger source are kept from 'current'
    Patch patch (Flavor f, const Patch& current, Genre g = Genre::Any);
    Patch patchForModel (const Patch& current, Genre g = Genre::Any); // "Randomize Drum Patch": keeps the model
    Patch cymbal (CymbalType t, const Patch& current, Genre g = Genre::Any);
    // Sound mutation: small moves of the sound-shaping parameters (pitch, decay, tone, filter, noise,
    // FM depth, transient, drive) around 'base'. depth 0..1. Model, level, pan and routing never change.
    // key >= 0: pitched drums move by steps of the minor pentatonic in that key and stay in tune
    Patch mutateSound (const Patch& base, float depth, int key = -1);
    CymbalType cymbalTypeFor (Genre g);                    // a type that suits the genre (weighted)
    Genre genreFor (const DiceSettings& s);                // resolves "follow the beat style"
    Patch alter (const Patch& p, float amount = 0.12f);    // "Alter Drum Patch"
    // Partial dice: 'cur' moved 'amount' (0..1) of the way to 'target'. Continuous parameters blend;
    // a choice parameter switches with probability amount^2 (the model only at amount >= 0.98).
    Patch blendToward (const Patch& cur, const Patch& target, float amount);
    // Partial beat dice: takes about amount * (number of steps that differ) steps of 'fresh' into 'cur',
    // at least one when anything differs. lane < 0: whole steps (trig, accent, fill, every expression
    // lane); lane >= 0: only that expression lane. Channels in 'keep' are left alone.
    void blendSteps (PatternData& cur, const PatternData& fresh, float amount, const ChannelMask& keep, int lane = -1);
    static Flavor guessFlavor (const Patch& p);

    void randomizePattern (PatternData& d, const std::array<Flavor, kNumChannels>& roles, int onlyChannel = -1);
    void randomizeAccentsFills (PatternData& d, int onlyChannel = -1);
    void alterPattern (PatternData& d, int onlyChannel = -1);
    // expression lanes: role-aware ghosts / nudges / tone / chance / rolls on the existing hits
    void randomizeExpression (PatternData& d, const std::array<Flavor, kNumChannels>& roles, int onlyChannel = -1);
    // DnB / breakbeat ghost notes in the gaps of snare-like channels; returns how many were added
    int addGhostNotes (PatternData& d, const std::array<Flavor, kNumChannels>& roles, int onlyChannel = -1);
    // small random pushes of timing, tone, decay and ghost level ("happy accidents")
    void grooveShake (PatternData& d, int onlyChannel = -1);

    // ---- beat generator (style, density, complexity, expression) ---------------------------
    void generateBeat (PatternData& d, const std::array<Flavor, kNumChannels>& roles, const DiceSettings& s,
                       const ChannelMask& keep, bool mayChangeLength = true);
    // A-L as a set: main, variation, fill, busier, breakdown, build-up, half-time, hat run, minimal,
    // glitch, variation 2, transition. Locked channels keep their own steps in every pattern.
    void generateSet (std::array<PatternData, kNumPatterns>& pats, const std::array<Flavor, kNumChannels>& roles,
                      const DiceSettings& s, const ChannelMask& keep);
    static const char* setSlotName (int pattern);
    // small musical changes (add / drop / move hits, accents, expression); anchors stay put
    void mutate (PatternData& d, const std::array<Flavor, kNumChannels>& roles, float amount, const ChannelMask& keep);
    // one expression lane of one channel, role-aware
    void randomizeExpressionLane (PatternData& d, int ch, Flavor role, int lane, float amount = 1.0f);

    // ---- kit sounds ----------------------------------------------------------------------
    Patch kitPatch (Flavor f, const Patch& current, const DiceSettings& s); // patch() + character + key
    void applyCharacter (Patch& p, KitCharacter c);
    void applyGenre (Patch& p, Flavor f, Genre g);        // genre ranges on top of a flavor recipe
public:
    static float hitLoudness (const Patch& p);            // K-weighted loudness of one full-velocity hit (LUFS)
    // sets P_LEVEL (within lo..hi) so a hit measures 'ref' LUFS; a few measured steps because drive and
    // the voice's output stage are not linear. Returns the loudness reached.
    static float matchLoudness (Patch& p, float ref, float lo, float hi);
    void fitCymbalLevel (Patch& p);                       // level so the hit sits ~10 dB under a snare
    static void tuneToKey (Patch& p, int key);

    float uni (float a, float b) { return std::uniform_real_distribution<float> (a, b) (rng); }
    int irange (int a, int b) { return std::uniform_int_distribution<int> (a, b) (rng); }
    bool chance (float p) { return uni (0.f, 1.f) < p; }

private:
    void channelPattern (PatternData& d, int ch, Flavor role);
    void channelExpression (PatternData& d, int ch, Flavor role, float amount);
    void templateChannel (PatternData& d, int ch, Flavor role, const DiceSettings& s, int duplicate);
    void euclidChannel (PatternData& d, int ch, Flavor role, const DiceSettings& s, int duplicate);
    void glitchChannel (PatternData& d, int ch, Flavor role, const DiceSettings& s);
    void densityAdjust (PatternData& d, int ch, Flavor role, const DiceSettings& s);
    void styleExpression (PatternData& d, int ch, Flavor role, const DiceSettings& s, const std::array<Flavor, kNumChannels>& roles);
    std::mt19937 rng;
};
} // namespace ferrum
