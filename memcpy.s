    .syntax unified
    .align 4

.global memcpy
memcpy:
	push    {r0}
	cmp     r2, #4
	it      lo
	lslslo  r2, r2, #30         /* Adjust r2 for less_than_4_bytes */
	blo     less_than_4_bytes
	
	ands    r3, r1, #3
	beq     aligned
	
	negs    r3, r3              /* Next aligned offset = (4 - src & 3) & 3 */
	lsls    r3, r3, #31
	ittt    cs
	ldrhcs  r3, [r1], #2		/* Load if 2 bytes unaligned */
	subcs   r2, r2, #2
	strhcs  r3, [r0], #2		/* Save if 2 bytes unaligned */
	ittt    mi
	ldrbmi  r3, [r1] ,#1		/* Load if 1 byte unaligned */
	submi   r2, r2, #1
	strbmi  r3, [r0] ,#1		/* Save if 1 byte unaligned */

aligned:
	push    {r4 - r10}
	subs 	r2, #32		 
	blo     less_than_32_bytes
L:
	ldmia 	r1!, {r3 - r10}
	subs 	r2, #32		 
	stmia	r0!, {r3 - r10}
	bhs 	L
					
less_than_32_bytes:
	lsls    r2, r2, #28
	it      cs
	ldmiacs	r1!, {r3 - r6}		/* Load if 16 bytes remained */
	it      mi
	ldmiami r1!, {r7 - r8}		/* Load if 8 bytes remained */
	it      cs
	stmiacs	r0!, {r3 - r6}
	it      mi
	stmiami r0!, {r7 - r8}
	
	lsls    r2, r2, #2
	itt     cs
	ldrcs   r3, [r1], #4		/* Load if 4 bytes remained */
	strcs   r3, [r0], #4
	
	pop     {r4 - r10}
	
less_than_4_bytes:
	it      ne
	ldrne   r3, [r1]    		/* Load if ether 2 bytes or 1 byte remained */
	lsls    r2, r2, #1
	itt     cs
	strhcs  r3, [r0],#2			/* Save if 2 bytes remained */
	lsrcs   r3, r3, 16
	it      mi
	strbmi  r3, [r0],#1			/* Save if 1 byte remained */
	
	pop     {r0}				
	bx      lr				
