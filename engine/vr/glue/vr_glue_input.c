/*
 * vr_glue_input.c -- engine-side glue for the hxvr VR module: controller buttons as engine keys
 * (K_VR_*, menu/console navigation, Menu = Escape), the default VR bindings, and the mock
 * controller dev commands. C, compiled into the engine; includes engine headers and vr_api.h only.
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"
#include "vr_glue_weapon.h"

cvar_t	vr_mainhand = {"vr_mainhand", "1", CVAR_ARCHIVE};	/* 0 left, 1 right (= VR_HAND_*) */
cvar_t	vr_movement_mode = {"vr_movement_mode", "1", CVAR_ARCHIVE};	/* 0 head, 1 aim of the off hand */
cvar_t	vr_turn_mode = {"vr_turn_mode", "0", CVAR_ARCHIVE};		/* 0 snap, 1 smooth */
cvar_t	vr_snap_angle = {"vr_snap_angle", "45", CVAR_ARCHIVE};		/* degrees */
cvar_t	vr_turn_speed = {"vr_turn_speed", "150", CVAR_ARCHIVE};	/* degrees per second (smooth) */
cvar_t	vr_stick_deadzone = {"vr_stick_deadzone", "0.15", CVAR_ARCHIVE};	/* radial */
cvar_t	vr_vignette = {"vr_vignette", "1", CVAR_ARCHIVE};		/* comfort vignette while the sticks move / smooth-turn */
cvar_t	vr_vignette_strength = {"vr_vignette_strength", "0.6", CVAR_ARCHIVE};	/* 0..1 */

static void VRG_BindDefaults_f (void);
static void VRG_MockHand_f (void);
static void VRG_MockHead_f (void);
static void VRG_MockHandsReset (void);
static void VRG_Where_f (void);

void VRG_InputInit (void)
{
	Cvar_RegisterVariable (&vr_mainhand);
	Cvar_RegisterVariable (&vr_movement_mode);
	Cvar_RegisterVariable (&vr_turn_mode);
	Cvar_RegisterVariable (&vr_snap_angle);
	Cvar_RegisterVariable (&vr_turn_speed);
	Cvar_RegisterVariable (&vr_stick_deadzone);
	Cvar_RegisterVariable (&vr_vignette);
	Cvar_RegisterVariable (&vr_vignette_strength);
	Cvar_RegisterVariable (&vr_debug_fire);
	Cmd_AddCommand ("vr_fireinfo", VRG_FireInfo_f);
	Cmd_AddCommand ("vr_where", VRG_Where_f);
	Cmd_AddCommand ("vr_bind_defaults", VRG_BindDefaults_f);
	Cmd_AddCommand ("vr_mock_hand", VRG_MockHand_f);
	Cmd_AddCommand ("vr_mock_head", VRG_MockHead_f);
	VRG_MockHandsReset ();
}

/* ---- buttons as keys ---- */

#define VRG_NUM_BUTTONS	6
static const unsigned	vrg_button_bit[VRG_NUM_BUTTONS] = {
	VR_BTN_TRIGGER, VR_BTN_GRIP, VR_BTN_LOWER, VR_BTN_UPPER, VR_BTN_STICK, VR_BTN_MENU
};
#define VRG_BUTTON_MENU	5	/* index of VR_BTN_MENU: always Escape, never a K_VR_* key */

/* the K_VR_* key of each button by hand role: [0] = main hand (vr_mainhand), [1] = the other */
static const int	vrg_game_key[2][VRG_NUM_BUTTONS - 1] = {
	{ K_VR_TRIGGER, K_VR_GRIP, K_VR_A, K_VR_B, K_VR_STICK },
	{ K_VR_OTRIGGER, K_VR_OGRIP, K_VR_X, K_VR_Y, K_VR_OSTICK }
};

static unsigned		vrg_in_buttons[2];			/* per hand: buttons seen down (0 while inactive) */
static int		vrg_in_key[2][VRG_NUM_BUTTONS];		/* key sent when the button went down, 0 = none */
static vr_repeat_t	vrg_in_repeat[2];			/* per hand: menu stick repeat */
static int		vrg_in_main = -1;			/* vr_mainhand (VR_HAND_*) of the last frame */
static double		vrg_in_time;				/* host time of the last VRG_InputFrame */
/* bumped by every VRG_InputFrame, VRG_InputStart and VRG_ReleaseAllKeys: a Key_Event that ran one of
 * them inside (a modal dialog's frames, a video restart from the menu...) has handled the buttons
 * already, so the interrupted frame stops instead of acting on its stale snapshot */
