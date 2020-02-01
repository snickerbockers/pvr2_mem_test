!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!
! "THE BEER-WARE LICENSE" (Revision 42):
! <snickerbockers@washemu.org> wrote this file.  As long as you retain this
! notice you can do whatever you want with this stuff. If we meet some day,
! and you think this stuff is worth it, you can buy me a beer in return.
!
! snickerbockers
!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	.globl irq_entry
	.globl vecbase
	.globl irq_count
	.globl wtf_istnrm

.align 2
vecbase:
	.space 0x5fc
	rte
	nop
irq_entry:
	mov.l addr_istnrm, r0
	mov.l @r0, r1
	mov #1, r2
	shll8 r2
	shll8 r2
	shll2 r2
	shll r2
	mov.l r2, @r0

	! read from ISTNRM twice more
	!
	! when we wrote to ISTNRM, that successfully cleared bit 19.
	! However, the interrupt is still being generated until we write to it
	! or read from it again.  This appears to be a bug on real hardware.
	!
	! Since the initial write to ISTNRM did successfully clear the bit, it
	! would be safe to omit the second write and instead rely upon the
	! filtering code below to ignore the second IRQ.  I'd expect that
	! most/all programs do this since Dreamcast multiplexes most interrupts
	! together onto a single line, and that is why I've never heard of
	! anybody else getting snagged on this behavior.
	mov.l @r0, r3
	mov.l @r0, r3

	! IRQ filtering code, we would need this if there was more than one IRQ
	! unmasked in hollywood, or if we hadn't written to ISTNRM twice as
	! described above.
	! tst r2, r1
	! bt irq_filtered

	mov.l addr_irq_count, r0
	mov.l @r0, r1
	add #1, r1
	bra kill_me_now
	mov.l r1, @r0

irq_filtered:
	! old debug code I no longer need
	! mov.l addr_wtf_istnrm, r0
	! mov.l addr_intevt, r1
	! mov.l @r1, r1
	! mov.l r1, @r0

kill_me_now:
	rte
	nop

.align 4
addr_irq_count:
	.long irq_count
irq_count:
	.long 0
addr_istnrm:
	.long 0xa05f6900
