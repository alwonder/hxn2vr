// Suggested OpenXR interaction-profile bindings (xr_bindings): the pure table the OpenXR backend suggests.
#include "doctest.h"
#include "xr_bindings.hpp"

#include <cstring>
#include <set>
#include <string>
#include <utility>

using hxvr::Act;
using hxvr::Binding;
using hxvr::ProfileBindings;
using hxvr::suggestedBindings;

namespace {

const char* const kLeft = "/user/hand/left/input/";
const char* const kRight = "/user/hand/right/input/";

bool startsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

bool has(const ProfileBindings& p, Act act, const std::string& path) {
	for (const Binding& b : p.bindings)
		if (b.act == act && path == b.path) return true;
	return false;
}

bool hasOnHand(const ProfileBindings& p, Act act, const char* hand) {
	for (const Binding& b : p.bindings)
		if (b.act == act && startsWith(b.path, hand)) return true;
	return false;
}

const ProfileBindings* find(const char* profile) {
	for (const ProfileBindings& p : suggestedBindings())
		if (std::strcmp(p.profile, profile) == 0) return &p;
	return nullptr;
}

}  // namespace

TEST_CASE("the table has the five interaction profiles") {
	CHECK(suggestedBindings().size() == 5);
	CHECK(find("/interaction_profiles/oculus/touch_controller"));
	CHECK(find("/interaction_profiles/valve/index_controller"));
	CHECK(find("/interaction_profiles/htc/vive_controller"));
	CHECK(find("/interaction_profiles/microsoft/motion_controller"));
	CHECK(find("/interaction_profiles/khr/simple_controller"));
}

TEST_CASE("every profile binds poses, trigger and a menu") {
	for (const ProfileBindings& p : suggestedBindings()) {
		CAPTURE(std::string(p.profile));
		for (const char* hand : {kLeft, kRight}) {
			CAPTURE(std::string(hand));
			CHECK(has(p, Act::GripPose, std::string(hand) + "grip/pose"));
			CHECK(has(p, Act::AimPose, std::string(hand) + "aim/pose"));
			CHECK(hasOnHand(p, Act::Trigger, hand));
		}
		CHECK((hasOnHand(p, Act::Menu, kLeft) || hasOnHand(p, Act::Menu, kRight)));
	}
}

TEST_CASE("binding paths are well formed and unique") {
	for (const ProfileBindings& p : suggestedBindings()) {
		CAPTURE(std::string(p.profile));
		CHECK(startsWith(p.profile, "/interaction_profiles/"));
		CHECK_FALSE(p.bindings.empty());
		std::set<std::pair<int, std::string>> seen;
		for (const Binding& b : p.bindings) {
			REQUIRE(b.path != nullptr);
			CAPTURE(std::string(b.path));
			const bool left = startsWith(b.path, kLeft), right = startsWith(b.path, kRight);
			CHECK((left || right));
			const std::size_t prefix = std::strlen(left ? kLeft : kRight);
			CHECK(std::strlen(b.path) > prefix);              // a component follows the hand
			CHECK(b.path[std::strlen(b.path) - 1] != '/');   // no trailing slash
			CHECK(std::strchr(b.path, ' ') == nullptr);
			CHECK(seen.insert({static_cast<int>(b.act), std::string(b.path)}).second);   // no duplicate (act, path)
		}
	}
}

TEST_CASE("index left upper button is the menu") {
	const ProfileBindings* p = find("/interaction_profiles/valve/index_controller");
	REQUIRE(p);
	CHECK(has(*p, Act::Menu, "/user/hand/left/input/b/click"));
	CHECK_FALSE(hasOnHand(*p, Act::Upper, kLeft));
	CHECK(has(*p, Act::Upper, "/user/hand/right/input/b/click"));
	CHECK(has(*p, Act::Lower, "/user/hand/left/input/a/click"));
	CHECK(has(*p, Act::Lower, "/user/hand/right/input/a/click"));
	CHECK_FALSE(hasOnHand(*p, Act::Menu, kRight));
}

TEST_CASE("touch: face buttons, analog squeeze and the left menu") {
	const ProfileBindings* p = find("/interaction_profiles/oculus/touch_controller");
	REQUIRE(p);
	CHECK(has(*p, Act::Lower, "/user/hand/left/input/x/click"));
	CHECK(has(*p, Act::Lower, "/user/hand/right/input/a/click"));
	CHECK(has(*p, Act::Upper, "/user/hand/left/input/y/click"));
	CHECK(has(*p, Act::Upper, "/user/hand/right/input/b/click"));
	CHECK(has(*p, Act::Menu, "/user/hand/left/input/menu/click"));
	CHECK_FALSE(hasOnHand(*p, Act::Menu, kRight));
	for (const char* hand : {kLeft, kRight}) {
		CHECK(has(*p, Act::Trigger, std::string(hand) + "trigger/value"));
		CHECK(has(*p, Act::Squeeze, std::string(hand) + "squeeze/value"));
		CHECK(has(*p, Act::Stick, std::string(hand) + "thumbstick"));
		CHECK(has(*p, Act::StickClick, std::string(hand) + "thumbstick/click"));
	}
}

TEST_CASE("vive uses the trackpad as the stick, simple has only select and menu") {
	const ProfileBindings* vive = find("/interaction_profiles/htc/vive_controller");
	REQUIRE(vive);
	for (const char* hand : {kLeft, kRight}) {
		CHECK(has(*vive, Act::Stick, std::string(hand) + "trackpad"));
		CHECK(has(*vive, Act::StickClick, std::string(hand) + "trackpad/click"));
		CHECK(has(*vive, Act::Squeeze, std::string(hand) + "squeeze/click"));
		CHECK(has(*vive, Act::Menu, std::string(hand) + "menu/click"));
	}
	CHECK_FALSE((hasOnHand(*vive, Act::Lower, kLeft) || hasOnHand(*vive, Act::Lower, kRight)));
	CHECK_FALSE((hasOnHand(*vive, Act::Upper, kLeft) || hasOnHand(*vive, Act::Upper, kRight)));

	const ProfileBindings* wmr = find("/interaction_profiles/microsoft/motion_controller");
	REQUIRE(wmr);
	for (const char* hand : {kLeft, kRight}) {
		CHECK(has(*wmr, Act::Stick, std::string(hand) + "thumbstick"));
		CHECK(has(*wmr, Act::Squeeze, std::string(hand) + "squeeze/click"));
		CHECK(has(*wmr, Act::Menu, std::string(hand) + "menu/click"));
	}

	const ProfileBindings* simple = find("/interaction_profiles/khr/simple_controller");
	REQUIRE(simple);
	CHECK(simple->bindings.size() == 8);   // per hand: grip, aim, select, menu
	for (const char* hand : {kLeft, kRight}) {
		CHECK(has(*simple, Act::Trigger, std::string(hand) + "select/click"));
		CHECK(has(*simple, Act::Menu, std::string(hand) + "menu/click"));
	}
}
