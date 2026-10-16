// vr_body.cpp -- the player's body model, calibration, the physical crouch and the room-scale lean (pure
// functions). The body is in metres of tracking space: nothing here takes vr_world_scale (the body does not
// scale with the world; the lean is converted to units with the caller's upm). No engine headers.
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
constexpr float kCrouchExit = 0.05f;   // of the eye height above the crouch height: stand up again

constexpr float kMinEye = 0.5f, kMaxEye = 2.5f;
constexpr float kMinArm = 0.4f, kMaxArm = 1.0f;

constexpr float kMaxRoomStep = 0.5f;    // metres per call: a longer head step is a tracking jump
constexpr float kMaxBodyStep = 64.0f;   // units per frame on the floor: a longer move is a teleport

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

// ---- room-scale on the client ----

void VR_RoomReset(vr_room_t* r) {
	if (!r) return;
	r->lean[0] = r->lean[1] = 0.0f;
	r->last_head = vr_vec3_t{0.0f, 0.0f, 0.0f};
	r->has_last = 0;
}

void VR_RoomStep(vr_room_t* r, const vr_vec3_t* head, float body_yaw, float upm, float radius_units,
                 float out_excess[2]) {
	if (out_excess) out_excess[0] = out_excess[1] = 0.0f;
	if (!r || !head || !finite3(*head)) return;   // no step, the last head stays
	if (!r->has_last) {
		r->last_head = *head;
		r->has_last = 1;
		return;
	}
	const float dx = head->x - r->last_head.x, dz = head->z - r->last_head.z;
	if (!std::isfinite(body_yaw) || !std::isfinite(upm) || !(upm > 0.0f)) {
		r->last_head = *head;
		return;
	}
	r->last_head = *head;
	if (std::sqrt(dx * dx + dz * dz) > kMaxRoomStep) return;   // tracking jump: not a step

	// tracking (x right, -z forward) -> Quake (x forward, y left), rotated by the body yaw, in units
	const float qx = -dz * upm, qy = -dx * upm;
	const float a = body_yaw * (3.14159265358979323846f / 180.0f);
	const float c = std::cos(a), s = std::sin(a);
	float lx = r->lean[0] + qx * c - qy * s;
	float ly = r->lean[1] + qx * s + qy * c;

	const float radius = (std::isfinite(radius_units) && radius_units > 0.0f) ? radius_units : 0.0f;
	const float len = std::sqrt(lx * lx + ly * ly);
	if (len > radius) {
		const float k = radius / len;   // len > 0 here
		if (out_excess) {
			out_excess[0] = lx * (1.0f - k);
			out_excess[1] = ly * (1.0f - k);
		}
		lx *= k;
		ly *= k;
	}
	r->lean[0] = lx;
	r->lean[1] = ly;
}

int VR_BodyJumped(const float prev_feet[3], const float feet[3]) {
	if (!prev_feet || !feet) return 1;
	const float dx = feet[0] - prev_feet[0], dy = feet[1] - prev_feet[1];
	const float d2 = dx * dx + dy * dy;
	if (!std::isfinite(d2)) return 1;
	return d2 > kMaxBodyStep * kMaxBodyStep ? 1 : 0;
}

int VR_CrouchUpdate(int crouched, float head_y, const vr_body_t* b) {
	if (!b || !(b->eye_height > 0.0f) || !std::isfinite(b->eye_height) || !std::isfinite(b->crouch_height)) return 0;
	if (crouched) return head_y > b->crouch_height + kCrouchExit * b->eye_height ? 0 : 1;   // NaN: stays
	return head_y < b->crouch_height ? 1 : 0;
}

int VR_CalibrateArm(const vr_body_t* b, const vr_pose_t* head, const vr_vec3_t* hand_pos, int hand, float* out) {
	if (!out) return 0;
	const float arm = VR_ArmDistance(b, head, hand_pos, hand);
	if (!(arm >= kMinArm && arm <= kMaxArm)) return 0;
	*out = arm;
	return 1;
}

}  // extern "C"
