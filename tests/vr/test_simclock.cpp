#include "doctest.h"
#include "vr_api.h"

#include <limits>

namespace {

int run_frames(vr_simclock_t& c, int frames, double dt, int paused = 0) {
	int total = 0;
	for (int i = 0; i < frames; ++i) total += VR_SimTicks(&c, dt, paused);
	return total;
}

}  // namespace

TEST_CASE("simclock: 90 frames of 1/90 s give 72 ticks") {
	vr_simclock_t c{};
	const int ticks = run_frames(c, 90, 1.0 / 90.0);
	CHECK(ticks >= 71);
	CHECK(ticks <= 73);
}

TEST_CASE("simclock: 1000 frames of 1/120 s give 600 ticks") {
	vr_simclock_t c{};
	const int ticks = run_frames(c, 1000, 1.0 / 120.0);
	CHECK(ticks >= 599);
	CHECK(ticks <= 601);
}

TEST_CASE("simclock: a long frame is capped at 4 ticks and the excess is dropped") {
	vr_simclock_t c{};
	CHECK(VR_SimTicks(&c, 0.2, 0) == 4);
	// the excess was discarded: a following short frame does not owe extra ticks
	CHECK(VR_SimTicks(&c, 1.0 / 144.0, 0) <= 1);
	CHECK(c.acc < 1.0 / 72.0);
}

TEST_CASE("simclock: a frame of exactly one tick yields one tick") {
	vr_simclock_t c{};
	CHECK(VR_SimTicks(&c, 1.0 / 72.0, 0) == 1);
	CHECK(VR_SimTicks(&c, 1.0 / 72.0, 0) == 1);
}

TEST_CASE("simclock: paused returns 0 and does not accumulate") {
	vr_simclock_t c{};
	CHECK(run_frames(c, 50, 0.1, 1) == 0);
	CHECK(c.acc == doctest::Approx(0.0));
	// first frame after unpausing: 1/90 s < 1/72 s, no tick yet
	CHECK(VR_SimTicks(&c, 1.0 / 90.0, 0) == 0);
	// second frame: 2/90 s >= 1/72 s -> exactly one tick
	CHECK(VR_SimTicks(&c, 1.0 / 90.0, 0) == 1);
}

TEST_CASE("simclock: time accumulated before a pause is kept, not grown, by the pause") {
	vr_simclock_t c{};
	CHECK(VR_SimTicks(&c, 1.0 / 90.0, 0) == 0);
	CHECK(run_frames(c, 20, 1.0 / 90.0, 1) == 0);
	// 1/90 (kept) + 1/90 >= 1/72 -> one tick, not a burst
	CHECK(VR_SimTicks(&c, 1.0 / 90.0, 0) == 1);
}

TEST_CASE("simclock: non-positive dt gives no ticks") {
	vr_simclock_t c{};
	CHECK(VR_SimTicks(&c, 0.0, 0) == 0);
	CHECK(VR_SimTicks(&c, -1.0, 0) == 0);
	CHECK(c.acc == doctest::Approx(0.0));
}

TEST_CASE("simclock: a huge dt is O(1), capped at 4 ticks, leaves acc within one tick") {
	vr_simclock_t c{};
	CHECK(VR_SimTicks(&c, 1e9, 0) == 4);
	CHECK(c.acc >= 0.0);
	CHECK(c.acc < 1.0 / 72.0);
}

TEST_CASE("simclock: non-finite dt gives no ticks and leaves the accumulator unchanged") {
	vr_simclock_t c{};
	CHECK(VR_SimTicks(&c, 1.0 / 90.0, 0) == 0);
	const double before = c.acc;
	CHECK(VR_SimTicks(&c, std::numeric_limits<double>::infinity(), 0) == 0);
	CHECK(c.acc == before);
	CHECK(VR_SimTicks(&c, std::numeric_limits<double>::quiet_NaN(), 0) == 0);
	CHECK(c.acc == before);
	CHECK(VR_SimTicks(&c, -std::numeric_limits<double>::infinity(), 0) == 0);
	CHECK(c.acc == before);
}
