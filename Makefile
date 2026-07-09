PREFIX = 
CC = gcc
AS = as
OBJCOPY = $(PREFIX)objcopy

CFLAGS = -m32 -std=gnu99 -ffreestanding -Os -Wall -Wextra -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
         -fomit-frame-pointer -fmerge-all-constants -fno-unwind-tables -fno-exceptions \
         -Iinclude
ASFLAGS = --32
LDFLAGS = -m32 -ffreestanding -Os -nostdlib -lgcc -no-pie -Wl,-m,elf_i386 -Wl,-gc-sections -Wl,--strip-all

DIRS = arch drivers fs ui shell lib sharkscript doom flappybird smb pong

all: sharkos.iso

$(DIRS):
	mkdir -p $@

plugins:
	mkdir -p plugins

boot.o: boot.s
	$(AS) $(ASFLAGS) boot.s -o boot.o

arch/io.o: src/arch/io.c include/kernel.h | arch
	$(CC) -c src/arch/io.c -o arch/io.o $(CFLAGS)

arch/interrupts.o: src/arch/interrupts.c include/kernel.h | arch
	$(CC) -c src/arch/interrupts.c -o arch/interrupts.o $(CFLAGS)

arch/cpu.o: src/arch/cpu.c include/kernel.h | arch
	$(CC) -c src/arch/cpu.c -o arch/cpu.o $(CFLAGS)

drivers/keyboard.o: src/drivers/keyboard.c include/kernel.h | drivers
	$(CC) -c src/drivers/keyboard.c -o drivers/keyboard.o $(CFLAGS)

drivers/pci.o: src/drivers/pci.c include/kernel.h | drivers
	$(CC) -c src/drivers/pci.c -o drivers/pci.o $(CFLAGS)

drivers/mouse.o: src/drivers/mouse.c include/kernel.h | drivers
	$(CC) -c src/drivers/mouse.c -o drivers/mouse.o $(CFLAGS)

fs/fs.o: src/fs/fs.c include/kernel.h | fs
	$(CC) -c src/fs/fs.c -o fs/fs.o $(CFLAGS)

ui/terminal.o: src/ui/terminal.c include/kernel.h | ui
	$(CC) -c src/ui/terminal.c -o ui/terminal.o $(CFLAGS)

ui/ui.o: src/ui/ui.c include/kernel.h | ui
	$(CC) -c src/ui/ui.c -o ui/ui.o $(CFLAGS)

ui/fastfetch.o: src/ui/fastfetch.c include/kernel.h | ui
	$(CC) -c src/ui/fastfetch.c -o ui/fastfetch.o $(CFLAGS)

ui/mouse.o: src/ui/mouse.c include/kernel.h | ui
	$(CC) -c src/ui/mouse.c -o ui/mouse.o $(CFLAGS)

shell/commands.o: src/shell/commands.c include/kernel.h include/sharkscript.h | shell
	$(CC) -c src/shell/commands.c -o shell/commands.o $(CFLAGS)

shell/spkg.o: src/shell/spkg.c include/kernel.h include/plugin_manager.h | shell
	$(CC) -c src/shell/spkg.c -o shell/spkg.o $(CFLAGS)

shell/main.o: src/shell/main.c include/kernel.h include/plugin_manager.h | shell
	$(CC) -c src/shell/main.c -o shell/main.o $(CFLAGS)

shell/lite.o: src/shell/lite.c include/kernel.h | shell
	$(CC) -c src/shell/lite.c -o shell/lite.o $(CFLAGS)

lib/lib.o: src/lib/lib.c include/kernel.h | lib
	$(CC) -c src/lib/lib.c -o lib/lib.o $(CFLAGS)

lib/globals.o: src/lib/globals.c include/kernel.h | lib
	$(CC) -c src/lib/globals.c -o lib/globals.o $(CFLAGS)

lib/pmm.o: src/lib/pmm.c include/kernel.h | lib
	$(CC) -c src/lib/pmm.c -o lib/pmm.o $(CFLAGS)

lib/elf.o: src/lib/elf.c include/kernel.h | lib
	$(CC) -c src/lib/elf.c -o lib/elf.o $(CFLAGS)

lib/sharkapi.o: src/lib/sharkapi.c include/kernel.h include/sharkapi.h | lib
	$(CC) -c src/lib/sharkapi.c -o lib/sharkapi.o $(CFLAGS)

lib/plugin_manager.o: src/lib/plugin_manager.c include/kernel.h include/plugin_manager.h | lib
	$(CC) -c src/lib/plugin_manager.c -o lib/plugin_manager.o $(CFLAGS)

plugins/python-interp.o: plugins/python-interp/python.c include/sharkapi.h include/plugin_manager.h | plugins
	$(CC) -c plugins/python-interp/python.c -o plugins/python-interp.o $(CFLAGS)

