/* The retail image's address range, 0x11000-0x586000, kept free inside our own exe (which is linked at 0x10000,
   as the Xbox links its games). Must be the first object on the link line. */
	.text
	.globl _h2n_retail_range
_h2n_retail_range:
	.space 0x575000
	.globl _h2n_retail_range_end
_h2n_retail_range_end:
