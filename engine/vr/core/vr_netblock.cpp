// vr_netblock.cpp -- the VR move block the client sends to its local server (clc_hxvr), and the server's
// sanity check of an accumulated room-scale move (pure functions). Little-endian on the wire whatever the
// host; no engine headers.
#include "vr_api.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr int kV1Size = 10;                  // u8 version, u8 flags, f32 x, f32 y
constexpr int kV2Size = 34;
constexpr float kMaxRoomscaleMove = 64.0f;   // units: more is a teleport or garbage, not a step

void put_f32(unsigned char* p, float f) {
	std::uint32_t u;
	std::memcpy(&u, &f, sizeof(u));
	p[0] = (unsigned char)(u & 0xffu);
	p[1] = (unsigned char)((u >> 8) & 0xffu);
	p[2] = (unsigned char)((u >> 16) & 0xffu);
	p[3] = (unsigned char)((u >> 24) & 0xffu);
}

float get_f32(const unsigned char* p) {
	const std::uint32_t u = (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) |
	                        ((std::uint32_t)p[3] << 24);
	float f;
	std::memcpy(&f, &u, sizeof(f));
	return f;
}

}  // namespace

int VR_WriteMoveBlock(const vr_moveblock_t* b, unsigned char* buf, int cap) {
	if (!b || !buf || cap < kV1Size) return 0;
	if (b->version < 1 || b->version > 255) return 0;
	const int size = b->version >= 2 ? kV2Size : kV1Size;
	if (cap < size) return 0;
	buf[0] = (unsigned char)b->version;
	buf[1] = (unsigned char)(b->flags & 0xffu);
	put_f32(buf + 2, b->roomscale[0]);
	put_f32(buf + 6, b->roomscale[1]);
	if (b->version >= 2) {
		for (int i = 0; i < 3; ++i) {
			put_f32(buf + 10 + 4 * i, b->muzzle[i]);
			put_f32(buf + 22 + 4 * i, b->aim[i]);
		}
	}
	return size;
}

int VR_ReadMoveBlock(const unsigned char* buf, int len, vr_moveblock_t* out) {
	if (!buf || !out || len < kV1Size) return 0;
	if (buf[0] == 0) return 0;
	const float x = get_f32(buf + 2), y = get_f32(buf + 6);
	if (!std::isfinite(x) || !std::isfinite(y)) return 0;
	vr_moveblock_t b{};
	b.version = buf[0];
	b.flags = buf[1];
	b.roomscale[0] = x;
	b.roomscale[1] = y;
	if (b.version >= 2 && len >= kV2Size) {
		for (int i = 0; i < 3; ++i) {
			b.muzzle[i] = get_f32(buf + 10 + 4 * i);
			b.aim[i] = get_f32(buf + 22 + 4 * i);
			if (!std::isfinite(b.muzzle[i]) || !std::isfinite(b.aim[i])) return 0;
		}
	} else {
		b.flags &= ~VR_MOVE_WEAPON;
	}
	*out = b;
	return 1;
}

int VR_MuzzleOffsetOk(const float muzzle[3]) {
	if (!muzzle || !std::isfinite(muzzle[0]) || !std::isfinite(muzzle[1]) ||
	    !std::isfinite(muzzle[2])) return 0;
	const double x = muzzle[0], y = muzzle[1], z = muzzle[2];
	return x * x + y * y + z * z <= 96.0 * 96.0 ? 1 : 0;
}

int VR_RoomscaleMoveOk(const float move[2]) {
	if (!move || !std::isfinite(move[0]) || !std::isfinite(move[1])) return 0;
	const double x = move[0], y = move[1];
	return std::sqrt(x * x + y * y) <= kMaxRoomscaleMove ? 1 : 0;
}
