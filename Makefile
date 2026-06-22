# You can change this to 'i686-elf-' if you install the cross-compiler
PREFIX = 
CC = gcc
AS = as
OBJCOPY = $(PREFIX)objcopy

# -mno-80387 -mno-mmx -mno-sse -mno-sse2: Prevents the compiler from using registers that are disabled at boot.
# This is the most common cause of "Triple Faults" in VirtualBox.
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
         -fomit-frame-pointer -fmerge-all-constants -Iinclude
ASFLAGS = --32
LDFLAGS = -m32 -ffreestanding -O2 -nostdlib -lgcc -no-pie -Wl,-m,elf_i386 -Wl,-gc-sections

DIRS = arch drivers fs ui shell lib sharkscript

all: sharkos.iso

$(DIRS):
	mkdir -p $@

boot.o: boot.s
	$(AS) $(ASFLAGS) boot.s -o boot.o

# arch
arch/io.o: src/arch/io.c include/kernel.h | arch
	$(CC) -c src/arch/io.c -o arch/io.o $(CFLAGS)

arch/interrupts.o: src/arch/interrupts.c include/kernel.h | arch
	$(CC) -c src/arch/interrupts.c -o arch/interrupts.o $(CFLAGS)

arch/cpu.o: src/arch/cpu.c include/kernel.h | arch
	$(CC) -c src/arch/cpu.c -o arch/cpu.o $(CFLAGS)

# drivers
drivers/keyboard.o: src/drivers/keyboard.c include/kernel.h | drivers
	$(CC) -c src/drivers/keyboard.c -o drivers/keyboard.o $(CFLAGS)

drivers/pci.o: src/drivers/pci.c include/kernel.h | drivers
	$(CC) -c src/drivers/pci.c -o drivers/pci.o $(CFLAGS)

drivers/mouse.o: src/drivers/mouse.c include/kernel.h | drivers
	$(CC) -c src/drivers/mouse.c -o drivers/mouse.o $(CFLAGS)

# fs
fs/fs.o: src/fs/fs.c include/kernel.h | fs
	$(CC) -c src/fs/fs.c -o fs/fs.o $(CFLAGS)

# ui
ui/terminal.o: src/ui/terminal.c include/kernel.h | ui
	$(CC) -c src/ui/terminal.c -o ui/terminal.o $(CFLAGS)

ui/ui.o: src/ui/ui.c include/kernel.h | ui
	$(CC) -c src/ui/ui.c -o ui/ui.o $(CFLAGS)

ui/fastfetch.o: src/ui/fastfetch.c include/kernel.h | ui
	$(CC) -c src/ui/fastfetch.c -o ui/fastfetch.o $(CFLAGS)

ui/mouse.o: src/ui/mouse.c include/kernel.h | ui
	$(CC) -c src/ui/mouse.c -o ui/mouse.o $(CFLAGS)

# shell
shell/commands.o: src/shell/commands.c include/kernel.h include/sharkscript.h | shell
	$(CC) -c src/shell/commands.c -o shell/commands.o $(CFLAGS)

shell/main.o: src/shell/main.c include/kernel.h | shell
	$(CC) -c src/shell/main.c -o shell/main.o $(CFLAGS)

# lib
lib/lib.o: src/lib/lib.c include/kernel.h | lib
	$(CC) -c src/lib/lib.c -o lib/lib.o $(CFLAGS)

lib/globals.o: src/lib/globals.c include/kernel.h | lib
	$(CC) -c src/lib/globals.c -o lib/globals.o $(CFLAGS)

lib/pmm.o: src/lib/pmm.c include/kernel.h | lib
	$(CC) -c src/lib/pmm.c -o lib/pmm.o $(CFLAGS)

lib/elf.o: src/lib/elf.c include/kernel.h | lib
	$(CC) -c src/lib/elf.c -o lib/elf.o $(CFLAGS)

# sharkscript (built-in interpreter)
sharkscript/shs.o: src/sharkscript/shs.c include/kernel.h include/sharkscript.h | sharkscript
	$(CC) -c src/sharkscript/shs.c -o sharkscript/shs.o $(CFLAGS)

sharkos.bin: boot.o arch/io.o arch/interrupts.o arch/cpu.o drivers/keyboard.o drivers/pci.o \
             drivers/mouse.o fs/fs.o ui/terminal.o ui/ui.o ui/fastfetch.o ui/mouse.o \
             shell/commands.o shell/main.o \
             lib/lib.o lib/globals.o lib/pmm.o lib/elf.o sharkscript/shs.o linker.ld
	$(CC) -T linker.ld -o sharkos.bin $(LDFLAGS) boot.o arch/io.o arch/interrupts.o arch/cpu.o \
		drivers/keyboard.o drivers/pci.o drivers/mouse.o fs/fs.o ui/terminal.o ui/ui.o \
		ui/fastfetch.o ui/mouse.o shell/commands.o shell/main.o lib/lib.o lib/globals.o \
		lib/pmm.o lib/elf.o sharkscript/shs.o

sharkos.iso: sharkos.bin grub.cfg
	mkdir -p isodir/boot/grub
	cp sharkos.bin isodir/boot/sharkos.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o sharkos.iso isodir

clean:
	rm -rf isodir arch drivers fs ui shell lib sharkscript
	rm -f *.o sharkos.bin sharkos.iso sharkscript

.PHONY: all clean $(DIRS)