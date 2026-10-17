// The VR move block (clc_hxvr): versioned, little-endian wire format, and the server's sanity check of an
// accumulated room-scale move.
#include "doctest.h"
#include "vr_api.h"

#include <cmath>
#include <cstring>
#include <limits>

TEST_CASE("move block round trip") {
	vr_moveblock_t b{};
	b.version = 1;
	b.flags = VR_MOVE_ROOMSCALE;
	b.roomscale[0] = 1.5f;
	b.roomscale[1] = -2.25f;

	unsigned char buf[VR_MOVEBLOCK_MAX];
	std::memset(buf, 0xAA, sizeof(buf));
	const int n = VR_WriteMoveBlock(&b, buf, (int)sizeof(buf));
	REQUIRE(n == 10);
	// v1 layout: u8 version, u8 flags, f32 LE x, f32 LE y
	CHECK(buf[0] == 1);
	CHECK(buf[1] == VR_MOVE_ROOMSCALE);
	const unsigned char x_le[4] = {0x00, 0x00, 0xC0, 0x3F};   // 1.5f = 0x3FC00000
	const unsigned char y_le[4] = {0x00, 0x00, 0x10, 0xC0};   // -2.25f = 0xC0100000
	CHECK(std::memcmp(buf + 2, x_le, 4) == 0);
	CHECK(std::memcmp(buf + 6, y_le, 4) == 0);
	CHECK(buf[10] == 0xAA);   // nothing written past the block

	vr_moveblock_t r{};
	REQUIRE(VR_ReadMoveBlock(buf, n, &r) == 1);
	CHECK(r.version == 1);
	CHECK(r.flags == VR_MOVE_ROOMSCALE);
	CHECK(r.roomscale[0] == 1.5f);
	CHECK(r.roomscale[1] == -2.25f);

	// exactly 10 bytes of room is enough
	CHECK(VR_WriteMoveBlock(&b, buf, 10) == 10);
}

TEST_CASE("move block rejects malformed input") {
	vr_moveblock_t b{};
	b.version = 1;
	b.flags = VR_MOVE_ROOMSCALE;
	b.roomscale[0] = 3.0f;
	b.roomscale[1] = 4.0f;
	unsigned char buf[VR_MOVEBLOCK_MAX];
	REQUIRE(VR_WriteMoveBlock(&b, buf, (int)sizeof(buf)) == 10);

	vr_moveblock_t r{};
	r.version = 77;
	CHECK(VR_ReadMoveBlock(buf, 9, &r) == 0);    // too short
	CHECK(r.version == 77);                       // out untouched on failure
	CHECK(VR_ReadMoveBlock(buf, 0, &r) == 0);
	CHECK(VR_ReadMoveBlock(buf, -1, &r) == 0);
	CHECK(VR_ReadMoveBlock(nullptr, 10, &r) == 0);
	CHECK(VR_ReadMoveBlock(buf, 10, nullptr) == 0);

	unsigned char bad[VR_MOVEBLOCK_MAX];
	std::memcpy(bad, buf, 10);
	bad[0] = 0;   // version 0
	CHECK(VR_ReadMoveBlock(bad, 10, &r) == 0);

	vr_moveblock_t nb = b;
	nb.roomscale[0] = std::numeric_limits<float>::quiet_NaN();
	REQUIRE(VR_WriteMoveBlock(&nb, bad, (int)sizeof(bad)) == 10);
	CHECK(VR_ReadMoveBlock(bad, 10, &r) == 0);   // NaN x
	nb.roomscale[0] = 0.0f;
	nb.roomscale[1] = -std::numeric_limits<float>::infinity();
	REQUIRE(VR_WriteMoveBlock(&nb, bad, (int)sizeof(bad)) == 10);
	CHECK(VR_ReadMoveBlock(bad, 10, &r) == 0);   // infinite y
	CHECK(r.version == 77);

	CHECK(VR_WriteMoveBlock(&b, buf, 9) == 0);   // no room
	CHECK(VR_WriteMoveBlock(&b, buf, 0) == 0);
	CHECK(VR_WriteMoveBlock(&b, nullptr, 64) == 0);
	CHECK(VR_WriteMoveBlock(nullptr, buf, 64) == 0);
	vr_moveblock_t v0 = b;
	v0.version = 0;   // would be unreadable
	CHECK(VR_WriteMoveBlock(&v0, buf, 64) == 0);
	v0.version = 256;   // does not fit the version byte
	CHECK(VR_WriteMoveBlock(&v0, buf, 64) == 0);
}

TEST_CASE("move block reads newer versions by prefix") {
	vr_moveblock_t b{};
	b.version = 1;
	b.flags = VR_MOVE_ROOMSCALE | 0x80u;   // a flag this version does not know
	b.roomscale[0] = -7.0f;
	b.roomscale[1] = 0.125f;
	unsigned char buf[VR_MOVEBLOCK_MAX];
	REQUIRE(VR_WriteMoveBlock(&b, buf, (int)sizeof(buf)) == 10);
	buf[0] = 2;      // a newer version with only the legacy prefix available
	buf[10] = 0x12;   // partial trailing data
	buf[11] = 0x34;

	vr_moveblock_t r{};
	REQUIRE(VR_ReadMoveBlock(buf, 12, &r) == 1);
	CHECK(r.version == 2);
	CHECK(r.flags == (VR_MOVE_ROOMSCALE | 0x80u));
	CHECK(r.roomscale[0] == -7.0f);
	CHECK(r.roomscale[1] == 0.125f);
}