static unsigned		vrg_in_serial;

qboolean		vrg_input_modal;	/* vr_glue_int.h: VRG_ModalFrame is running a frame */

int VRG_MainHand (void)
{
	return (vr_mainhand.integer == VR_HAND_LEFT) ? VR_HAND_LEFT : VR_HAND_RIGHT;
}

/* The key a button press sends in this key_dest (0 = none). Menu is Escape everywhere. In the menus
 * and the console: the main hand's trigger, A and X are Enter, B and Y are Escape. The key binding
 * menu (key_menubind) gets the raw K_VR_* keys. Chat (key_message) gets nothing but Menu.
 * In a modal yes/no dialog (SCR_ModalMessage): the main trigger, A and X are 'y'; B, Y and Menu 'n'. */
static int VRG_ButtonKey (keydest_t dest, int role, int b)
{
	const unsigned	bit = vrg_button_bit[b];

	if (vrg_input_modal)
	{
		if ((bit == VR_BTN_TRIGGER && role == 0) || bit == VR_BTN_LOWER)
			return 'y';
		if (bit == VR_BTN_UPPER || bit == VR_BTN_MENU)
			return 'n';
		return 0;
	}
	if (b == VRG_BUTTON_MENU)
		return K_ESCAPE;
	if (dest == key_game || dest == key_menubind)
		return vrg_game_key[role][b];
	if (dest == key_menu || dest == key_console)
	{
		switch (bit)
		{
		case VR_BTN_TRIGGER:
			return (role == 0) ? K_ENTER : 0;
		case VR_BTN_LOWER:
			return K_ENTER;
		case VR_BTN_UPPER:
			return K_ESCAPE;
		default:
			return 0;
		}
	}
	return 0;
}

/* SCR_ModalMessage sets key_count to -1 and waits for key_lastpress to become 'y', 'n' or Escape.
 * A press there only sets key_lastpress (Key_Event stops at the key_count check); a release must not
 * answer the dialog (key_lastpress is kept) and still runs a +command's -command. */
static void VRG_KeyDown (int key)
{
	if (vrg_input_modal)
		key_count = -1;
	Key_Event (key, true);
}

static void VRG_KeyUp (int key)
{
	int	last, count;

	if (!vrg_input_modal)
	{
		Key_Event (key, false);
		return;
	}
	last = key_lastpress;
	count = key_count;
	key_count = 0;
	Key_Event (key, false);
	key_lastpress = last;
	key_count = count;
}

void VRG_ReleaseAllKeys (void)
{
	int	hand, b, key;

	vrg_in_serial++;
	for (hand = 0; hand < 2; hand++)
	{
		for (b = 0; b < VRG_NUM_BUTTONS; b++)
		{
			key = vrg_in_key[hand][b];
			if (!key)
				continue;
			vrg_in_key[hand][b] = 0;	/* before Key_Event: it may re-enter */
			VRG_KeyUp (key);
		}
		memset (&vrg_in_repeat[hand], 0, sizeof(vrg_in_repeat[hand]));
	}
}

static const int	vrg_dir_key[5] = { 0, K_UPARROW, K_DOWNARROW, K_LEFTARROW, K_RIGHTARROW };	/* VR_DIR_* */

static void VRG_MockQueueRun (double now);

