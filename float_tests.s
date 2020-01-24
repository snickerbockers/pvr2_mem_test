!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!
! "THE BEER-WARE LICENSE" (Revision 42):
! <snickerbockers@washemu.org> wrote this file.  As long as you retain this
! notice you can do whatever you want with this stuff. If we meet some day,
! and you think this stuff is worth it, you can buy me a beer in return.
!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

.globl test_single_addr_double

.text

test_single_addr_double:
	mov.l r8, @-r15
	mov.l r9, @-r15
	mov.l r10, @-r15
	mov.l r11, @-r15
	mov.l r12, @-r15
	mov.l r13, @-r15
	mov.l r14, @-r15
	sts.l pr, @-r15
	sts fpscr, r10
	mov.l r10, @-r15
	mov.l addr_get_all_banks_from32, r8

	mov r4, r9
	add #4, r9

	! call get_all_banks_from32 for the first dword
	mov r4, r5
	add #-32, r15
	mov r15, r13
	jsr @r8
	mov r15, r4

	add #-32, r15
	mov r15, r14

	! fix up fpscr
	mov #3, r2
	shll8 r2
        shll8 r2
        shll2 r2
        shll r2
        not r2, r2
        and r10, r2
        mov #1, r3
        shll8 r3
        shll8 r3
        shll2 r3
        shll2 r3
        or r3, r2
        lds r2, fpscr

	! r8 is the bank number
	xor r8, r8

foreach_bank_even:

	! r0 = bank_no * sizeof(double*)
	mov r8, r0
	shll2 r0

	! r11=testvals
	mov.l addr_double_testvals, r11

	! write testvals[bank_no] to ptrs[bank_no]
	mov.l @(r0, r13), r5
	mov r5, r9
	shll r0
	mov r0, r4
	bsr do_quad_read
	add r11, r4

	bsr do_quad_write
	mov r9, r4

	! now we need to get the pointer to the second 4 bytes for all 8 banks.
	! We derive this from the address we just wrote to.
	! we need to call either addr_get_all_banks_from32 or
	! addr_get_all_banks_from64 depending on whether that was 32-bit or
	! 64-bit access.
	mov r8, r0
	mov r9, r5
	shlr r0
	tst #1, r0
	bf/s get_the_32bit_banks
	add #4, r5

	! this testcase makes me want to kill myself
	mov.l addr_get_all_banks_from64, r0
	jsr @r0
	mov r14, r4

	bra please_kill_me_now
	nop

get_the_32bit_banks:
	mov.l addr_get_all_banks_from32, r0
	jsr @r0
	mov r14, r4

please_kill_me_now:

	! clear bit 0 of bank_no
        mov r8, r0
        and #0xfe, r0
        shll2 r0
        shll r0

	! load testvals[bank_no & ~1] into r11-r12
	add r11, r0
	mov.l @r0+, r12
	mov.l @r0, r11

	xor r9, r9
	mov #0xff, r10

	! if (*ptrs[0] != testvals[bank_no & ~1])
	mov r9, r4
	add r13, r4
	mov r9, r5
	add r14, r5
	mov.l @r4, r4
	bsr do_quad_read_scatter
	mov.l @r5, r5
	flds fr0, fpul
	sts fpul, r6
	cmp/eq r12, r6
	bf test_single_addr_double_on_fail
	flds fr1, fpul
	sts fpul, r6
	cmp/eq r11, r6
	bf test_single_addr_double_on_fail
	add #4, r9

	! if (*ptrs[1] != 0xffffffff)
	mov r9, r4
	add r13, r4
	mov r9, r5
	add r14, r5
	mov.l @r4, r4
	bsr do_quad_read_scatter
	mov.l @r5, r5
	flds fr0, fpul
	sts fpul, r6
	cmp/eq r10, r6
	bf test_single_addr_double_on_fail
	flds fr1, fpul
	sts fpul, r6
	cmp/eq r10, r6
	bf test_single_addr_double_on_fail
	add #4, r9

	! this is 32-bit
	! if (*ptrs[2] != testvals[bank_no & ~1])
	mov r9, r4
	add r13, r4
	mov r9, r5
	add r14, r5
	mov.l @r4, r4
	bsr do_quad_read_scatter
	mov.l @r5, r5
	flds fr0, fpul
	sts fpul, r6
	cmp/eq r12, r6
	bf test_single_addr_double_on_fail
	flds fr1, fpul
	sts fpul, r6
	cmp/eq r11, r6
	bf test_single_addr_double_on_fail
	add #4, r9

	! this is 32-bit
	! if (*ptrs[3] != 0xffffffff)
	mov r9, r4
	add r13, r4
	mov r9, r5
	add r14, r5
	mov.l @r4, r4
	bsr do_quad_read_scatter
	mov.l @r5, r5
	flds fr0, fpul
	sts fpul, r6
	cmp/eq r10, r6
	bf test_single_addr_double_on_fail
	flds fr1, fpul
	sts fpul, r6
	cmp/eq r10, r6
	bf test_single_addr_double_on_fail
	add #4, r9

	! if (*ptrs[4] != testvals[bank_no & ~1])
	mov r9, r4
	add r13, r4
	mov r9, r5
	add r14, r5
	mov.l @r4, r4
	bsr do_quad_read_scatter
	mov.l @r5, r5
	flds fr0, fpul
	sts fpul, r6
	cmp/eq r12, r6
	bf test_single_addr_double_on_fail
	flds fr1, fpul
	sts fpul, r6
	cmp/eq r11, r6
	bf test_single_addr_double_on_fail
	add #4, r9

	! if (*ptrs[5] != 0xffffffff)
	mov r9, r4
	add r13, r4
	mov r9, r5
	add r14, r5
	mov.l @r4, r4
	bsr do_quad_read_scatter
	mov.l @r5, r5
	flds fr0, fpul
	sts fpul, r6
	cmp/eq r10, r6
	bf test_single_addr_double_on_fail
	flds fr1, fpul
	sts fpul, r6
	cmp/eq r10, r6
	bf test_single_addr_double_on_fail
	add #4, r9

	! this is 32-bit
	! if (*ptrs[6] != testvals[bank_no & ~1])
	mov r9, r4
	add r13, r4
	mov r9, r5
	add r14, r5
	mov.l @r4, r4
	bsr do_quad_read_scatter
	mov.l @r5, r5
	flds fr0, fpul
	sts fpul, r6
	cmp/eq r12, r6
	bf test_single_addr_double_on_fail
	flds fr1, fpul
	sts fpul, r6
	cmp/eq r11, r6
	bf test_single_addr_double_on_fail
	add #4, r9

	! this is 32-bit
	! if (*ptrs[7] != 0xffffffff)
	mov r9, r4
	add r13, r4
	mov r9, r5
	add r14, r5
	mov.l @r4, r4
	bsr do_quad_read_scatter
	mov.l @r5, r5
	flds fr0, fpul
	sts fpul, r6
	cmp/eq r10, r6
	bf test_single_addr_double_on_fail
	flds fr1, fpul
	sts fpul, r6
	cmp/eq r10, r6
	bf test_single_addr_double_on_fail
	add #4, r9

	add #1, r8
	mov #8, r0
	cmp/eq r0, r8
	bf foreach_bank_even

	bra test_single_addr_double_done
	xor r0, r0

