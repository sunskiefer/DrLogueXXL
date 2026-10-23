// SPDX-License-Identifier: GPL-3.0-or-later
// DrLogueXXL -- mpc-vst-plugins engine contract (wrapper/engine.h) on top of dlx::Engine.
#include "dlx_engine.h"

extern "C" {
#include "engine.h"   // mpc-vst-plugins wrapper/engine.h
}

namespace {

void* Create(const char* data_dir) { return new dlx::Engine(data_dir); }
void Destroy(void* inst) { delete static_cast<dlx::Engine*>(inst); }
void Midi(void* inst, const uint8_t* msg, int len) { static_cast<dlx::Engine*>(inst)->Midi(msg, len); }
void SetParam(void* inst, const char* key, const char* val) { static_cast<dlx::Engine*>(inst)->SetParam(key, val); }
int GetParam(void* inst, const char* key, char* buf, int len) {
  return static_cast<dlx::Engine*>(inst)->GetParam(key, buf, len);
}
void Render(void* inst, int16_t* out, int frames) { static_cast<dlx::Engine*>(inst)->Render(out, frames); }

const mpc_engine_t kEngine = {Create, Destroy, Midi, SetParam, GetParam, Render, nullptr, nullptr};

}  // namespace

extern "C" const mpc_engine_t* mpc_engine(void) { return &kEngine; }

// the unit slots take a new engine's defaults when MULTI's engine changes: tell the host so the screen follows
extern "C" int mpc_engine_changed(void* inst, char* key, int len) {
  return static_cast<dlx::Engine*>(inst)->NextChanged(key, len);
}