void VRG_InputFrame (void)
{
	const int	main_hand = VRG_MainHand ();
	const double	now = Sys_DoubleTime ();
	const float	dt = (float) (now - vrg_in_time);	/* VR_StickRepeat caps it at 0.1 s */
	unsigned	serial;
	int		hand, b, key, dir;

	vrg_in_time = now;
	VRG_MockQueueRun (now);		/* delayed vr_mock_hand changes: seen by the next VR_BeginFrame */

	if (main_hand != vrg_in_main)
	{
		/* buttons still held stay pressed in vrg_in_buttons: no new press until they are let go */
		VRG_ReleaseAllKeys ();
		vrg_in_main = main_hand;
	}

	/* after each Key_Event: stop if it ran another input frame, a VR start or a release of all keys
	 * (vrg_in_serial moved on) */
	serial = ++vrg_in_serial;
#define VRG_INTERRUPTED()	(vrg_in_serial != serial)
	for (hand = 0; hand < 2; hand++)
	{
		const int	role = (hand == main_hand) ? 0 : 1;
		vr_hand_t	h;
		unsigned	down;
		keydest_t	dest;
		float		stick[2] = { 0.0f, 0.0f };

		VR_GetHand (hand, &h);
		down = h.active ? h.buttons : 0;	/* an inactive hand holds nothing */
		for (b = 0; b < VRG_NUM_BUTTONS; b++)
		{
			const unsigned	bit = vrg_button_bit[b];

			/* the stored state is updated before Key_Event, which may re-enter or not return */
			if ((vrg_in_buttons[hand] & bit) && !(down & bit))
			{
				vrg_in_buttons[hand] &= ~bit;
				key = vrg_in_key[hand][b];
				vrg_in_key[hand][b] = 0;
				if (key)
					VRG_KeyUp (key);	/* the key it was pressed with, whatever key_dest is now */
			}
			else if (!(vrg_in_buttons[hand] & bit) && (down & bit))
			{
				vrg_in_buttons[hand] |= bit;
				/* vr_calibrate takes its presses itself: no key, so nothing to release later */
				key = VRG_CalibratePress (hand, role, bit) ? 0 : VRG_ButtonKey (Key_GetDest (), role, b);
				vrg_in_key[hand][b] = key;
				if (key)
					VRG_KeyDown (key);
			}
			else
				continue;
			if (VRG_INTERRUPTED ())
				return;
		}

		/* menus and console: either stick gives arrows (pressed and released at once); not in a
		 * modal dialog */
		dest = Key_GetDest ();
		if (!vrg_input_modal && (dest == key_menu || dest == key_console))
		{
			if (h.active)
			{
				stick[0] = h.stick[0];
				stick[1] = h.stick[1];
			}
			dir = VR_StickRepeat (&vrg_in_repeat[hand], stick, dt);
			if (dir > VR_DIR_NONE && dir <= VR_DIR_RIGHT)
			{
				Key_Event (vrg_dir_key[dir], true);
				if (VRG_INTERRUPTED ())
					return;
				Key_Event (vrg_dir_key[dir], false);
				if (VRG_INTERRUPTED ())
					return;
			}
		}
		else
		{
			memset (&vrg_in_repeat[hand], 0, sizeof(vrg_in_repeat[hand]));
		}
	}
#undef VRG_INTERRUPTED
}

/* ---- stick locomotion and turning ---- */

static vr_turn_t	vrg_turn;	/* snap turn state of the main hand's stick */

/* Sticks move and turn only in the running game: not in menus / console / chat, demos, before the
 * signon is done, in an intermission or in a camera cutscene (cl.v.cameramode: the player is stuck in
 * the camera and does not move). */
static qboolean VRG_StickGameplay (void)
{
	return VR_IsRunning () && Key_GetDest () == key_game && !cls.demoplayback &&
		cls.signon == SIGNONS && !cl.intermission && !cl.v.cameramode;
}

/* Yaw change (degrees) of the main hand's stick for this frame; VRG_PreInput adds it to the body
 * yaw. The stick state is followed also while a menu is up (a push held when it closes does not
 * fire a snap turn), but the step only counts in the running game. */
float VRG_TurnStep (void)
{
	vr_hand_t	h;
	float		step;

	VR_GetHand (VRG_MainHand (), &h);
	step = VR_TurnStep (&vrg_turn, h.active ? h.stick[0] : 0.0f, (float) host_frametime,
			vr_turn_mode.integer != 0, vr_snap_angle.value, vr_turn_speed.value,
			vr_stick_deadzone.value);
	return VRG_StickGameplay () ? step : 0.0f;
}

/* CL_BaseMove, after the keyboard terms: the off hand's stick adds to forwardmove / sidemove, along
 * its aim (vr_movement_mode 1) or the head, in the frame of the view the server moves by. Speed key
 * and haste are applied by the engine afterwards. */
void VRG_StickMove (usercmd_t *cmd)
{
	vr_hand_t	hands[2], h;
	vr_pose_t	head_pose;
	float		head[3], stick[2], move[2], move_yaw, view[3];

	if (!VRG_StickGameplay ())
		return;
	VRG_TrackedPoses (&head_pose, NULL, hands);
	h = hands[1 - VRG_MainHand ()];
	if (!h.active)
		return;
	VR_StickDeadzone (h.stick, vr_stick_deadzone.value, stick);
	if (stick[0] == 0.0f && stick[1] == 0.0f)
		return;
	VR_TrackingToQuakeAngles (&head_pose.rot, head);
	/* a hand without a valid pose keeps stale poses: VR_MoveYaw falls back to the head */
	move_yaw = VR_MoveYaw (vr_movement_mode.integer, &h.aim.rot, h.pose_valid, head[YAW], vrg_body_yaw);
	/* Use the sent aim yaw even when the weapon points at the floor or ceiling. */
	VRG_AimAngles (view);
	VR_StickToMove (stick, move_yaw, view[YAW], 200.0f, move);
	cmd->forwardmove += move[0];
	cmd->sidemove += move[1];
}

