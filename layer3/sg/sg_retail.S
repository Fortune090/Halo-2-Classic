/* H2Classic: the Spooky Ghost Menu's assembly.
 *
 * 1. guarded_call - the mod's own (hooks.S), with Windows' CONTEXT offsets in place of the console's.
 * 2. The mod's hook routines for calls made by the XBE's OWN code. Most of the game runs here as compiled source, and
 *    there a hook is a C function with the hooked function's declared arguments (sg_hooks.c). But some functions run as
 *    the XBE's code in place (the routes; in 1.5, what the update changed), and code reached from them calls the XBE's
 *    functions directly, with the registers its link-time code uses. For those call sites the console build's routines
 *    are used as they are (hooks.S, v2.0.40): the same bytes patched at the same call (sg_hooks.c checks each is that
 *    call first), the same C function called. orig_* is the XBE's function at its own address for the game version in
 *    use, reached through a pointer sg_hooks.c fills.
 */
	.text
#define ORIG(name) .globl _sg_orig_##name; orig_##name: jmp *_sg_orig_##name
ORIG(inf_ammo_fn)
ORIG(fire)
ORIG(damage)
ORIG(region_dmg)
ORIG(main_save)
ORIG(death_revert)
ORIG(jump_logic)
ORIG(fall_check)
ORIG(fp_marker)
ORIG(perspective)
ORIG(cam_mode)
ORIG(cam_target)
ORIG(tvt)
ORIG(fp_gather)

	.globl	_guarded_call		/* s32 guarded_call(void (*fn)(void)): 1 if a fault was caught */
_guarded_call:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	%esp, %ebp
	subl	$4, %esp
	fnstcw	(%esp)			/* [ebp-4]: the x87 control word */
	pushl	$gc_resume		/* record +12 [ebp-8]: where a fault resumes */
	pushl	%ebp			/* record +8 [ebp-12]: the frame */
	pushl	$gc_handler		/* record +4 [ebp-16] */
	pushl	%fs:0			/* record +0 [ebp-20]: the next record */
	movl	%esp, %fs:0
	call	*20(%ebp)		/* fn */
	xorl	%eax, %eax
	jmp	1f
gc_resume:
	fninit
	fldcw	-4(%ebp)
	cld
	call	_sg_guard_note		/* (sg_hooks.c: the first few are written to the log) */
	movl	$1, %eax
1:	movl	-20(%ebp), %ecx
	movl	%ecx, %fs:0
	movl	%ebp, %esp
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
gc_handler:				/* (exception record, our record, context, dispatcher context) */
	movl	4(%esp), %eax
	testl	$6, 4(%eax)		/* unwinding: not ours to resume */
	jnz	2f
	movl	8(%esp), %edx		/* our record */
	movl	12(%esp), %ecx		/* the context (Windows x86: Ebp 0xb4, Eip 0xb8, Esp 0xc4) */
	movl	12(%edx), %eax
	movl	%eax, 0xb8(%ecx)	/* Eip: gc_resume */
	movl	%edx, 0xc4(%ecx)	/* Esp: the record (everything needed above it) */
	movl	8(%edx), %eax
	movl	%eax, 0xb4(%ecx)	/* Ebp: the frame */
	incl	_sg_guard_faults
	movl	4(%esp), %eax		/* the exception record: where (+0x0c) and the address touched (+0x18) - for the log's first few */
	movl	0x0c(%eax), %edx
	movl	%edx, _sg_guard_where
	movl	0x18(%eax), %edx
	movl	%edx, _sg_guard_touched
	xorl	%eax, %eax		/* ExceptionContinueExecution */
	ret
2:	movl	$1, %eax		/* ExceptionContinueSearch */
	ret

/* ---- the console build's routines, as they are (hooks.S) ---- */
	.globl	_sg_r_ia_hook		/* replaces `call 0x106030` (eax the weapon; al the answer) */
_sg_r_ia_hook:
	pushfl
	pushal
	cld
	pushl	%eax			/* the weapon */
	call	_ia_answer
	addl	$4, %esp
	cmpl	$2, %eax
	je	1f
	movl	%eax, 28(%esp)		/* the answer, into pushal's eax */
	popal
	popfl
	ret
