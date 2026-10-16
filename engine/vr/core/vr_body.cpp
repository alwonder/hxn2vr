// vr_body.cpp -- the player's body model and calibration (pure functions). All in metres of tracking
// space: nothing here takes vr_world_scale (the body does not scale with the world). No engine headers.
#include "vr_api.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace {

// Eye height is 0.936 of the stature; the other joints are fractions of the stature.
constexpr float kEyeFraction = 0.936f;
constexpr float kShoulderHeight = 0.818f;
constexpr float kShoulderWidth = 0.259f;
constexpr float kArmLength = 0.40f;
constexpr float kChestHeight = 0.72f;
constexpr float kWaistHeight = 0.60f;
constexpr float kDefaultCrouch = 0.65f;

constexpr float kMinEye = 0.5f, kMaxEye = 2.5f;
constexpr float kMinArm = 0.4f, kMaxArm = 1.0f;

bool finite3(const vr_vec3_t& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool plausible_eye(float y) { return std::isfinite(y) && y >= kMinEye && y <= kMaxEye; }
// An override is used when it is a positive finite number; anything else is derived.
bool use_override(float v) { return std::isfinite(v) && v > 0.0f; }

}  // namespace

extern "C" {

int VR_BodyFromParams(const vr_body_params_t* p, vr_body_t* out) {
	if (!p || !out || !plausible_eye(p->eye_height)) return 0;
	const float stature = p->eye_height / kEyeFraction;
	vr_body_t b;
	b.eye_height = p->eye_height;
	b.stature = stature;
	b.shoulder_height = kShoulderHeight * stature;
	b.shoulder_width = use_override(p->shoulder_width) ? p->shoulder_width : kShoulderWidth * stature;
	b.arm_length = use_override(p->arm_length) ? p->arm_length : kArmLength * stature;
	b.chest_height = kChestHeight * stature;
	b.waist_height = kWaistHeight * stature;
	b.crouch_height = (use_override(p->crouch_fraction) ? p->crouch_fraction : kDefaultCrouch) * p->eye_height;
	*out = b;
	return 1;
}

int VR_CalibrateEyeHeight(float head_y, float* out) {
	if (!out || !plausible_eye(head_y)) return 0;
	*out = head_y;
	return 1;
}

float VR_ArmDistance(const vr_body_t* b, const vr_pose_t* head, const vr_vec3_t* hand_pos, int hand) {
	if (!b || !head || !hand_pos || !finite3(head->pos) || !finite3(*hand_pos)) return -1.0f;
	if (!std::isfinite(b->shoulder_height) || !std::isfinite(b->shoulder_width)) return -1.0f;
	const glm::quat q(head->rot.w, head->rot.x, head->rot.y, head->rot.z);
	const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
	if (!std::isfinite(n) || n < 1e-6f) return -1.0f;
	const glm::quat qn = q / n;
	// the head's horizontal right: its right axis on the floor plane; if the head rolls onto its side
	// (right axis vertical), derive it from the forward axis instead
	const glm::vec3 r = qn * glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec2 right(r.x, r.z);
	if (glm::length(right) < 0.3f) {
		const glm::vec3 f = qn * glm::vec3(0.0f, 0.0f, -1.0f);
		right = glm::vec2(-f.z, f.x);
	}
	const float len = glm::length(right);
	if (!(len > 1e-6f)) return -1.0f;
	right /= len;
	const float side = (hand == VR_HAND_RIGHT ? 0.5f : -0.5f) * b->shoulder_width;
	const glm::vec3 shoulder(head->pos.x + right.x * side, b->shoulder_height, head->pos.z + right.y * side);
	const float arm = glm::length(glm::vec3(hand_pos->x, hand_pos->y, hand_pos->z) - shoulder);
	return std::isfinite(arm) ? arm : -1.0f;
}

int VR_CalibrateArm(const vr_body_t* b, const vr_pose_t* head, const vr_vec3_t* hand_pos, int hand, float* out) {
	if (!out) return 0;
	const float arm = VR_ArmDistance(b, head, hand_pos, hand);
	if (!(arm >= kMinArm && arm <= kMaxArm)) return 0;
	*out = arm;
	return 1;
}

}  // extern "C"