/* ---- comfort vignette ---- */

static float		vrg_vignette;		/* 0..1: how much of the vignette strength is applied now */
static double		vrg_vignette_time;	/* host time of the previous VRG_VignetteAmount (0: none) */

/* VRG_UpdateScreen, once per rendered frame: the vignette amount, 0..1. The target is the larger of the
 * movement stick's deflection (off hand, after the deadzone) and, with smooth turning, the turn stick's
 * x deflection (main hand, after the deadzone) in the running game, else 0; the amount follows it at 5/s
 * (the fade in and out is not instant, so it does not flicker with the stick). */
float VRG_VignetteAmount (void)
{
	const double	now = Sys_DoubleTime ();
	const float	dt = vrg_vignette_time > 0 ? (float) (now - vrg_vignette_time) : 0.0f;
	float		target = 0.0f;

	vrg_vignette_time = now;
	if (VRG_StickGameplay ())
	{
		vr_hand_t	main_h, other;
		float		in[2], out[2];

		VR_GetHand (VRG_MainHand (), &main_h);
		VR_GetHand (1 - VRG_MainHand (), &other);
		if (other.active)
		{
			VR_StickDeadzone (other.stick, vr_stick_deadzone.value, out);
			target = sqrtf (out[0] * out[0] + out[1] * out[1]);
		}
		if (main_h.active && vr_turn_mode.integer != 0)
		{
			in[0] = main_h.stick[0];
			in[1] = 0.0f;
			VR_StickDeadzone (in, vr_stick_deadzone.value, out);
			target = q_max (target, fabsf (out[0]));
		}
	}
	vrg_vignette = VR_Approach (vrg_vignette, q_min (target, 1.0f), 5.0f, dt);
	return vrg_vignette;
}

/* vr_where: one line with the player's state, for the smoke tests (see the Where scenarios). */
static void VRG_Where_f (void)
{
	const float	*org = cl_entities[cl.viewentity].origin;
	int		hand, b, held = 0;

	for (hand = 0; hand < 2; hand++)
	{
		for (b = 0; b < VRG_NUM_BUTTONS; b++)
		{
			if (vrg_in_key[hand][b])
				held++;
		}
	}
	/* lean: the head's room-scale lean from the feet (world units); crouch: the physical crouch held
	 * (VRG_CrouchHeld); crouchkey: the +crouch button */
	Con_Printf ("VR: where origin %.1f %.1f %.1f yaw %.1f lean %.2f %.2f view %.1f crouch %d held %d crouchkey %d\n",
			org[0], org[1], org[2], vrg_body_yaw, vrg_room.lean[0], vrg_room.lean[1], cl.viewheight,
			VRG_CrouchHeld () ? 1 : 0, held, in_crouch.state & 1);
}

/* ---- default bindings ---- */

static const struct
{
	int		key;
	const char	*binding;
} vrg_default_binds[] = {
	{ K_VR_TRIGGER,		"+attack" },
	{ K_VR_GRIP,		"impulse 10" },		/* next weapon */
	{ K_VR_A,		"+jump" },
	{ K_VR_B,		"+crouch" },
	{ K_VR_STICK,		"vr_recenter" },
	{ K_VR_OTRIGGER,	"invuse" },
	{ K_VR_OGRIP,		"+showinfo" },
	{ K_VR_X,		"invleft" },
	{ K_VR_Y,		"invright" },
	{ K_VR_OSTICK,		"+speed" }
};
#define VRG_NUM_DEFAULT_BINDS	((int) (sizeof(vrg_default_binds) / sizeof(vrg_default_binds[0])))

static void VRG_BindDefaults_f (void)
{
	int	i;

	for (i = 0; i < VRG_NUM_DEFAULT_BINDS; i++)
		Key_SetBinding (vrg_default_binds[i].key, vrg_default_binds[i].binding);
	Con_Printf ("VR: default controller bindings set\n");
}