1:	popal
	popfl
	jmp	orig_inf_ammo_fn

	.globl	_sg_r_bc_fire_hook	/* replaces `call 0x102fb0` (stdcall: the weapon, the barrel) */
_sg_r_bc_fire_hook:
	pushal
	pushfl
	cld
	pushl	40(%esp)		/* the weapon: above pushal (32), pushfl (4), return (4) */
	call	_bc_before
	addl	$4, %esp
	popfl
	popal
	pushl	8(%esp)			/* the barrel */
	pushl	8(%esp)			/* the weapon (one push lower now) */
	call	orig_fire		/* stdcall: takes its two arguments off */
	pushal
	pushfl
	cld
	call	_bc_after
	popfl
	popal
	ret	$8

	.globl	_sg_r_ohk_hook		/* replaces every `call 0xd7b80` (stdcall, six stack arguments) */
_sg_r_ohk_hook:
	pushal
	pushfl
	cld
	pushl	44(%esp)                        /* the object hit ([esp+8] at entry) */
	pushl	44(%esp)                        /* the event ([esp+4] at entry) */
	call	_ohk_before
	addl	$8, %esp
	popfl
	popal
	pushl	24(%esp)                        /* the six arguments again, last first (each push moves the next into place) */
	pushl	24(%esp)
	pushl	24(%esp)
	pushl	24(%esp)
	pushl	24(%esp)
	pushl	24(%esp)
	call	orig_damage
	pushal
	pushfl
	cld
	call	_ohk_after
	popfl
	popal
	ret	$0x18

	.globl	_sg_r_rdmg_hook		/* replaces the calls to the region damage (0xdaaa0; six stack arguments) */
_sg_r_rdmg_hook:
	pushl	8(%esp)			/* the object (its 2nd argument) */
	call	_rdmg_skip
	addl	$4, %esp
	testl	%eax, %eax
	jnz	1f
	jmp	orig_region_dmg		/* the game's own, as called */
1:	ret	$0x18			/* skipped: its own six arguments popped, as it does */

	.globl	_sg_r_hsave_hook	/* the game's checkpoint logic asking for a save (0x12b790): a call and a tail jump */
_sg_r_hsave_hook:
	pushal
	pushfl
	cld
	call	_natural_save_check	/* g_cp_skip: Disable natural checkpoints */
	popfl
	popal
	cmpl	$0, _g_cp_skip
	jne	1f
	jmp	orig_main_save
1:	ret				/* skipped: back as the save would have come back */

	.globl	_sg_r_hrevert_hook	/* the once-a-tick call that reverts you after you die (0x138e40, no arguments) */
_sg_r_hrevert_hook:
	pushal
	pushfl
	cld
	call	_death_revert_before
	popfl
	popal
	call	orig_death_revert
	pushal
	pushfl
	cld
	call	_death_revert_after	/* a revert it raised, with Disable reverts on: lowered */
	popfl
	popal
	ret

	.globl	_sg_r_acro_jump_hook	/* replaces `call 0xe4680` (edi the biped; al the answer) */
_sg_r_acro_jump_hook:
	cmpl	$0, _o_acro
	jne	1f
	jmp	orig_jump_logic
1:	pushal
	pushfl
	cld
	pushl	%edi
	call	_acro_before
	addl	$4, %esp
	popfl
	popal
	call	orig_jump_logic
	pushal
	pushfl
	cld
	pushl	%edi
	call	_acro_after
	addl	$4, %esp
	popfl
	popal				/* (eax: the game's answer, as it returned it) */
	ret

	.globl	_sg_r_acro_fall_hook	/* replaces `call 0xe3700` (eax the biped) */
_sg_r_acro_fall_hook:
	cmpl	$0, _o_acro
	je	1f
	pushal
	pushfl
	cld
	pushl	%eax
	call	_acro_player_biped
	addl	$4, %esp
	movl	%eax, g_acro_gate
	popfl
	popal
	cmpl	$0, g_acro_gate
	jne	2f
1:	jmp	orig_fall_check
2:	ret

	.globl	_sg_r_fpm_hook		/* replaces `call 0x16640f` (ecx the object; four stack arguments; al the answer) */
