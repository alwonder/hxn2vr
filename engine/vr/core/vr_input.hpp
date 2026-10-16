// vr_input.hpp -- controller state internals shared by vr_module.cpp and vr_input.cpp.
// Module-internal C++ header.
#pragma once

#include "backend.hpp"

namespace hxvr {

// The running backend, or nullptr (defined in vr_module.cpp).
Backend* activeBackend();

// Forgets all controller state (VR starts or stops): every hand reads as inactive, no buttons held.
void inputReset();

// Called by VR_BeginFrame right after the backend's beginFrame: snapshots both hands and derives the
// TRIGGER / GRIP button bits with hysteresis from the previous frame's bits.
void inputUpdate(const Backend& backend);

}  // namespace hxvr