void VRG_InputStart (void)
{
	int	i;

	vrg_in_serial++;	/* an input frame this start interrupted stops */
	memset (vrg_in_buttons, 0, sizeof(vrg_in_buttons));
	memset (vrg_in_key, 0, sizeof(vrg_in_key));
	memset (vrg_in_repeat, 0, sizeof(vrg_in_repeat));
	memset (&vrg_turn, 0, sizeof(vrg_turn));
	vrg_vignette = 0;
	vrg_vignette_time = 0;
	vrg_in_main = VRG_MainHand ();
	vrg_in_time = Sys_DoubleTime ();
	VRG_MockHandsReset ();

	/* first VR start (no VR key bound yet, e.g. no config.cfg from a VR session): default layout */
	for (i = 0; i < VRG_NUM_DEFAULT_BINDS; i++)
	{
		if (keybindings[vrg_default_binds[i].key])
			return;
	}
	VRG_BindDefaults_f ();
}

/* ---- mock controllers (dev commands, mock backend only) ---- */

/* what vr_mock_hand last sent to the module, one copy per hand (the mock's default hands after a
 * VR start: active, both poses valid, held in front of the chest) */
static vr_hand_t	vrg_mock_hand[2];
static float	vrg_mock_angles[2][2];   /* Quake yaw and pitch, degrees */

/* delayed vr_mock_hand changes (host time), applied in order by VRG_InputFrame -- which also runs
 * inside a modal dialog's loop, where the command buffer does not run */
#define VRG_MOCK_QUEUE	16
static struct
{
	double	at;
	int	hand, field;
	float	v;
} vrg_mock_queue[VRG_MOCK_QUEUE];
static int	vrg_mock_queued;

static void VRG_MockHandsReset (void)
{
	int	i;

	memset (vrg_mock_hand, 0, sizeof(vrg_mock_hand));
	memset (vrg_mock_angles, 0, sizeof(vrg_mock_angles));
	vrg_mock_queued = 0;
	for (i = 0; i < 2; i++)
	{
		vr_hand_t	*h = &vrg_mock_hand[i];

		h->active = 1;
		h->pose_valid = 1;
		h->grip.pos.x = h->aim.pos.x = (i == VR_HAND_LEFT) ? -0.2f : 0.2f;
		h->grip.pos.y = h->aim.pos.y = 1.0f;
		h->grip.pos.z = h->aim.pos.z = -0.3f;
		h->grip.rot.w = h->aim.rot.w = 1.0f;
	}
}

static void VRG_MockHandUsage (void)
{
	Con_Printf ("VR: usage: vr_mock_hand <left|right> <trigger|grip|a|b|stick|menu|sx|sy|active|valid|px|py|pz|yaw|pitch> <value> [delay]\n");
}

static void VRG_SetButton (vr_hand_t *h, unsigned bit, qboolean on)
{
	if (on)
		h->buttons |= bit;
	else
		h->buttons &= ~bit;
}

static const char	*vrg_mock_fields[] = {
	"trigger", "grip", "a", "b", "stick", "menu", "sx", "sy", "active", "valid",
	"px", "py", "pz", "yaw", "pitch"
};
#define VRG_NUM_MOCK_FIELDS	((int) (sizeof(vrg_mock_fields) / sizeof(vrg_mock_fields[0])))

/* sets field (index into vrg_mock_fields) of the mock hand and sends the hand to the module */
static void VRG_MockApply (int hand, int field, float v)
{
	vr_hand_t	*h = &vrg_mock_hand[hand];

	switch (field)
	{
	case 0:	h->trigger = v;					break;
	case 1:	h->squeeze = v;					break;
	case 2:	VRG_SetButton (h, VR_BTN_LOWER, v != 0);	break;
	case 3:	VRG_SetButton (h, VR_BTN_UPPER, v != 0);	break;
	case 4:	VRG_SetButton (h, VR_BTN_STICK, v != 0);	break;
	case 5:	VRG_SetButton (h, VR_BTN_MENU, v != 0);		break;
	case 6:	h->stick[0] = v;				break;
	case 7:	h->stick[1] = v;				break;
	case 8:	h->active = (v != 0);				break;
	case 9:	h->pose_valid = (v != 0);			break;
	case 10: h->grip.pos.x = h->aim.pos.x = v;		break;
	case 11: h->grip.pos.y = h->aim.pos.y = v;		break;
	case 12: h->grip.pos.z = h->aim.pos.z = v;		break;
	case 13: vrg_mock_angles[hand][0] = v;			break;
	case 14: vrg_mock_angles[hand][1] = v;			break;
	default:						return;
	}
	if (field == 13 || field == 14)
	{
		/* Tracking rotation: +Y yaw turns left; -X pitch points down.
		 * q = yaw(Y) * pitch(-X), using half angles in radians. */
		const float yaw = vrg_mock_angles[hand][0] * (3.14159265358979323846f / 360.0f);
		const float pitch = vrg_mock_angles[hand][1] * (3.14159265358979323846f / 360.0f);
		const float sy = sinf (yaw), cy = cosf (yaw), sp = sinf (pitch), cp = cosf (pitch);

		h->aim.rot.x = -cy * sp;
		h->aim.rot.y = sy * cp;
		h->aim.rot.z = sy * sp;
		h->aim.rot.w = cy * cp;
		h->grip.rot = h->aim.rot;
	}
	VR_MockSetHand (hand, h);
}

