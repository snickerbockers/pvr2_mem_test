################################################################################
#
# "THE BEER-WARE LICENSE" (Revision 42):
# <snickerbockers@washemu.org> wrote this file.  As long as you retain this
# notice you can do whatever you want with this stuff. If we meet some day,
# and you think this stuff is worth it, you can buy me a beer in return.
#
# snickerbockers
#
################################################################################

AS=sh4-elf-as
LD=sh4-elf-ld
CC=sh4-elf-gcc
OBJCOPY=sh4-elf-objcopy
SCRAMBLE=scramble
MAKEIP=makeip
CDI4DC=cdi4dc

all: pvr2_mem_test.cdi

clean:
	rm -rf init.o *.elf *.bin *.o *.iso *.cdi isodir

init.o: init.s
	$(AS) -little -o init.o init.s

irq.o: irq.s
	$(AS) -little -o irq.o irq.s

float_tests.o: float_tests.s
	$(AS) -little -o float_tests.o float_tests.s

store_queue.o: store_queue.s
	$(AS) -little -o store_queue.o store_queue.s

pvr2_mem_test.elf: init.o float_tests.o main.o store_queue.o dma_tests.o irq.o
	$(CC) -Wl,-e_start,-Ttext,0x8c010000 init.o float_tests.o store_queue.o main.o dma_tests.o irq.o -o pvr2_mem_test.elf -nostartfiles -nostdlib -lgcc -m4

pvr2_mem_test.bin.unscrambled: pvr2_mem_test.elf
	$(OBJCOPY) -O binary -j .text -j .data -j .bss -j .rodata  --set-section-flags .bss=alloc,load,contents pvr2_mem_test.elf $@

isodir/1st_read.bin: pvr2_mem_test.bin.unscrambled
	mkdir -p isodir
	$(SCRAMBLE) $< $@

filesystem.iso: isodir/1st_read.bin IP.BIN $(shell find isodir)
	mkisofs -G IP.BIN -C 0,11702 -o $@ isodir

main.o: main.c pvr2_mem_test.h
	$(CC) -c main.c -nostartfiles -nostdlib

dma_tests.o: dma_tests.c pvr2_mem_test.h
	$(CC) -c dma_tests.c -nostartfiles -nostdlib

IP.BIN: ip.txt IP.TMPL
	$(MAKEIP) ./ip.txt ./IP.BIN

pvr2_mem_test.cdi: filesystem.iso
	$(CDI4DC) $< $@

run: pvr2_mem_test.cdi
	washingtondc -c test -- $<
