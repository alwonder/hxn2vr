// xr_bindings.hpp -- the OpenXR interaction-profile bindings the backend suggests (pure data, no
// OpenXR calls, unit-tested). Module-internal C++ header.
#pragma once

#include <vector>

namespace hxvr {

// One per OpenXR action of the "gameplay" set (backend_openxr.cpp). Each action has the subaction
// paths /user/hand/left and /user/hand/right; the hand of a binding is the prefix of its path.
enum class Act { GripPose, AimPose, Trigger, Squeeze, Stick, StickClick, Lower, Upper, Menu, Haptic };
constexpr int kActCount = 10;
static_assert(static_cast<int>(Act::Haptic) + 1 == kActCount, "kActCount must follow the Act enum");

// path: full input or output path, "/user/hand/<left|right>/<input|output>/<component>" (a string literal).
struct Binding { Act act; const char* path; };
struct ProfileBindings { const char* profile; std::vector<Binding> bindings; };

// The five suggested profiles: oculus/touch_controller, valve/index_controller, htc/vive_controller,
// microsoft/motion_controller, khr/simple_controller. Poses (grip, aim) and Trigger are bound on both
// hands of every profile, as is Haptic output; Menu on at least one hand (Touch and Index: left only).
const std::vector<ProfileBindings>& suggestedBindings();

}  // namespace hxvr
