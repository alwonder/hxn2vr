// vr_netblock.cpp -- the VR move block the client sends to its local server (clc_hxvr), and the server's
// sanity check of an accumulated room-scale move (pure functions). Little-endian on the wire whatever the
// host; no engine headers.
#include "vr_api.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr int kV1Size = 10;                  // u8 version, u8 flags, f32 x, f32 y
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
	buf[0] = (unsigned char)b->version;
	buf[1] = (unsigned char)(b->flags & 0xffu);
	put_f32(buf + 2, b->roomscale[0]);
	put_f32(buf + 6, b->roomscale[1]);
	return kV1Size;
}

int VR_ReadMoveBlock(const unsigned char* buf, int len, vr_moveblock_t* out) {
	if (!buf || !out || len < kV1Size) return 0;
	if (buf[0] == 0) return 0;
	const float x = get_f32(buf + 2), y = get_f32(buf + 6);
	if (!std::isfinite(x) || !std::isfinite(y)) return 0;
	out->version = buf[0];
	out->flags = buf[1];
	out->roomscale[0] = x;
	out->roomscale[1] = y;
	return 1;
}

int VR_RoomscaleMoveOk(const float move[2]) {
	if (!move || !std::isfinite(move[0]) || !std::isfinite(move[1])) return 0;
	const double x = move[0], y = move[1];
	return std::sqrt(x * x + y * y) <= kMaxRoomscaleMove ? 1 : 0;
}
