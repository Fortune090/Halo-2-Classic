/* v15list.S: room for the list of places where this program, linked for Halo 2's 1.5 title update, differs from itself
   linked for 1.0 (scripts/v15list.py fills it in after the two links; main.c's v15_apply reads it). Empty: 1.0 only. */
.section .h2nv15,"dr"
.globl _h2n_v15_list
.balign 4
_h2n_v15_list:
.space H2N_V15_LIST_SIZE
