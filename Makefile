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

AS=sh4-linux-gnu-as
LD=sh4-linux-gnu-ld
CC=sh4-linux-gnu-gcc
OBJCOPY=sh4-linux-gnu-objcopy

all: pvr2_mem_test.bin

clean:
	rm -f init.o float_tests.o pvr2_mem_test.elf pvr2_mem_test.bin main.o

init.o: init.s
	$(AS) -little -o init.o init.s

float_tests.o: float_tests.s
	$(AS) -little -o float_tests.o float_tests.s

pvr2_mem_test.elf: init.o float_tests.o main.o
	$(CC) -Wl,-e_start,-Ttext,0x8c010000 init.o float_tests.o main.o -o pvr2_mem_test.elf -nostartfiles -nostdlib -lgcc -m4

pvr2_mem_test.bin: pvr2_mem_test.elf
	$(OBJCOPY) -O binary -j .text -j .data -j .bss -j .rodata  --set-section-flags .bss=alloc,load,contents pvr2_mem_test.elf pvr2_mem_test.bin

main.o: main.c
	$(CC) -c main.c -nostartfiles -nostdlib -Os