static void VRG_MockQueueRun (double now)
{
	int	i, n = 0;

	for (i = 0; i < vrg_mock_queued; i++)
	{
		if (vrg_mock_queue[i].at <= now)
			VRG_MockApply (vrg_mock_queue[i].hand, vrg_mock_queue[i].field, vrg_mock_queue[i].v);
		else
			vrg_mock_queue[n++] = vrg_mock_queue[i];
	}
	vrg_mock_queued = n;
}

/* vr_mock_hand <left|right> <field> <value> [delay]: sets one raw field of a mock hand, at once or
 * `delay` seconds of host time later. a = LOWER, b = UPPER (whichever hand), grip = squeeze,
 * stick = thumbstick click, sx / sy = stick axes. */
static void VRG_MockHand_f (void)
{
	const char	*hname, *field;
	float		v, delay = 0;
	int		hand, f;

	if (Cmd_Argc () != 4 && Cmd_Argc () != 5)
	{
		VRG_MockHandUsage ();
		return;
	}
	hname = Cmd_Argv (1);
	field = Cmd_Argv (2);
	v = atof (Cmd_Argv (3));
	if (Cmd_Argc () == 5)
		delay = atof (Cmd_Argv (4));
	if (!q_strcasecmp (hname, "left"))
		hand = VR_HAND_LEFT;
	else if (!q_strcasecmp (hname, "right"))
		hand = VR_HAND_RIGHT;
	else
	{
		VRG_MockHandUsage ();
		return;
	}
	for (f = 0; f < VRG_NUM_MOCK_FIELDS; f++)
	{
		if (!q_strcasecmp (field, vrg_mock_fields[f]))
			break;
	}
	if (f == VRG_NUM_MOCK_FIELDS || !(delay >= 0))
	{
		VRG_MockHandUsage ();
		return;
	}
	if (!VR_IsRunning ())
	{
		Con_Printf ("VR: not running\n");
		return;
	}
	if (delay > 0)
	{
		if (vrg_mock_queued == VRG_MOCK_QUEUE)
		{
			Con_Printf ("VR: vr_mock_hand queue full, ignored\n");
			return;
		}
		vrg_mock_queue[vrg_mock_queued].at = Sys_DoubleTime () + delay;
		vrg_mock_queue[vrg_mock_queued].hand = hand;
		vrg_mock_queue[vrg_mock_queued].field = f;
		vrg_mock_queue[vrg_mock_queued].v = v;
		vrg_mock_queued++;
		return;
	}
	VRG_MockApply (hand, f, v);
}

/* vr_mock_head <x> <y> <z> [seconds]: moves the mock head to this offset from its rest position
 * (metres, tracking axes: x right, y up, -z forward), linearly over `seconds` (default: at once). */
static void VRG_MockHead_f (void)
{
	vr_vec3_t	target;
	float		seconds = 0;

	if (Cmd_Argc () < 4 || Cmd_Argc () > 5)
	{
		Con_Printf ("VR: usage: vr_mock_head <x> <y> <z> [seconds]\n");
		return;
	}
	target.x = atof (Cmd_Argv (1));
	target.y = atof (Cmd_Argv (2));
	target.z = atof (Cmd_Argv (3));
	if (Cmd_Argc () == 5)
		seconds = atof (Cmd_Argv (4));
	if (!VR_IsRunning ())
	{
		Con_Printf ("VR: not running\n");
		return;
	}
	VR_MockSetHeadOffset (&target, seconds);
}