test_single_addr_double_on_fail:
	bra test_single_addr_double_done
	mov #1, r0

test_single_addr_double_done:

	add #64, r15
	lds.l @r15+, fpscr
	lds.l @r15+, pr
	mov.l @r15+, r14
	mov.l @r15+, r13
	mov.l @r15+, r12
	mov.l @r15+, r11
	mov.l @r15+, r10
	mov.l @r15+, r9
	rts
	mov.l @r15+, r8


! read a double from two different addresses each pointing to one dword
do_quad_read_scatter:
	! addr of first four bytes goes in r4
	! addr of second four bytes goes in r5
	! return is in dr0
	mov #4, r0
	tst r0, r4
	bf do_quad_read_scatter_first_dword_odd

	fmov @r4, dr2
	flds fr2, fpul
	bra do_quad_read_scatter_second_dword
	fsts fpul, fr0

do_quad_read_scatter_first_dword_odd:
	dt r4
	dt r4
	dt r4
	dt r4
	fmov @r4, dr2
	flds fr3, fpul
	fsts fpul, fr0
	
do_quad_read_scatter_second_dword:
	tst r0, r5
	bf do_quad_read_scatter_second_dword_odd

	fmov @r5, dr2
	flds fr2, fpul
	rts
	fsts fpul, fr1

do_quad_read_scatter_second_dword_odd:
	dt r5
	dt r5
	dt r5
	dt r5
	fmov @r5, dr2
	flds fr3, fpul
	rts
	fsts fpul, fr1


	! do really inconvenient non-aligned quad-reads using quad-read instructions
	! obviously the best way to do this would be with two separate mov.l instructions,
	! but then we wouldn't be testing quad reads
	!
	! only dr2 will be clobbered.
do_quad_read:
	! addr goes in r4
	! return is in dr0
	mov r4, r0
	tst #4, r0
	bf do_quad_read_odd

	rts
	fmov @r4, dr0

do_quad_read_odd:
	mov r15, r0
	tst #4, r0
	bt do_quad_read_with_aligned_stack
	add #-4, r15

do_quad_read_with_aligned_stack:
	dt r4
	dt r4
	dt r4
	dt r4
	fmov @r4+, dr0
	fmov @r4, dr2
	flds fr2, fpul
	sts.l fpul, @-r15
	flds fr1, fpul
	sts.l fpul, @-r15
	fmov @r15, dr0
	rts
	mov r0, r15

	! do really inconvenient non-aligned quad-writes using quad-write instructions
	! obviously the best way to do this would be with two separate mov.l instructions,
	! but then we wouldn't be testing quad writes
	!
	! only dr2 will be clobbered.
do_quad_write:
	! addr goes in r4
	! value goes in dr0
	! mov.l r0, @-r15
	mov r4, r0
	tst #4, r0
	bf do_quad_write_odd

	rts
	fmov dr0, @r4

do_quad_write_odd:
	dt r4
	dt r4
	dt r4
	dt r4
	fmov @r4, dr2
	flds fr0, fpul
	fsts fpul, fr3
	fmov dr2, @r4
	add #8, r4

	fmov @r4, dr2
	flds fr1, fpul
	fsts fpul, fr2
	rts
	fmov dr2, @r4

	! rts
	! mov.l @r15+, r0

.align 4
addr_get_all_banks_from32:
	.long get_all_banks_from32
addr_get_all_banks_from64:
	.long get_all_banks_from64
addr_double_testvals:
	.long double_testvals

double_testvals:
	.long 0xdeadbeef
	.long 0xbabeface
	.long 0xdeadbabe
	.long 0xbeefb00b
	.long 0xb16b00b5
	.long 0xfeedbabe
	.long 0xfeedface
	.long 0xaaaa5555
	.long 0xdeadc0de
	.long 0x5555aaaa
	.long 0xaaaa5555
	.long 0xbabeb00b
	.long 0x5555aaaa
	.long 0xb16babe5
	.long 0x12345678
	.long 0x90abcdef

mem_bank_sz:
	.long (4<<20)
