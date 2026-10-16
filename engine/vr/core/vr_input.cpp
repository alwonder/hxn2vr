// vr_input.cpp -- controller state of the hxvr C API: per-frame snapshot of both hands, button
// hysteresis, and the mock-only setters. Must not include engine headers.
#include "vr_api.h"

#include "vr_input.hpp"

#include <glm/glm.hpp>

namespace {

// Analog trigger / squeeze become buttons with hysteresis: press at >= 0.55, release at <= 0.35.
constexpr float kPress = 0.55f;
constexpr float kRelease = 0.35f;

vr_hand_t g_hands[2] = {};

vr_pose_t to_c(const hxvr::Pose& p) {
	return vr_pose_t{{p.pos.x, p.pos.y, p.pos.z}, {p.rot.x, p.rot.y, p.rot.z, p.rot.w}};
}

// Pressed state of one analog axis after this frame's value, given whether it was pressed last frame.
bool hysteresis(bool wasDown, float value) {
	if (wasDown) return value > kRelease;
	return value >= kPress;
}

bool valid_hand(int hand) { return hand == VR_HAND_LEFT || hand == VR_HAND_RIGHT; }

}  // namespace

namespace hxvr {

void inputReset() {
	g_hands[0] = vr_hand_t{};
	g_hands[1] = vr_hand_t{};
}

void inputUpdate(const Backend& backend) {
	for (int i = 0; i < 2; ++i) {
		const HandState s = backend.hand(i);
		vr_hand_t& out = g_hands[i];
		if (!s.active) {   // nothing is reported, and no button stays held across the gap
			out = vr_hand_t{};
			continue;
		}
		const bool wasTrigger = (out.buttons & VR_BTN_TRIGGER) != 0u;
		const bool wasGrip = (out.buttons & VR_BTN_GRIP) != 0u;
		unsigned buttons = 0;
		if (hysteresis(wasTrigger, s.trigger)) buttons |= VR_BTN_TRIGGER;
		if (hysteresis(wasGrip, s.squeeze)) buttons |= VR_BTN_GRIP;
		if (s.lower) buttons |= VR_BTN_LOWER;
		if (s.upper) buttons |= VR_BTN_UPPER;
		if (s.stickClick) buttons |= VR_BTN_STICK;
		if (s.menu) buttons |= VR_BTN_MENU;
		out.active = 1;
		out.pose_valid = s.poseValid ? 1 : 0;
		out.grip = to_c(s.grip);
		out.aim = to_c(s.aim);
		out.trigger = s.trigger;
		out.squeeze = s.squeeze;
		out.stick[0] = s.stick.x;
		out.stick[1] = s.stick.y;
		out.buttons = buttons;
	}
}

}  // namespace hxvr

extern "C" {

void VR_GetHand(int hand, vr_hand_t* out) {
	if (!out) return;
	*out = (hxvr::activeBackend() && valid_hand(hand)) ? g_hands[hand] : vr_hand_t{};
}

void VR_MockSetHand(int hand, const vr_hand_t* in) {
	hxvr::Backend* b = hxvr::activeBackend();
	if (!b || !in || !valid_hand(hand)) return;
	hxvr::HandState s;
	s.active = in->active != 0;
	s.poseValid = in->pose_valid != 0;
	s.grip = hxvr::Pose{glm::vec3(in->grip.pos.x, in->grip.pos.y, in->grip.pos.z),
	                    glm::quat(in->grip.rot.w, in->grip.rot.x, in->grip.rot.y, in->grip.rot.z)};
	s.aim = hxvr::Pose{glm::vec3(in->aim.pos.x, in->aim.pos.y, in->aim.pos.z),
	                   glm::quat(in->aim.rot.w, in->aim.rot.x, in->aim.rot.y, in->aim.rot.z)};
	s.trigger = in->trigger;
	s.squeeze = in->squeeze;
	s.stick = glm::vec2(in->stick[0], in->stick[1]);
	s.lower = (in->buttons & VR_BTN_LOWER) != 0u;
	s.upper = (in->buttons & VR_BTN_UPPER) != 0u;
	s.stickClick = (in->buttons & VR_BTN_STICK) != 0u;
	s.menu = (in->buttons & VR_BTN_MENU) != 0u;
	b->mockSetHand(hand, s);
}

void VR_MockSetHeadOffset(const vr_vec3_t* target, float seconds) {
	hxvr::Backend* b = hxvr::activeBackend();
	if (!b || !target) return;
	b->mockSetHeadOffset(glm::vec3(target->x, target->y, target->z), seconds);
}

}  // extern "C"