TEST_CASE("move block v2 round trip") {
	vr_moveblock_t b{2, VR_MOVE_ROOMSCALE | VR_MOVE_WEAPON,
	                 {1.5f, -2.25f}, {10.0f, -4.0f, 36.0f}, {-5.0f, 90.0f, 0.0f}};
	CHECK(VR_MOVEBLOCK_VERSION == 2);
	unsigned char buf[VR_MOVEBLOCK_MAX];
	std::memset(buf, 0xAA, sizeof(buf));
	const int n = VR_WriteMoveBlock(&b, buf, (int)sizeof(buf));
	REQUIRE(n == 34);
	CHECK(buf[34] == 0xAA);
	// u8 version, u8 flags, then roomscale, muzzle and aim as f32 LE.
	const unsigned char expected[34] = {
		2, 3, 0x00, 0x00, 0xC0, 0x3F, 0x00, 0x00, 0x10, 0xC0,
		0x00, 0x00, 0x20, 0x41, 0x00, 0x00, 0x80, 0xC0,
		0x00, 0x00, 0x10, 0x42, 0x00, 0x00, 0xA0, 0xC0,
		0x00, 0x00, 0xB4, 0x42, 0x00, 0x00, 0x00, 0x00
	};
	CHECK(std::memcmp(buf, expected, sizeof(expected)) == 0);
	vr_moveblock_t r{};
	REQUIRE(VR_ReadMoveBlock(buf, n, &r) == 1);
	CHECK(r.version == 2);
	CHECK(r.flags == b.flags);
	for (int i = 0; i < 2; ++i) CHECK(r.roomscale[i] == b.roomscale[i]);
	for (int i = 0; i < 3; ++i) {
		CHECK(r.muzzle[i] == b.muzzle[i]);
		CHECK(r.aim[i] == b.aim[i]);
	}
	CHECK(VR_WriteMoveBlock(&b, buf, 34) == 34);
	CHECK(VR_WriteMoveBlock(&b, buf, 33) == 0);
}

TEST_CASE("move block v1 still readable") {
	// A legacy packet with the weapon bit set must never enable weapon data.
	const unsigned char buf[10] = {
		1, 3, 0x00, 0x00, 0xC0, 0x3F, 0x00, 0x00, 0x10, 0xC0
	};
	vr_moveblock_t r{2, VR_MOVE_WEAPON, {0.0f, 0.0f},
	                 {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
	REQUIRE(VR_ReadMoveBlock(buf, (int)sizeof(buf), &r) == 1);
	CHECK(r.version == 1);
	CHECK(r.flags == VR_MOVE_ROOMSCALE);
	CHECK(r.roomscale[0] == 1.5f);
	CHECK(r.roomscale[1] == -2.25f);
	for (int i = 0; i < 3; ++i) {
		CHECK(r.muzzle[i] == 0.0f);
		CHECK(r.aim[i] == 0.0f);
	}
}

TEST_CASE("move block v2 rejects non-finite weapon values") {
	// Build packets directly so reader validation does not depend on writer validation.
	unsigned char buf[34] = {2, VR_MOVE_WEAPON};
	const unsigned char nan_le[4] = {0x00, 0x00, 0xC0, 0x7F};
	const unsigned char inf_le[4] = {0x00, 0x00, 0x80, 0x7F};
	vr_moveblock_t r{};
	r.version = 77;
	std::memcpy(buf + 10, nan_le, 4);   // muzzle x
	CHECK(VR_ReadMoveBlock(buf, (int)sizeof(buf), &r) == 0);
	CHECK(r.version == 77);
	std::memset(buf + 10, 0, 4);
	std::memcpy(buf + 22, inf_le, 4);   // aim pitch
	CHECK(VR_ReadMoveBlock(buf, (int)sizeof(buf), &r) == 0);
	CHECK(r.version == 77);
}

TEST_CASE("muzzle offset sanity") {
	const float ok[3] = {10.0f, -4.0f, 36.0f};
	const float big[3] = {130.0f, 100.0f, 30.0f};
	const float nan[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
	const float edge[3] = {0.0f, 0.0f, 160.0f};   // R5: inclusive 160-unit bound
	CHECK(VR_MuzzleOffsetOk(ok) == 1);
	CHECK(VR_MuzzleOffsetOk(big) == 0);
	CHECK(VR_MuzzleOffsetOk(nan) == 0);
	CHECK(VR_MuzzleOffsetOk(edge) == 1);
	const float outside[3] = {0, 0, 160.01f};
	CHECK(VR_MuzzleOffsetOk(outside) == 0);
	CHECK(VR_MuzzleOffsetOk(nullptr) == 0);
}

TEST_CASE("server rejects oversized room-scale moves") {
	const float zero[2] = {0.0f, 0.0f};
	const float ok[2] = {40.0f, 40.0f};      // 56.57
	const float edge[2] = {0.0f, -64.0f};    // exactly the limit
	const float big[2] = {60.0f, 30.0f};     // 67.08
	const float inf[2] = {std::numeric_limits<float>::infinity(), 0.0f};
	const float nan[2] = {0.0f, std::numeric_limits<float>::quiet_NaN()};
	CHECK(VR_RoomscaleMoveOk(zero) == 1);
	CHECK(VR_RoomscaleMoveOk(ok) == 1);
	CHECK(VR_RoomscaleMoveOk(edge) == 1);
	CHECK(VR_RoomscaleMoveOk(big) == 0);
	CHECK(VR_RoomscaleMoveOk(inf) == 0);
	CHECK(VR_RoomscaleMoveOk(nan) == 0);
	CHECK(VR_RoomscaleMoveOk(nullptr) == 0);
}
