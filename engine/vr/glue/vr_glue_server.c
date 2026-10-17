/*
 * vr_glue_server.c -- engine-side glue for the hxvr VR module, server side: reads the VR move block a VR
 * client sends to its local server (clc_hxvr) and keeps its room-scale move for the next physics tick
 * (sv_phys.c: SV_HXVR_RoomscaleMove).
 * C, compiled into the engine; includes engine headers and vr_api.h only.
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"

cvar_t vr_debug_fire = {"vr_debug_fire", "0", CVAR_NONE};
static qboolean vrg_fire_free[MAX_EDICTS];
static int vrg_fire_count = -1;
static char vrg_last_fire[512];
static vec3_t vrg_probe_raw, vrg_probe_hit;
static qboolean vrg_probe_blocked;
static qboolean vrg_probe_startsolid;
static vec3_t vrg_probe_eye, vrg_probe_forward;

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
				if (vrg_probe_blocked)
					Con_SafePrintf ("VR: clamp raw %.3f %.3f %.3f hit %.3f %.3f %.3f start %d eye %.3f %.3f %.3f aim %.6f %.6f %.6f\n",
						vrg_probe_raw[0], vrg_probe_raw[1], vrg_probe_raw[2],
						vrg_probe_hit[0], vrg_probe_hit[1], vrg_probe_hit[2],
						vrg_probe_startsolid, vrg_probe_eye[0], vrg_probe_eye[1], vrg_probe_eye[2],
						vrg_probe_forward[0], vrg_probe_forward[1], vrg_probe_forward[2]);
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

/* Shared by server compatibility and the client aim dot. */
static void VRG_MuzzleTrace (const float start[3], const float end[3], edict_t *ent, trace_t *trace)
{
	vec3_t p1, p2;
	if (sv.active)
	{
		VectorCopy (start, p1);
		VectorCopy (end, p2);
		*trace = SV_Move (p1, vec3_origin, vec3_origin, p2, MOVE_NOMONSTERS, ent);
	}
	else
		VRG_WorldTrace (start, end, trace);
}

void VRG_ClampMuzzle (edict_t *ent, const float start[3], const float muzzle[3],
                      const float angles[3], float out[3])
{
	vec3_t forward, right, up, end, hit, aim;
	trace_t trace;
	int blocked;
	const float clearance = 16;
	VectorCopy (angles, aim);
	AngleVectors (aim, forward, right, up);
	VRG_MuzzleTrace (start, muzzle, ent, &trace);
	/* Diagnostic only: distinguish an invalid approach from a close-wall pullback. */
	vrg_probe_startsolid = trace.startsolid || trace.allsolid;
	VectorCopy (start, vrg_probe_eye);
	VectorCopy (forward, vrg_probe_forward);
	blocked = trace.startsolid || trace.allsolid || trace.fraction < 1;
	VectorCopy (trace.endpos, hit);
	if (trace.startsolid || trace.allsolid)
		VectorCopy (start, hit);
	if (!blocked)
	{
		/* Stock progs adds up to 16 units along aim when spawning a missile. */
		VectorMA (muzzle, clearance, forward, end);
		VRG_MuzzleTrace (muzzle, end, ent, &trace);
		blocked = trace.startsolid || trace.allsolid || trace.fraction < 1;
		VectorCopy (trace.endpos, hit);
		if (trace.startsolid || trace.allsolid)
			VectorCopy (muzzle, hit);
		if (blocked)
		{
			/* The pullback follows eye->hit, which may cross a corner before that hit. */
			VRG_MuzzleTrace (start, hit, ent, &trace);
			vrg_probe_startsolid |= trace.startsolid || trace.allsolid;
			VectorCopy (trace.endpos, hit);
			if (trace.startsolid || trace.allsolid)
				VectorCopy (start, hit);
		}
	}
	VectorCopy (muzzle, vrg_probe_raw);
	VectorCopy (hit, vrg_probe_hit);
	vrg_probe_blocked = blocked;
	VR_CompatClampMuzzle (start, muzzle, hit, blocked, clearance, out);
}

/* Mock-only smoke fixture. No normal movement, firing or clamp behavior is changed.
 * Suspend a clear player hull near a traced vertical wall, avoiding timed walking
 * into stairs/slopes. The script must release all inputs and verify stable samples. */
void VRG_MockWall_f (void)
{
	static qboolean placed;
	static vec3_t saved_origin;
	static float saved_movetype;
	edict_t *player;
	vr_status_t status;
	vec3_t eye, end, forward, right, up, angles, candidate, candidate_eye, delta;
	trace_t wall, hull, check;
	int raise, distance;

	VR_GetStatus (&status);
	if (!VR_IsRunning () || strcmp (status.backend, "mock") || !vr_debug_fire.integer ||
	    !sv.active || svs.maxclients != 1 || cls.demoplayback || cl.viewentity != 1)
		return;
	player = svs.clients[0].edict;
	if (Cmd_Argc () != 2 || (strcmp (Cmd_Argv (1), "place") && strcmp (Cmd_Argv (1), "restore")))
	{
		Con_SafePrintf ("Usage: vr_mock_wall <place|restore>\n");
		return;
	}
	if (!strcmp (Cmd_Argv (1), "restore"))
	{
		if (!placed) return;
		VectorCopy (saved_origin, player->v.origin);
		player->v.movetype = saved_movetype;
		VectorClear (player->v.velocity);
		SV_LinkEdict (player, false);
		placed = false;
		return;
	}
	if (placed) return;
	VectorClear (angles);
	angles[YAW] = vrg_body_yaw;
	AngleVectors (angles, forward, right, up);
	for (raise = 0; raise <= 96; raise += 32)
	{
		VectorAdd (player->v.origin, player->v.view_ofs, eye);
		eye[2] += raise;
		VectorMA (eye, 4096, forward, end);
		wall = SV_Move (eye, vec3_origin, vec3_origin, end, MOVE_NOMONSTERS, player);
		if (wall.startsolid || wall.allsolid || wall.fraction == 1 || fabs(wall.plane.normal[2]) > 0.1)
			continue;
		for (distance = 40; distance <= 56; distance += 8)
		{
			VectorMA (wall.endpos, -distance, forward, candidate_eye);
			VectorSubtract (candidate_eye, player->v.view_ofs, candidate);
			hull = SV_Move (candidate, player->v.mins, player->v.maxs, candidate, MOVE_NORMAL, player);
			check = SV_Move (candidate_eye, vec3_origin, vec3_origin, end, MOVE_NOMONSTERS, player);
			if (hull.startsolid || hull.allsolid || check.startsolid || check.allsolid || check.fraction == 1)
				continue;
			VectorSubtract (check.endpos, candidate_eye, delta);
			if (VectorLength(delta) < 20 || VectorLength(delta) > 60)
				continue;
			VectorCopy (player->v.origin, saved_origin);
			saved_movetype = player->v.movetype;
			placed = true;
			VectorCopy (candidate, player->v.origin);
			VectorClear (player->v.velocity);
			player->v.movetype = MOVETYPE_NOCLIP;
			SV_LinkEdict (player, false);
			Con_SafePrintf ("HXW_wall_fixture_ready distance %.3f\n", (float)VectorLength(delta));
			return;
		}
	}
	Con_SafePrintf ("HXW_wall_fixture_failed no clear hull/eye 20..60 units from a wall\n");
}
