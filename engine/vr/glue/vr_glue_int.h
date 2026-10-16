/*
 * vr_glue_int.h -- state and functions shared between the glue's own .c files (vr_glue.c,
 * vr_glue_input.c). Not for engine code: the engine includes vr_glue.h only.
 * Include after quakedef.h, vr_api.h and vr_glue.h.
 */
#ifndef HXVR_VR_GLUE_INT_H
#define HXVR_VR_GLUE_INT_H

/* ---- VR session state (vr_glue.c) ---- */

extern vr_frame_t	vrg_frame;		/* flags of the last VR_BeginFrame */
extern qboolean		vrg_active;		/* running && last frame had render != 0 */

/* body yaw (degrees; not normalized: after VRG_PostInput it is in [0, 360) via anglemod, every
 * consumer normalizes) and the recenter anchor (tracking space, x/z used): the view yaw is
 * body_yaw + head yaw. Keyboard/mouse turn the body; the head adds its own rotation. */
extern float		vrg_body_yaw;
extern qboolean		vrg_body_valid;		/* body_yaw taken from cl.viewangles since VR started */
extern vr_vec3_t	vrg_anchor;

/* vr_eye_height (metres) and vr_world_scale in use (VRG_CheckScaleCvars): the last valid values */
extern float		vrg_eye_height, vrg_world_scale;

/* ---- controller input and mock dev commands (vr_glue_input.c) ---- */

void VRG_InputInit (void);	/* VRG_Init: registers vr_mainhand, vr_bind_defaults, vr_mock_hand, vr_mock_head */
void VRG_InputStart (void);	/* VRG_Start, after VR_Start succeeded: forgets all button state, resets the
				 * mock hands, sets the default bindings if no VR key is bound */
/* VRG_HostFrameBegin, after VR_BeginFrame (VR running): turns button edges of both hands into
 * Key_Event calls routed by key_dest, and stick directions into menu/console arrows. */
void VRG_InputFrame (void);
/* Releases every key a controller button holds down (Key_Event(code, false) with the code it was
 * pressed with). VRG_Stop, and VRG_InputFrame when vr_mainhand changed. */
void VRG_ReleaseAllKeys (void);
/* true while VRG_ModalFrame runs its frame: VRG_InputFrame then maps the buttons to the dialog's
 * 'y' / 'n' instead of the key_dest routing, and skips the menu stick arrows */
extern qboolean		vrg_input_modal;

#endif /* HXVR_VR_GLUE_INT_H */
