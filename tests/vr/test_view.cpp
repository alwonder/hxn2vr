#include "doctest.h"
#include "vr_api.h"

#include <cmath>

TEST_CASE("compose: body yaw + head yaw wrapped, head pitch/roll pass through") {
	const float head[3] = {-12.5f, 30.0f, 7.0f};
	float out[3];
	VR_ComposeViewAngles(170.0f, head, out);
	CHECK(out[0] == doctest::Approx(-12.5f));
	CHECK(out[1] == doctest::Approx(-160.0f));
	CHECK(out[2] == doctest::Approx(7.0f));
}

TEST_CASE("setangle: body yaw absorbs the head yaw so the composed view hits the server angle") {
	CHECK(VR_BodyYawFromSetAngle(90.0f, 30.0f) == doctest::Approx(60.0f));

	const float head[3] = {0.0f, 30.0f, 0.0f};
	float out[3];
	VR_ComposeViewAngles(60.0f, head, out);
	CHECK(out[1] == doctest::Approx(90.0f));

	// wraps into (-180, 180]
	CHECK(VR_BodyYawFromSetAngle(-170.0f, 30.0f) == doctest::Approx(160.0f));
	CHECK(VR_BodyYawFromSetAngle(10.0f, -170.0f) == doctest::Approx(180.0f));
}

TEST_CASE("setangle round trip for arbitrary yaws") {
	const float yaws[] = {-179.0f, -90.0f, 0.0f, 45.0f, 180.0f};
	for (float s : yaws)
		for (float h : yaws) {
			const float body = VR_BodyYawFromSetAngle(s, h);
			const float head[3] = {0.0f, h, 0.0f};
			float out[3];
			VR_ComposeViewAngles(body, head, out);
			float d = out[1] - s;
			while (d > 180.0f) d -= 360.0f;
			while (d < -180.0f) d += 360.0f;
			CHECK(std::fabs(d) < 1e-3f);
		}
}