plugins/doom/doom_plugin.o: plugins/doom/doom_plugin.c include/sharkapi.h include/plugin_manager.h include/doom.h | plugins/doom
	$(CC) -c plugins/doom/doom_plugin.c -o plugins/doom/doom_plugin.o $(CFLAGS)

plugins/flappybird/flappybird_plugin.o: plugins/flappybird/flappybird_plugin.c include/sharkapi.h include/plugin_manager.h include/flappybird.h | plugins/flappybird
	$(CC) -c plugins/flappybird/flappybird_plugin.c -o plugins/flappybird/flappybird_plugin.o $(CFLAGS)

plugins/pong/pong_plugin.o: plugins/pong/pong_plugin.c include/sharkapi.h include/plugin_manager.h include/pong.h | plugins/pong
	$(CC) -c plugins/pong/pong_plugin.c -o plugins/pong/pong_plugin.o $(CFLAGS)

plugins/smb/smb_plugin.o: plugins/smb/smb_plugin.c include/sharkapi.h include/plugin_manager.h include/smb.h | plugins/smb
	$(CC) -c plugins/smb/smb_plugin.c -o plugins/smb/smb_plugin.o $(CFLAGS)

doom/doom.o: src/doom/doom.c include/kernel.h include/doom.h | doom
	$(CC) -c src/doom/doom.c -o doom/doom.o $(CFLAGS)

flappybird/flappybird.o: src/flappybird/flappybird.c include/kernel.h include/flappybird.h | flappybird
	$(CC) -c src/flappybird/flappybird.c -o flappybird/flappybird.o $(CFLAGS)

pong/pong.o: src/pong/pong.c include/kernel.h include/pong.h | pong
	$(CC) -c src/pong/pong.c -o pong/pong.o $(CFLAGS)

smb/smb.o: src/smb/smb.c include/kernel.h include/smb.h | smb
	$(CC) -c src/smb/smb.c -o smb/smb.o $(CFLAGS)

sharkscript/shs.o: src/sharkscript/shs.c include/kernel.h include/sharkscript.h | sharkscript
	$(CC) -c src/sharkscript/shs.c -o sharkscript/shs.o $(CFLAGS)

sharkos.bin: boot.o arch/io.o arch/interrupts.o arch/cpu.o drivers/keyboard.o drivers/pci.o \
             drivers/mouse.o fs/fs.o ui/terminal.o ui/ui.o ui/fastfetch.o ui/mouse.o \
             shell/commands.o shell/spkg.o shell/main.o shell/lite.o \
             lib/lib.o lib/globals.o lib/pmm.o lib/elf.o lib/sharkapi.o lib/plugin_manager.o \
             plugins/python-interp.o plugins/doom/doom_plugin.o plugins/flappybird/flappybird_plugin.o plugins/pong/pong_plugin.o plugins/smb/smb_plugin.o sharkscript/shs.o doom/doom.o flappybird/flappybird.o pong/pong.o smb/smb.o linker.ld
	$(CC) -T linker.ld -o sharkos.bin $(LDFLAGS) boot.o arch/io.o arch/interrupts.o arch/cpu.o \
		drivers/keyboard.o drivers/pci.o drivers/mouse.o fs/fs.o ui/terminal.o ui/ui.o \
		ui/fastfetch.o ui/mouse.o shell/commands.o shell/spkg.o shell/main.o shell/lite.o lib/lib.o lib/globals.o lib/pmm.o lib/elf.o lib/sharkapi.o lib/plugin_manager.o plugins/python-interp.o plugins/doom/doom_plugin.o plugins/flappybird/flappybird_plugin.o plugins/pong/pong_plugin.o plugins/smb/smb_plugin.o sharkscript/shs.o doom/doom.o flappybird/flappybird.o pong/pong.o smb/smb.o

sharkos.iso: sharkos.bin grub.cfg
	mkdir -p isodir/boot/grub
	mkdir -p isodir/System/Plugins
	cp sharkos.bin isodir/boot/sharkos.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	@echo "# SharkOS Plugins Directory" > isodir/System/Plugins/README.txt
	@echo "Place plugin binaries here" >> isodir/System/Plugins/README.txt
	cp plugins/flappybird/flappybird_plugin.o isodir/System/Plugins/flappybird.plg
	cp plugins/doom/doom_plugin.o isodir/System/Plugins/doom.plg
	cp plugins/pong/pong_plugin.o isodir/System/Plugins/pong.plg
	cp plugins/smb/smb_plugin.o isodir/System/Plugins/smb.plg
	grub-mkrescue -o sharkos.iso isodir

clean:
	rm -rf isodir arch drivers fs ui shell lib sharkscript doom flappybird pong smb
	rm -f *.o sharkos.bin sharkos.iso sharkscript plugins/*.o plugins/flappybird/*.o plugins/pong/*.o plugins/smb/*.o

.PHONY: all clean $(DIRS)