_sg_r_fpm_hook:
	pushfl
	pushal
	cld
	pushl	%ecx			/* the object */
	call	_fpm_answer
	addl	$4, %esp
	cmpl	$2, %eax
	je	1f
	movl	%eax, 28(%esp)		/* the answer, into pushal's eax */
	popal
	popfl
	ret	$0x10
1:	popal
	popfl
	jmp	orig_fp_marker

	.globl	_sg_r_persp_hook	/* replaces `call 0x155760` (eax the player; eax the answer) */
_sg_r_persp_hook:
	pushfl
	pushal
	cld
	pushl	%eax			/* the player */
	call	_persp_answer
	addl	$4, %esp
	cmpl	$-1, %eax
	je	1f
	movl	%eax, 28(%esp)		/* the answer, into pushal's eax */
	popal
	popfl
	ret
1:	popal
	popfl
	jmp	orig_perspective

	.globl	_sg_r_hcam_mode_hook	/* replaces `call 0x155810` (ebx the unit; its word on the stack) */
_sg_r_hcam_mode_hook:
	pushl	4(%esp)			/* its word */
	call	orig_cam_mode		/* stdcall: takes it off; ebx as given */
	pushal
	pushfl
	cld
	pushl	40(%esp)		/* the word: above pushfl (4), pushal (32), return (4) */
	pushl	%ebx			/* the unit */
	pushl	%eax			/* the game's answer */
	call	_cam_mode_answer
	addl	$12, %esp
	movl	%eax, 32(%esp)		/* ours, in the saved eax */
	popfl
	popal
	ret	$4

	.globl	_sg_r_htarget_hook	/* replaces `call 0x1871e0` (eax the player, esi its record) */
_sg_r_htarget_hook:
	call	orig_cam_target		/* eax, esi as given */
	pushal
	pushfl
	cld
	pushl	%esi
	call	_cam_target_fix		/* a possessed vehicle: its driver seat's camera */
	addl	$4, %esp
	popfl
	popal
	ret

	.globl	_sg_r_tvt_hook		/* replaces the scripts' `call 0x11c470` (eax the volume, edi the point; al: inside) */
_sg_r_tvt_hook:
	pushl	%eax			/* the volume */
	call	orig_tvt
	pushfl
	pushal
	movl	36(%esp), %ecx		/* the volume pushed above: past pushal (32) and pushfl (4) */
	pushl	%eax			/* the answer */
	pushl	%edi			/* the point */
	pushl	%ecx
	cld
	call	_tvt_note
	addl	$12, %esp
	popal
	popfl
	leal	4(%esp), %esp		/* the saved volume dropped, the flags untouched */
	ret

	.globl	_sg_r_fp_gather_hook	/* replaces `call 0x261ec0` (stdcall, six) */
_sg_r_fp_gather_hook:
	pushl	24(%esp)			/* the six, as given: each push moves the next one up to 24 */
	pushl	24(%esp)
	pushl	24(%esp)
	pushl	24(%esp)
	pushl	24(%esp)
	pushl	24(%esp)
	call	orig_fp_gather			/* (takes them off itself) */
	pushfl
	pushal
	cld
	pushl	52(%esp)			/* count: 4 (return) + 36 (saved) + 12 up; each push moves the next one to 52 */
	pushl	52(%esp)			/* entries */
	pushl	52(%esp)			/* squad */
	pushl	52(%esp)			/* actor */
	call	_fw_fp_filter
	addl	$16, %esp
	popal
	popfl
	ret	$0x18

	.data
	.balign	4
g_acro_gate: .long 0
	.globl	_sg_guard_faults
_sg_guard_faults: .long 0
	.globl	_sg_guard_where, _sg_guard_touched
_sg_guard_where: .long 0
_sg_guard_touched: .long 0
_sg_orig_inf_ammo_fn: .long 0
_sg_orig_fire: .long 0
_sg_orig_damage: .long 0
_sg_orig_region_dmg: .long 0
_sg_orig_main_save: .long 0
_sg_orig_death_revert: .long 0
_sg_orig_jump_logic: .long 0
_sg_orig_fall_check: .long 0
_sg_orig_fp_marker: .long 0
_sg_orig_perspective: .long 0
_sg_orig_cam_mode: .long 0
_sg_orig_cam_target: .long 0
_sg_orig_tvt: .long 0
_sg_orig_fp_gather: .long 0
