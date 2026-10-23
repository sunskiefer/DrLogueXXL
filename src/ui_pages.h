// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- the screen pages of the PERFORM page: for each of the 16 part / effect keys (and GLOBAL), the pages of
// four parameters the screen shows and the four knobs under it edit, as on the machine's screen.
#pragma once
#include "param_ids.h"

namespace dlx {

enum Sel { SEL_BD = 0, SEL_SD, SEL_LT, SEL_HT, SEL_CH, SEL_OH, SEL_RS, SEL_CP, SEL_SP1, SEL_SP2, SEL_VPM, SEL_NOISE,
           SEL_USER, SEL_DELAY, SEL_REVERB, SEL_MASTER, SEL_GLOBAL, NUM_SEL };

static const int kMaxPages = 10;

// Fills pages[k][0..3] with parameter ids (-1 = an empty knob) for the selection, reading v for what depends on other
// settings (the delay's BPM types, the master effect type, how many parameters the MULTI unit has). Returns the count.
int UiPages(int sel, const float* v, int pages[kMaxPages][4], int unit_params);

// The screen's title for the selection ("BD  BASS DRUM", "MASTER  COMPRESSOR"...); unit_name for USER.
void UiTitle(int sel, const float* v, const char* unit_name, char* buf, int len);

// The part a selection belongs to (its mixer keys), or -1 for the effects and GLOBAL.
int UiPart(int sel);

}  // namespace dlx
