/*
 * vr_glue_server.c -- engine-side glue for the hxvr VR module, server side: reads the VR move block a VR
 * client sends to its local server (clc_hxvr) and keeps its room-scale move for the next physics tick
 * (sv_phys.c: SV_HXVR_RoomscaleMove).
 * C, compiled into the engine; includes engine headers and vr_api.h only.
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"

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
	if (b.flags & VR_MOVE_ROOMSCALE)
	{
		cl->hxvr_move[0] += b.roomscale[0];
		cl->hxvr_move[1] += b.roomscale[1];
	}
}
