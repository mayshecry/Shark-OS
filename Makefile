# You can change this to 'i686-elf-' if you install the cross-compiler
PREFIX = 
CC = gcc
AS = as
OBJCOPY = $(PREFIX)objcopy

# -mno-80387 -mno-mmx -mno-sse -mno-sse2: Prevents the compiler from using registers that are disabled at boot.
# This is the most common cause of "Triple Faults" in VirtualBox.
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -mno-80387 -mno-mmx -mno-sse -mno-sse2
ASFLAGS = --32
# -no-pie prevents the linker from trying to create a PIE executable
LDFLAGS = -m32 -ffreestanding -O2 -nostdlib -lgcc -no-pie -Wl,-m,elf_i386

all: sharkos.iso

boot.o: boot.s
	$(AS) $(ASFLAGS) boot.s -o boot.o

sharkscript_bin.o: sharkscript
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 sharkscript sharkscript_bin.o

kernel.o: kernel.c
	$(CC) -c kernel.c -o kernel.o $(CFLAGS)

sharkos.bin: boot.o kernel.o sharkscript_bin.o linker.ld
	$(CC) -T linker.ld -o sharkos.bin $(LDFLAGS) boot.o kernel.o sharkscript_bin.o

sharkos.iso: sharkos.bin grub.cfg
	mkdir -p isodir/boot/grub
	cp sharkos.bin isodir/boot/sharkos.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o sharkos.iso isodir

clean:
	rm -rf isodir
	rm -f *.o sharkos.bin sharkos.iso

.PHONY: all clean