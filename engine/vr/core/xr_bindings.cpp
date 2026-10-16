// xr_bindings.cpp -- the suggested-bindings table (see xr_bindings.hpp).
#include "xr_bindings.hpp"

namespace hxvr {

#define HXVR_L "/user/hand/left/input/"
#define HXVR_R "/user/hand/right/input/"

const std::vector<ProfileBindings>& suggestedBindings() {
	static const std::vector<ProfileBindings> table = {
		{"/interaction_profiles/oculus/touch_controller",
		 {
			 {Act::GripPose, HXVR_L "grip/pose"},          {Act::GripPose, HXVR_R "grip/pose"},
			 {Act::AimPose, HXVR_L "aim/pose"},            {Act::AimPose, HXVR_R "aim/pose"},
			 {Act::Trigger, HXVR_L "trigger/value"},       {Act::Trigger, HXVR_R "trigger/value"},
			 {Act::Squeeze, HXVR_L "squeeze/value"},       {Act::Squeeze, HXVR_R "squeeze/value"},
			 {Act::Stick, HXVR_L "thumbstick"},            {Act::Stick, HXVR_R "thumbstick"},
			 {Act::StickClick, HXVR_L "thumbstick/click"}, {Act::StickClick, HXVR_R "thumbstick/click"},
			 {Act::Lower, HXVR_L "x/click"},               {Act::Lower, HXVR_R "a/click"},
			 {Act::Upper, HXVR_L "y/click"},               {Act::Upper, HXVR_R "b/click"},
			 {Act::Menu, HXVR_L "menu/click"},
		 }},
		{"/interaction_profiles/valve/index_controller",
		 {
			 {Act::GripPose, HXVR_L "grip/pose"},          {Act::GripPose, HXVR_R "grip/pose"},
			 {Act::AimPose, HXVR_L "aim/pose"},            {Act::AimPose, HXVR_R "aim/pose"},
			 {Act::Trigger, HXVR_L "trigger/value"},       {Act::Trigger, HXVR_R "trigger/value"},
			 {Act::Squeeze, HXVR_L "squeeze/value"},       {Act::Squeeze, HXVR_R "squeeze/value"},
			 {Act::Stick, HXVR_L "thumbstick"},            {Act::Stick, HXVR_R "thumbstick"},
			 {Act::StickClick, HXVR_L "thumbstick/click"}, {Act::StickClick, HXVR_R "thumbstick/click"},
			 {Act::Lower, HXVR_L "a/click"},               {Act::Lower, HXVR_R "a/click"},
			 {Act::Upper, HXVR_R "b/click"},
			 {Act::Menu, HXVR_L "b/click"},   // the left upper button is the menu (Index has no menu button)
		 }},
		{"/interaction_profiles/htc/vive_controller",
		 {
			 {Act::GripPose, HXVR_L "grip/pose"},        {Act::GripPose, HXVR_R "grip/pose"},
			 {Act::AimPose, HXVR_L "aim/pose"},          {Act::AimPose, HXVR_R "aim/pose"},
			 {Act::Trigger, HXVR_L "trigger/value"},     {Act::Trigger, HXVR_R "trigger/value"},
			 {Act::Squeeze, HXVR_L "squeeze/click"},     {Act::Squeeze, HXVR_R "squeeze/click"},
			 {Act::Stick, HXVR_L "trackpad"},            {Act::Stick, HXVR_R "trackpad"},
			 {Act::StickClick, HXVR_L "trackpad/click"}, {Act::StickClick, HXVR_R "trackpad/click"},
			 {Act::Menu, HXVR_L "menu/click"},           {Act::Menu, HXVR_R "menu/click"},
		 }},
		{"/interaction_profiles/microsoft/motion_controller",
		 {
			 {Act::GripPose, HXVR_L "grip/pose"},          {Act::GripPose, HXVR_R "grip/pose"},
			 {Act::AimPose, HXVR_L "aim/pose"},            {Act::AimPose, HXVR_R "aim/pose"},
			 {Act::Trigger, HXVR_L "trigger/value"},       {Act::Trigger, HXVR_R "trigger/value"},
			 {Act::Squeeze, HXVR_L "squeeze/click"},       {Act::Squeeze, HXVR_R "squeeze/click"},
			 {Act::Stick, HXVR_L "thumbstick"},            {Act::Stick, HXVR_R "thumbstick"},
			 {Act::StickClick, HXVR_L "thumbstick/click"}, {Act::StickClick, HXVR_R "thumbstick/click"},
			 {Act::Menu, HXVR_L "menu/click"},             {Act::Menu, HXVR_R "menu/click"},
		 }},
		{"/interaction_profiles/khr/simple_controller",
		 {
			 {Act::GripPose, HXVR_L "grip/pose"},    {Act::GripPose, HXVR_R "grip/pose"},
			 {Act::AimPose, HXVR_L "aim/pose"},      {Act::AimPose, HXVR_R "aim/pose"},
			 {Act::Trigger, HXVR_L "select/click"},  {Act::Trigger, HXVR_R "select/click"},
			 {Act::Menu, HXVR_L "menu/click"},       {Act::Menu, HXVR_R "menu/click"},
		 }},
	};
	return table;
}

#undef HXVR_L
#undef HXVR_R

}  // namespace hxvr
