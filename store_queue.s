/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <snickerbockers@washemu.org> wrote this file.  As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 *
 * snickerbockers
 * ----------------------------------------------------------------------------
 */

.globl _test_single_addr_sq
.globl _write_sq

.text
	
_write_sq:
	! r4 points to the data to write
	! r5 is the destination address

	! shuffle registers around a bit, I used to have another parameter
	! i got rid of so i have to do this now lol
	mov r5, r6
	mov r4, r5

	! fix up fpscr.  We do 64-bit writes to the SQ
        sts fpscr, r7
	mov #3, r2
	shll8 r2
        shll8 r2
        shll2 r2
        shll r2
        not r2, r2
        and r7, r2
        mov #1, r3
        shll8 r3
        shll8 r3
        shll2 r3
        shll2 r3
        or r3, r2
        lds r2, fpscr

	! storequeue address bits 31:26
	mov #0x38, r0
	shll8 r0
	shll8 r0
	shll8 r0
	shll2 r0

	! store queue selector
	mov r6, r4
	mov #0x20, r1
	and r1, r4

	or r4, r0
	mov r0, r2 ! r2 will be the destination address
	! or #0x20, r0 ! r0 is our SQ write address

	fmov @r5+, dr0
	fmov @r5+, dr2
	fmov @r5+, dr4
	fmov @r5+, dr6

	mov #0, r1
	fmov dr0, @(r0, r1)
	add #8, r1
	fmov dr2, @(r0, r1)
	add #8, r1
	fmov dr4, @(r0, r1)
	add #8, r1
	fmov dr6, @(r0, r1)

	! get qacr value
	mov r6, r0
	shlr8 r0
	shlr8 r0
	shlr8 r0

	and #0x1c, r0

	! make r5 point to either qacr0 or qacr1
	mov.l addr_qaccr0, r5
	shlr2 r4
	shlr r4
	add r4, r5

	mov.l r0, @r5

	! clear upper bits in pref addr
	shll2 r6
	shll2 r6
	shll2 r6
	shlr2 r6
	shlr2 r6
	shlr2 r6

	! clear lower bits in pref addr
	shlr2 r6
	shll2 r6

	! assemble the final address
	or r2, r6

	! do it!
	pref @r6

	! return
	rts
	lds r7, fpscr

	.align 4
addr_qaccr0:
	.long 0xff000038
