// vr_module_testing.hpp -- test-only entry points of vr_module.cpp (Ruling R7: always compiled,
// declared only here, never in vr_api.h). Module-internal.
#pragma once

#include "backend.hpp"

#include <memory>

// Stops any running backend and makes `backend` the running one, as if VR_Start had succeeded
// with it (no host, no GL, no eye targets). The backend must already be started by the caller.
// VR_Stop() later stops and destroys it.
void VR_TestInjectBackend(std::unique_ptr<hxvr::Backend> backend);
