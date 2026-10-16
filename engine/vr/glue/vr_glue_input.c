/*
 * vr_glue_input.c -- engine-side glue for the hxvr VR module: controller buttons as engine keys
 * (K_VR_*, menu/console navigation, Menu = Escape), the default VR bindings, and the mock
 * controller dev commands. C, compiled into the engine; includes engine headers and vr_api.h only.
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"

cvar_t	vr_mainhand = {"vr_mainhand", "1", CVAR_ARCHIVE};	/* 0 left, 1 right (= VR_HAND_*) */

static void VRG_BindDefaults_f (void);
static void VRG_MockHand_f (void);
static void VRG_MockHead_f (void);
static void VRG_MockHandsReset (void);

void VRG_InputInit (void)
{
	Cvar_RegisterVariable (&vr_mainhand);
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

static int VRG_MainHand (void)
{
	return (vr_mainhand.integer == VR_HAND_LEFT) ? VR_HAND_LEFT : VR_HAND_RIGHT;
}

/* The key a button press sends in this key_dest (0 = none). Menu is Escape everywhere. In the menus
 * and the console: the main hand's trigger, A and X are Enter, B and Y are Escape. The key binding
 * menu (key_menubind) gets the raw K_VR_* keys. Chat (key_message) gets nothing but Menu. */
static int VRG_ButtonKey (keydest_t dest, int role, int b)
{
	if (b == VRG_BUTTON_MENU)
		return K_ESCAPE;
	if (dest == key_game || dest == key_menubind)
		return vrg_game_key[role][b];
	if (dest == key_menu || dest == key_console)
	{
		switch (vrg_button_bit[b])
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

void VRG_ReleaseAllKeys (void)
{
	int	hand, b, key;

	for (hand = 0; hand < 2; hand++)
	{
		for (b = 0; b < VRG_NUM_BUTTONS; b++)
		{
			key = vrg_in_key[hand][b];
			if (!key)
				continue;
			vrg_in_key[hand][b] = 0;	/* before Key_Event: it may re-enter */
			Key_Event (key, false);
		}
		memset (&vrg_in_repeat[hand], 0, sizeof(vrg_in_repeat[hand]));
	}
}

static const int	vrg_dir_key[5] = { 0, K_UPARROW, K_DOWNARROW, K_LEFTARROW, K_RIGHTARROW };	/* VR_DIR_* */

void VRG_InputFrame (void)
{
	const int	main_hand = VRG_MainHand ();
	const double	now = Sys_DoubleTime ();
	float		dt = (float) (now - vrg_in_time);
	int		hand, b, key, dir;

	/* stick repeat time: a long frame (loading, a sound cached on first use...) counts as at most
	 * 0.1 s, so a hitch does not turn one flick of the stick into a repeat */
	if (!(dt >= 0.0f))
		dt = 0.0f;
	if (dt > 0.1f)
		dt = 0.1f;
	vrg_in_time = now;

	if (main_hand != vrg_in_main)
	{
		/* buttons still held stay pressed in vrg_in_buttons: no new press until they are let go */
		VRG_ReleaseAllKeys ();
		vrg_in_main = main_hand;
	}

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
					Key_Event (key, false);	/* the key it was pressed with, whatever key_dest is now */
			}
			else if (!(vrg_in_buttons[hand] & bit) && (down & bit))
			{
				vrg_in_buttons[hand] |= bit;
				key = VRG_ButtonKey (Key_GetDest (), role, b);
				vrg_in_key[hand][b] = key;
				if (key)
					Key_Event (key, true);
			}
		}

		/* menus and console: either stick gives arrows (pressed and released at once) */
		dest = Key_GetDest ();
		if (dest == key_menu || dest == key_console)
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
				Key_Event (vrg_dir_key[dir], false);
			}
		}
		else
		{
			memset (&vrg_in_repeat[hand], 0, sizeof(vrg_in_repeat[hand]));
		}
	}
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

	memset (vrg_in_buttons, 0, sizeof(vrg_in_buttons));
	memset (vrg_in_key, 0, sizeof(vrg_in_key));
	memset (vrg_in_repeat, 0, sizeof(vrg_in_repeat));
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

static void VRG_MockHandsReset (void)
{
	int	i;

	memset (vrg_mock_hand, 0, sizeof(vrg_mock_hand));
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
	Con_Printf ("VR: usage: vr_mock_hand <left|right> <trigger|grip|a|b|stick|menu|sx|sy|active|valid> <value>\n");
}

static void VRG_SetButton (vr_hand_t *h, unsigned bit, qboolean on)
{
	if (on)
		h->buttons |= bit;
	else
		h->buttons &= ~bit;
}

/* vr_mock_hand <left|right> <field> <value>: sets one raw field of a mock hand. a = LOWER, b = UPPER
 * (whichever hand), grip = squeeze, stick = thumbstick click, sx / sy = stick axes. */
static void VRG_MockHand_f (void)
{
	const char	*hname, *field;
	float		v;
	int		hand;
	vr_hand_t	*h;

	if (Cmd_Argc () != 4)
	{
		VRG_MockHandUsage ();
		return;
	}
	hname = Cmd_Argv (1);
	field = Cmd_Argv (2);
	v = atof (Cmd_Argv (3));
	if (!q_strcasecmp (hname, "left"))
		hand = VR_HAND_LEFT;
	else if (!q_strcasecmp (hname, "right"))
		hand = VR_HAND_RIGHT;
	else
	{
		VRG_MockHandUsage ();
		return;
	}
	h = &vrg_mock_hand[hand];

	if (!q_strcasecmp (field, "trigger"))
		h->trigger = v;
	else if (!q_strcasecmp (field, "grip"))
		h->squeeze = v;
	else if (!q_strcasecmp (field, "a"))
		VRG_SetButton (h, VR_BTN_LOWER, v != 0);
	else if (!q_strcasecmp (field, "b"))
		VRG_SetButton (h, VR_BTN_UPPER, v != 0);
	else if (!q_strcasecmp (field, "stick"))
		VRG_SetButton (h, VR_BTN_STICK, v != 0);
	else if (!q_strcasecmp (field, "menu"))
		VRG_SetButton (h, VR_BTN_MENU, v != 0);
	else if (!q_strcasecmp (field, "sx"))
		h->stick[0] = v;
	else if (!q_strcasecmp (field, "sy"))
		h->stick[1] = v;
	else if (!q_strcasecmp (field, "active"))
		h->active = (v != 0);
	else if (!q_strcasecmp (field, "valid"))
		h->pose_valid = (v != 0);
	else
	{
		VRG_MockHandUsage ();
		return;
	}
	if (!VR_IsRunning ())
	{
		Con_Printf ("VR: not running\n");
		return;
	}
	VR_MockSetHand (hand, h);
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
