// vr_input.cpp -- controller state of the hxvr C API: per-frame snapshot of both hands, button
// hysteresis, the mock-only setters and the menu stick repeat. Must not include engine headers.
#include "vr_api.h"

#include "vr_input.hpp"

#include <glm/glm.hpp>

#include <cmath>

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

void VR_Haptic(int hand, float amplitude, float seconds) {
	hxvr::Backend* b = hxvr::activeBackend();
	if (!b || !valid_hand(hand) || !std::isfinite(amplitude) || !std::isfinite(seconds) ||
	    !(amplitude > 0.0f) || !(seconds > 0.0f)) return;
	b->haptic(hand, std::fmin(amplitude, 1.0f), std::fmin(seconds, 0.5f));
}

void VR_MockLastHaptic(int hand, float* amplitude, float* seconds) {
	float a = 0.0f, s = 0.0f;
	hxvr::Backend* b = hxvr::activeBackend();
	if (b && valid_hand(hand)) b->mockLastHaptic(hand, a, s);
	if (amplitude) *amplitude = a;
	if (seconds) *seconds = s;
}

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

// Menu arrows from a stick (see vr_api.h). The held direction is kept while its own axis stays past
// kStickHold with the same sign, even if the other axis becomes larger (no flicker on a diagonal);
// once it is let go, a new direction can start in the same call. A call's dt counts as at most
// kMaxDt, so a long frame (loading, a sound cached on first use...) cannot turn one flick into a
// repeat, and at most one direction is emitted per call.
int VR_StickRepeat(vr_repeat_t* r, const float stick[2], float dt) {
	constexpr float kStickStart = 0.6f;
	constexpr float kStickHold = 0.3f;
	constexpr float kRepeatDelay = 0.45f;
	constexpr float kRepeatEvery = 0.12f;
	constexpr float kMaxDt = 0.1f;   // < kRepeatEvery: one call never owes more than one repeat
	if (!r) return VR_DIR_NONE;
	float x = stick ? stick[0] : 0.0f, y = stick ? stick[1] : 0.0f;
	if (!std::isfinite(x)) x = 0.0f;
	if (!std::isfinite(y)) y = 0.0f;
	if (!std::isfinite(dt) || !(dt > 0.0f)) dt = 0.0f;
	if (dt > kMaxDt) dt = kMaxDt;

	// the held direction's own axis, signed so that "towards the direction" is positive
	float along = 0.0f;
	switch (r->dir) {
		case VR_DIR_UP: along = y; break;
		case VR_DIR_DOWN: along = -y; break;
		case VR_DIR_RIGHT: along = x; break;
		case VR_DIR_LEFT: along = -x; break;
		default: r->dir = VR_DIR_NONE; break;
	}
	if (r->dir != VR_DIR_NONE) {
		if (along >= kStickHold) {
			r->held += dt;
			if (r->held < r->next) return VR_DIR_NONE;
			r->next += kRepeatEvery;   // > held: dt <= kMaxDt < kRepeatEvery
			return r->dir;
		}
		r->dir = VR_DIR_NONE;   // let go
	}

	const bool vertical = std::fabs(y) >= std::fabs(x);
	const float dominant = vertical ? y : x;
	if (std::fabs(dominant) < kStickStart) return VR_DIR_NONE;
	r->dir = vertical ? (dominant > 0.0f ? VR_DIR_UP : VR_DIR_DOWN) : (dominant > 0.0f ? VR_DIR_RIGHT : VR_DIR_LEFT);
	r->held = 0.0f;
	r->next = kRepeatDelay;
	return r->dir;
}

// Radial deadzone with rescaling (see vr_api.h).
void VR_StickDeadzone(const float in[2], float deadzone, float out[2]) {
	if (!out) return;
	const float x = in ? in[0] : 0.0f, y = in ? in[1] : 0.0f;
	if (!std::isfinite(x) || !std::isfinite(y)) {
		out[0] = out[1] = 0.0f;
		return;
	}
	const float dz = std::isfinite(deadzone) ? std::fmin(std::fmax(deadzone, 0.0f), 0.99f) : 0.0f;
	const float len = std::sqrt(x * x + y * y);
	if (!(len > dz)) {
		out[0] = out[1] = 0.0f;
		return;
	}
	const float scaled = std::fmin((len - dz) / (1.0f - dz), 1.0f);
	out[0] = x / len * scaled;
	out[1] = y / len * scaled;
}

float VR_MoveYaw(int mode, const vr_quat_t* hand_aim, int hand_valid, float head_yaw, float body_yaw) {
	constexpr float kMinProjection = 0.3f;
	constexpr float kRadToDeg = 57.29577951308232f;
	if (mode != 1 || !hand_valid || !hand_aim) return body_yaw + head_yaw;
	const glm::quat q(hand_aim->w, hand_aim->x, hand_aim->y, hand_aim->z);
	const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
	if (!std::isfinite(n) || n < 1e-6f) return body_yaw + head_yaw;
	const glm::vec3 f = (q / n) * glm::vec3(0.0f, 0.0f, -1.0f);   // aim forward, tracking axes
	// Quake axes: x = -z, y = -x. Projection on the floor = (x, z) of the tracking vector.
	if (!(std::sqrt(f.x * f.x + f.z * f.z) >= kMinProjection)) return body_yaw + head_yaw;
	return body_yaw + std::atan2(-f.x, -f.z) * kRadToDeg;
}

void VR_StickToMove(const float stick[2], float move_yaw, float view_yaw, float speed, float out[2]) {
	if (!out) return;
	const float x = stick ? stick[0] : 0.0f, y = stick ? stick[1] : 0.0f;
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(move_yaw) || !std::isfinite(view_yaw) ||
	    !std::isfinite(speed)) {
		out[0] = out[1] = 0.0f;
		return;
	}
	// the stick's frame is rotated by d (counterclockwise) against the view's
	const double d = static_cast<double>(move_yaw - view_yaw) * 3.14159265358979323846 / 180.0;
	const double s = std::sin(d), c = std::cos(d);
	out[0] = static_cast<float>(speed * (y * c + x * s));
	out[1] = static_cast<float>(speed * (x * c - y * s));
}

float VR_TurnStep(vr_turn_t* t, float stick_x, float dt, int smooth, float snap_deg, float speed_dps,
                  float deadzone) {
	constexpr float kFire = 0.7f;
	constexpr float kRearm = 0.3f;
	if (!t || !std::isfinite(stick_x)) return 0.0f;
	if (smooth) {
		if (!std::isfinite(dt) || !(dt > 0.0f) || !std::isfinite(speed_dps)) return 0.0f;
		const float in[2] = {stick_x, 0.0f};
		float out[2];
		VR_StickDeadzone(in, deadzone, out);
		return -out[0] * speed_dps * dt;
	}
	const float ax = std::fabs(stick_x);
	if (t->fired) {
		if (ax < kRearm) t->fired = 0;
		return 0.0f;
	}
	if (ax < kFire || !std::isfinite(snap_deg)) return 0.0f;
	t->fired = 1;
	return stick_x > 0.0f ? -snap_deg : snap_deg;
}

}  // extern "C"
