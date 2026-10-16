// The VR move block (clc_hxvr): versioned, little-endian wire format, and the server's sanity check of an
// accumulated room-scale move.
#include "doctest.h"
#include "vr_api.h"

#include <cmath>
#include <cstring>
#include <limits>

TEST_CASE("move block round trip") {
	vr_moveblock_t b{};
	b.version = VR_MOVEBLOCK_VERSION;
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
	b.version = VR_MOVEBLOCK_VERSION;
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
	b.version = 2;
	b.flags = VR_MOVE_ROOMSCALE | 0x80u;   // a flag this version does not know
	b.roomscale[0] = -7.0f;
	b.roomscale[1] = 0.125f;
	unsigned char buf[VR_MOVEBLOCK_MAX];
	REQUIRE(VR_WriteMoveBlock(&b, buf, (int)sizeof(buf)) == 10);
	buf[10] = 0x12;   // v2's extra fields
	buf[11] = 0x34;

	vr_moveblock_t r{};
	REQUIRE(VR_ReadMoveBlock(buf, 12, &r) == 1);
	CHECK(r.version == 2);
	CHECK(r.flags == (VR_MOVE_ROOMSCALE | 0x80u));
	CHECK(r.roomscale[0] == -7.0f);
	CHECK(r.roomscale[1] == 0.125f);
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
