/* helper routines the SDK compiler's code calls by name (its own runtime library's versions are Xbox-only) */
.data
.globl __fltused
__fltused: .long 0x9875
.text
.globl __chkstk
__chkstk:                       /* eax = bytes; touches each page, lowers esp by eax, returns */
	pushl %ecx
	leal 8(%esp),%ecx
1:	cmpl $0x1000,%eax
	jb 2f
	subl $0x1000,%ecx
	orl $0,(%ecx)
	subl $0x1000,%eax
	jmp 1b
2:	subl %eax,%ecx
	orl $0,(%ecx)
	movl %esp,%eax
	movl %ecx,%esp
	movl (%eax),%ecx
	movl 4(%eax),%eax
	jmp *%eax
.globl __ftol2
__ftol2:                        /* st(0) -> edx:eax, truncated; keeps ecx */
	subl $12,%esp
	fnstcw (%esp)
	movw (%esp),%ax
	orw $0x0C00,%ax
	movw %ax,2(%esp)
	fldcw 2(%esp)
	fistpll 4(%esp)
	fldcw (%esp)
	movl 4(%esp),%eax
	movl 8(%esp),%edx
	addl $12,%esp
	ret
.globl __allshl
__allshl:                       /* edx:eax <<= cl */
	cmpb $64,%cl
	jae 2f
	cmpb $32,%cl
	jae 1f
	shldl %cl,%eax,%edx
	shll %cl,%eax
	ret
1:	movl %eax,%edx
	xorl %eax,%eax
	andb $31,%cl
	shll %cl,%edx
	ret
2:	xorl %eax,%eax
	xorl %edx,%edx
	ret
.globl __aullshr
__aullshr:                      /* edx:eax >>= cl, filling with zeros */
	cmpb $64,%cl
	jae 2f
	cmpb $32,%cl
	jae 1f
	shrdl %cl,%edx,%eax
	shrl %cl,%edx
	ret
1:	movl %edx,%eax
	xorl %edx,%edx
	andb $31,%cl
	shrl %cl,%eax
	ret
2:	xorl %eax,%eax
	xorl %edx,%edx
	ret
.globl __allshr
__allshr:                       /* edx:eax >>= cl, keeping the sign */
	cmpb $64,%cl
	jae 2f
	cmpb $32,%cl
	jae 1f
	shrdl %cl,%edx,%eax
	sarl %cl,%edx
	ret
1:	movl %edx,%eax
	sarl $31,%edx
	andb $31,%cl
	sarl %cl,%eax
	ret
2:	sarl $31,%edx
	movl %edx,%eax
	ret

/* s10l: Spooky Ghost's Increased LODs where the game runs retail's own code (with H2N_UNLOCK; interp.c h2n_lods_retail
 * points the two calls at these). The source's versions of both places are never reached: function_133520 is an
 * unproven draft routed to retail's code, and function_4bcc0 is only called by function_4b5d0, which is routed too, so
 * retail's 0x4b5d0 calls retail's 0x4bcc0. These are the mod's two hooks, at the mod's two places in that code.
 *  - the instanced geometry's fade (retail 0x13366d, in 0x133520): the call to the C runtime's float-to-integer helper
 *    (0x322b0c) is made as the game makes it - the fade off the x87 stack, the answer in edx:eax - and the answer is then
 *    255: fully drawn whenever the frustum test passed the piece (the mod's inst_fade_hook)
 *  - an object's level of detail (retail 0x4be4d, in 0x4bcc0): 0x4c450 (ecx, ebx and xmm4 hold its inputs, and the one
 *    stack argument, which it takes off: ret 4) is called with every register as it came and that argument pushed again;
 *    its answer is then 5, which the game's own cap after the call lowers to the most it allows (the mod's obj_lod_hook) */
.data
.globl _h2n_retail_ftol_at
_h2n_retail_ftol_at: .long 0x00322b0c
.globl _h2n_retail_4c450_at
_h2n_retail_4c450_at: .long 0x0004c450
.text
#ifdef H2N_LODS_TEST
.data
.globl _h2n_t_lods
_h2n_t_lods: .long 0,0,0,0              /* (test builds only) instance fades seen, below 255; objects' levels seen, below 5 */
.text
#endif
.globl _h2n_inst_fade_hook
_h2n_inst_fade_hook:
	call *_h2n_retail_ftol_at
#ifdef H2N_LODS_TEST
	incl _h2n_t_lods
	cmpl $255,%eax
	jge 1f
	incl _h2n_t_lods+4
1:
#endif
	movl $255,%eax
	ret
.globl _h2n_obj_lod_hook
_h2n_obj_lod_hook:
	pushl 4(%esp)                   /* the stack argument again, under this call's return address */
	call *_h2n_retail_4c450_at      /* it takes the copy off (ret 4) */
#ifdef H2N_LODS_TEST
	incl _h2n_t_lods+8
	cmpb $5,%al
	jge 2f
	incl _h2n_t_lods+12
2:
#endif
	movl $5,%eax
	ret $4                          /* and the caller's, as 0x4c450 does */
