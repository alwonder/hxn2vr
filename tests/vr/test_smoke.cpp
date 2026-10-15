#include "doctest.h"
#include "vr_api.h"

TEST_CASE("units per metre") {
	CHECK(VR_UnitsPerMetre(1.65f, 1.0f) == doctest::Approx(50.0f / 1.65f));
	CHECK(VR_UnitsPerMetre(1.65f, 1.2f) == doctest::Approx(50.0f / 1.65f * 1.2f));
}
