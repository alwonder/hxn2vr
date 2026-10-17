/*
 * vr_glue_server.c -- engine-side glue for the hxvr VR module, server side: reads the VR move block a VR
 * client sends to its local server (clc_hxvr) and keeps its room-scale move for the next physics tick
 * (sv_phys.c: SV_HXVR_RoomscaleMove).
 * C, compiled into the engine; includes engine headers and vr_api.h only.
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"

cvar_t vr_debug_fire = {"vr_debug_fire", "0", CVAR_NONE};
static qboolean vrg_fire_free[MAX_EDICTS];
static int vrg_fire_count = -1;
static char vrg_last_fire[512];

/* Snapshot for debug logging or enabled VR haptics: appended slots and previously free slots are new entities.
 * Do this for each progs call, not each tick: an existing missile must not count again. */
void VRG_ServerFireBegin (void)
{
	int i;
	vrg_fire_count = -1;
	if (!vr_debug_fire.value && !VRG_HapticsEnabled ())
		return;
	vrg_fire_count = sv.num_edicts;
	for (i = 0; i < vrg_fire_count; i++)
		vrg_fire_free[i] = EDICT_NUM(i)->free;
}

void VRG_ServerFireEnd (edict_t *player, const float muzzle[3], const float body[3], qboolean wrapped)
{
	int i;
	edict_t *e;
	if (vrg_fire_count < 0)
		return;
	for (i = 1; i < sv.num_edicts; i++)
	{
		e = EDICT_NUM(i);
		if ((i >= vrg_fire_count || vrg_fire_free[i]) && !e->free &&
		    e->v.owner == EDICT_TO_PROG(player))
		{
			if (wrapped && vr_debug_fire.value)
			{
				q_snprintf (vrg_last_fire, sizeof(vrg_last_fire), "fire muzzle %.3f %.3f %.3f body %.3f %.3f %.3f proj %.3f %.3f %.3f\n",
					muzzle[0], muzzle[1], muzzle[2], body[0], body[1], body[2],
					e->v.origin[0], e->v.origin[1], e->v.origin[2]);
				Con_SafePrintf ("VR: %s", vrg_last_fire);
			}
			/* Only the viewed local player drives this process's controllers. */
			if (NUM_FOR_EDICT(player) == cl.viewentity)
				VRG_OnWeaponFire ();
			break;
		}
	}
	vrg_fire_count = -1;
}

void VRG_FireInfo_f (void)
{
	if (vrg_last_fire[0])
		Con_SafePrintf ("VR: last %s", vrg_last_fire);
	else
		Con_SafePrintf ("VR: no wrapped fire recorded\n");
}

/* clc_hxvr (SV_ReadClientMessage): u8 len, len bytes of the move block. The bytes are always consumed;
 * a block that is too long, unreadable or from a client that is not the local one (the loopback socket
 * of a listen server is "LOCAL") is ignored. A room-scale move adds to cl->hxvr_move: several messages
 * can come in before the next physics tick, which takes the sum. A read past the end of the message
 * sets msg_badread: SV_ReadClientMessage then drops the client as for any other message. */
void VRG_ServerReadMoveBlock (client_t *cl)
{
	unsigned char	blk[256];
	vr_moveblock_t	b;
	int		len, i;

	/* HXVR: every rejected/partial block invalidates the previous weapon sample. */
	cl->hxvr_weapon = false;
	cl->hxvr_weapon_time = 0;
	len = MSG_ReadByte ();
	if (msg_badread || len < 0)
		return;
	for (i = 0; i < len; i++)
	{
		blk[i] = (unsigned char) MSG_ReadByte ();
		if (msg_badread)
			return;
	}
	if (len > VR_MOVEBLOCK_MAX || !VR_ReadMoveBlock (blk, len, &b))
		return;
	if (!cl->netconnection || strcmp (NET_QSocketGetAddressString (cl->netconnection), "LOCAL"))
		return;
	if ((b.flags & VR_MOVE_WEAPON) && VR_MuzzleOffsetOk (b.muzzle))
	{
		cl->hxvr_weapon = true;
		VectorCopy (b.muzzle, cl->hxvr_muzzle);
		VectorCopy (b.aim, cl->hxvr_aim);
		VectorCopy (b.aim, cl->edict->v.v_angle);
		cl->hxvr_weapon_time = sv.time;
	}
	if (b.flags & VR_MOVE_ROOMSCALE)
	{
		cl->hxvr_move[0] += b.roomscale[0];
		cl->hxvr_move[1] += b.roomscale[1];
	}
}
