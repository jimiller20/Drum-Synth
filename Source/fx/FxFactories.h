// FxFactories.h - internal: one factory per module family
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "FxModule.h"

namespace ferrum::fx
{
std::unique_ptr<Module> makeFerric();
std::unique_ptr<Module> makeAmp();
std::unique_ptr<Module> makeCabinet();
std::unique_ptr<Module> makeTape();
std::unique_ptr<Module> makeCrusher();
std::unique_ptr<Module> makeGlue();
std::unique_ptr<Module> makeShaper();
std::unique_ptr<Module> makeLadder();
std::unique_ptr<Module> makeEq();
std::unique_ptr<Module> makeUtility();
std::unique_ptr<Module> makeEcho();
std::unique_ptr<Module> makeReverb();
std::unique_ptr<Module> makeSpring();
std::unique_ptr<Module> makeChorus();
std::unique_ptr<Module> makeLxr (Type t);
} // namespace ferrum::fx
