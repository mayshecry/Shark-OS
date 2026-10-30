PREFIX = 
CC = gcc
HOSTCC ?= gcc
AS = as
OBJCOPY = $(PREFIX)objcopy

CFLAGS = -m64 -std=gnu99 -ffreestanding -Os -Wall -Wextra -fno-pie -fno-stack-protector \
         -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only \
         -fomit-frame-pointer -fmerge-all-constants -fno-unwind-tables -fno-exceptions \
         -mcmodel=kernel -Iinclude
ASFLAGS = --64
LDFLAGS = -m64 -ffreestanding -Os -nostdlib -no-pie -Wl,-m,elf_x86_64 -Wl,-gc-sections -Wl,--strip-all

DIRS = arch drivers drivers/audio fs ui shell lib sharkscript doom flappybird smb pong geometrydash fnaf desktop net browser

all: sharkos.iso

$(DIRS):
	mkdir -p $@

plugins:
	mkdir -p plugins

plugins/sharkcube:
	mkdir -p plugins/sharkcube

plugins/sharkcraft:
	mkdir -p plugins/sharkcraft

plugins/fnaf:
	mkdir -p plugins/fnaf

boot.o: boot.s
	$(AS) $(ASFLAGS) boot.s -o boot.o

arch/early.o: src/arch/early.c include/kernel.h | arch
	$(CC) -c src/arch/early.c -o arch/early.o $(CFLAGS)

arch/io.o: src/arch/io.c include/kernel.h | arch
	$(CC) -c src/arch/io.c -o arch/io.o $(CFLAGS)

arch/interrupts.o: src/arch/interrupts.c include/kernel.h | arch
	$(CC) -c src/arch/interrupts.c -o arch/interrupts.o $(CFLAGS)

arch/cpu.o: src/arch/cpu.c include/kernel.h | arch
	$(CC) -c src/arch/cpu.c -o arch/cpu.o $(CFLAGS)

arch/mtrr.o: src/arch/mtrr.c include/kernel.h | arch
	$(CC) -c src/arch/mtrr.c -o arch/mtrr.o $(CFLAGS)

drivers/keyboard.o: src/drivers/keyboard.c include/kernel.h | drivers
	$(CC) -c src/drivers/keyboard.c -o drivers/keyboard.o $(CFLAGS)

drivers/pci.o: src/drivers/pci.c include/kernel.h | drivers
	$(CC) -c src/drivers/pci.c -o drivers/pci.o $(CFLAGS)

drivers/audio/audio.o: src/drivers/audio/audio.c include/kernel.h include/audio.h | drivers/audio
	$(CC) -c src/drivers/audio/audio.c -o drivers/audio/audio.o $(CFLAGS)
drivers/audio/mixer.o: src/drivers/audio/mixer.c include/kernel.h include/audio.h | drivers/audio
	$(CC) -c src/drivers/audio/mixer.c -o drivers/audio/mixer.o $(CFLAGS)
drivers/audio/ac97.o: src/drivers/audio/ac97.c include/kernel.h include/audio.h | drivers/audio
	$(CC) -c src/drivers/audio/ac97.c -o drivers/audio/ac97.o $(CFLAGS)
drivers/audio/hda.o: src/drivers/audio/hda.c include/kernel.h include/audio.h | drivers/audio
	$(CC) -c src/drivers/audio/hda.c -o drivers/audio/hda.o $(CFLAGS)
drivers/audio/pcspkr.o: src/drivers/audio/pcspkr.c include/kernel.h include/audio.h | drivers/audio
	$(CC) -c src/drivers/audio/pcspkr.c -o drivers/audio/pcspkr.o $(CFLAGS)
drivers/audio/wav.o: src/drivers/audio/wav.c include/kernel.h include/audio.h | drivers/audio
	$(CC) -c src/drivers/audio/wav.c -o drivers/audio/wav.o $(CFLAGS)


net/net.o: src/net/net.c include/kernel.h include/net.h include/tls.h | net
	$(CC) -c src/net/net.c -o net/net.o $(CFLAGS)

net/crypto.o: src/net/crypto.c include/tls.h | net
	$(CC) -c src/net/crypto.c -o net/crypto.o $(CFLAGS)

net/tls.o: src/net/tls.c include/tls.h | net
	$(CC) -c src/net/tls.c -o net/tls.o $(CFLAGS)

net/httpd.o: src/net/httpd.c include/httpd.h include/kernel.h include/net.h | net
	$(CC) -c src/net/httpd.c -o net/httpd.o $(CFLAGS)

net/ed25519.o: src/net/ed25519.c include/tls.h include/kernel.h | net
	$(CC) -c src/net/ed25519.c -o net/ed25519.o $(CFLAGS)

net/ssh.o: src/net/ssh.c include/ssh.h include/httpd.h include/kernel.h include/net.h include/tls.h | net
	$(CC) -c src/net/ssh.c -o net/ssh.o $(CFLAGS)

server-build:
	mkdir -p server-build

server-build/main.o: src/server/main.c include/httpd.h include/ssh.h include/kernel.h include/net.h | server-build
	$(CC) -c src/server/main.c -o server-build/main.o $(CFLAGS)

server-build/globals.o: src/server/globals.c include/kernel.h | server-build
	$(CC) -c src/server/globals.c -o server-build/globals.o $(CFLAGS)

server-build/lib.o: src/server/lib.c include/kernel.h | server-build
	$(CC) -c src/server/lib.c -o server-build/lib.o $(CFLAGS)

server-build/interrupts.o: src/server/interrupts.c include/kernel.h | server-build
	$(CC) -c src/server/interrupts.c -o server-build/interrupts.o $(CFLAGS)

drivers/mouse.o: src/drivers/mouse.c include/kernel.h | drivers
	$(CC) -c src/drivers/mouse.c -o drivers/mouse.o $(CFLAGS)

drivers/rtc.o: src/drivers/rtc.c include/kernel.h | drivers
	$(CC) -c src/drivers/rtc.c -o drivers/rtc.o $(CFLAGS)

drivers/ata.o: src/drivers/ata.c include/kernel.h include/ata.h | drivers
	$(CC) -c src/drivers/ata.c -o drivers/ata.o $(CFLAGS)

fs/fs.o: src/fs/fs.c include/kernel.h | fs
	$(CC) -c src/fs/fs.c -o fs/fs.o $(CFLAGS)

fs/fat16.o: src/fs/fat16.c include/kernel.h include/ata.h | fs
	$(CC) -c src/fs/fat16.c -o fs/fat16.o $(CFLAGS)

ui/terminal.o: src/ui/terminal.c include/kernel.h | ui
	$(CC) -c src/ui/terminal.c -o ui/terminal.o $(CFLAGS)

ui/ui.o: src/ui/ui.c include/kernel.h | ui
	$(CC) -c src/ui/ui.c -o ui/ui.o $(CFLAGS)

ui/fastfetch.o: src/ui/fastfetch.c include/kernel.h | ui
	$(CC) -c src/ui/fastfetch.c -o ui/fastfetch.o $(CFLAGS)

ui/mouse.o: src/ui/mouse.c include/kernel.h | ui
	$(CC) -c src/ui/mouse.c -o ui/mouse.o $(CFLAGS)

desktop/bootscreen.o: src/desktop/bootscreen.c include/kernel.h include/desktop.h | desktop
	$(CC) -c src/desktop/bootscreen.c -o desktop/bootscreen.o $(CFLAGS)

desktop/windowmanager.o: src/desktop/windowmanager.c include/kernel.h include/desktop.h | desktop
	$(CC) -c src/desktop/windowmanager.c -o desktop/windowmanager.o $(CFLAGS)

desktop/appwindows.o: src/desktop/appwindows.c include/kernel.h include/desktop.h | desktop
	$(CC) -c src/desktop/appwindows.c -o desktop/appwindows.o $(CFLAGS)

desktop/startmenu.o: src/desktop/startmenu.c include/kernel.h include/desktop.h | desktop
	$(CC) -c src/desktop/startmenu.c -o desktop/startmenu.o $(CFLAGS)

desktop/desktop.o: src/desktop/desktop.c include/kernel.h include/desktop.h include/wallpaper_data.h include/kawaii_wallpaper_data.h include/wallpaper_cottage_data.h include/wallpaper_sakura_data.h include/wallpaper_neko_data.h include/wallpaper_thumbs_data.h | desktop
	$(CC) -c src/desktop/desktop.c -o desktop/desktop.o $(CFLAGS)

desktop/icons.o: src/desktop/icons.c include/kernel.h include/desktop.h include/icon_data.h | desktop
	$(CC) -c src/desktop/icons.c -o desktop/icons.o $(CFLAGS)

desktop/png.o: src/desktop/png.c include/kernel.h | desktop
	$(CC) -c src/desktop/png.c -o desktop/png.o $(CFLAGS)

desktop/gif.o: src/desktop/gif.c include/kernel.h | desktop
	$(CC) -c src/desktop/gif.c -o desktop/gif.o $(CFLAGS)

desktop/jpeg.o: src/desktop/jpeg.c include/kernel.h | desktop
	$(CC) -c src/desktop/jpeg.c -o desktop/jpeg.o $(CFLAGS)

desktop/webp.o: src/desktop/webp.c include/kernel.h | desktop
	$(CC) -c src/desktop/webp.c -o desktop/webp.o $(CFLAGS)

BROWSER_HDRS = include/browser.h include/browser_internal.h include/font.h include/kernel.h include/desktop.h include/win98_theme.h include/theme.h include/net.h

browser/svg.o: src/browser/svg.c $(BROWSER_HDRS) | browser
	$(CC) -c src/browser/svg.c -o browser/svg.o $(CFLAGS)

browser/font.o: src/browser/font.c $(BROWSER_HDRS) include/font_data.h | browser
	$(CC) -c src/browser/font.c -o browser/font.o $(CFLAGS)

browser/html.o: src/browser/html.c $(BROWSER_HDRS) | browser
	$(CC) -c src/browser/html.c -o browser/html.o $(CFLAGS)

browser/css.o: src/browser/css.c $(BROWSER_HDRS) | browser
	$(CC) -c src/browser/css.c -o browser/css.o $(CFLAGS)

browser/layout.o: src/browser/layout.c $(BROWSER_HDRS) | browser
	$(CC) -c src/browser/layout.c -o browser/layout.o $(CFLAGS)

browser/js.o: src/browser/js.c $(BROWSER_HDRS) | browser
	$(CC) -c src/browser/js.c -o browser/js.o $(CFLAGS)

browser/browser.o: src/browser/browser.c $(BROWSER_HDRS) | browser
	$(CC) -c src/browser/browser.c -o browser/browser.o $(CFLAGS)

desktop/win98_widgets.o: src/desktop/win98_widgets.c include/kernel.h include/win98_theme.h include/theme.h | desktop
	$(CC) -c src/desktop/win98_widgets.c -o desktop/win98_widgets.o $(CFLAGS)

desktop/theme.o: src/desktop/theme.c include/kernel.h include/desktop.h include/theme.h | desktop
	$(CC) -c src/desktop/theme.c -o desktop/theme.o $(CFLAGS)

desktop/diskmgmt.o: src/desktop/diskmgmt.c include/kernel.h include/desktop.h include/ata.h | desktop
	$(CC) -c src/desktop/diskmgmt.c -o desktop/diskmgmt.o $(CFLAGS)

shell/commands.o: src/shell/commands.c include/kernel.h include/sharkscript.h include/ata.h include/desktop.h | shell
	$(CC) -c src/shell/commands.c -o shell/commands.o $(CFLAGS)

shell/spkg.o: src/shell/spkg.c include/kernel.h include/plugin_manager.h | shell
	$(CC) -c src/shell/spkg.c -o shell/spkg.o $(CFLAGS)

shell/main.o: src/shell/main.c include/kernel.h include/desktop.h include/plugin_manager.h include/geometrydash.h include/net.h | shell
	$(CC) -c src/shell/main.c -o shell/main.o $(CFLAGS)

shell/lite.o: src/shell/lite.c include/kernel.h include/net.h | shell
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

plugins/geometrydash/geometrydash_plugin.o: plugins/geometrydash/geometrydash_plugin.c include/sharkapi.h include/plugin_manager.h include/geometrydash.h | plugins/geometrydash
	$(CC) -c plugins/geometrydash/geometrydash_plugin.c -o plugins/geometrydash/geometrydash_plugin.o $(CFLAGS)

plugins/sharkcube/sharkcube_plugin.o: plugins/sharkcube/sharkcube_plugin.c include/sharkapi.h include/desktop.h | plugins/sharkcube
	$(CC) -DSHARKCUBE_BUILTIN -c plugins/sharkcube/sharkcube_plugin.c -o plugins/sharkcube/sharkcube_plugin.o $(CFLAGS)

plugins/sharkcube/sharkcube.plg: plugins/sharkcube/sharkcube_plugin.c include/sharkapi.h | plugins/sharkcube
	$(CC) -c plugins/sharkcube/sharkcube_plugin.c -o plugins/sharkcube/sharkcube.plg $(CFLAGS)

plugins/sharkcraft/sharkcraft_plugin.o: plugins/sharkcraft/sharkcraft_plugin.c include/sharkapi.h include/desktop.h | plugins/sharkcraft
	$(CC) -DSHARKCRAFT_BUILTIN -c plugins/sharkcraft/sharkcraft_plugin.c -o plugins/sharkcraft/sharkcraft_plugin.o $(CFLAGS)

plugins/sharkcraft/sharkcraft.plg: plugins/sharkcraft/sharkcraft_plugin.c include/sharkapi.h include/desktop.h | plugins/sharkcraft
	$(CC) -c plugins/sharkcraft/sharkcraft_plugin.c -o plugins/sharkcraft/sharkcraft.plg $(CFLAGS)

plugins/fnaf/fnaf_plugin.o: plugins/fnaf/fnaf_plugin.c include/sharkapi.h include/plugin_manager.h include/fnaf.h | plugins/fnaf
	$(CC) -DFNAF_BUILTIN -c plugins/fnaf/fnaf_plugin.c -o plugins/fnaf/fnaf_plugin.o $(CFLAGS)

plugins/fnaf/fnaf.plg: plugins/fnaf/fnaf_plugin.c include/sharkapi.h include/fnaf.h | plugins/fnaf
	$(CC) -c plugins/fnaf/fnaf_plugin.c -o plugins/fnaf/fnaf.plg $(CFLAGS)

doom/doom.o: src/doom/doom.c include/kernel.h include/doom.h | doom
	$(CC) -c src/doom/doom.c -o doom/doom.o $(CFLAGS)

flappybird/flappybird.o: src/flappybird/flappybird.c include/kernel.h include/flappybird.h | flappybird
	$(CC) -c src/flappybird/flappybird.c -o flappybird/flappybird.o $(CFLAGS)

pong/pong.o: src/pong/pong.c include/kernel.h include/pong.h | pong
	$(CC) -c src/pong/pong.c -o pong/pong.o $(CFLAGS)

smb/smb.o: src/smb/smb.c include/kernel.h include/smb.h | smb
	$(CC) -c src/smb/smb.c -o smb/smb.o $(CFLAGS)

geometrydash/geometrydash.o: src/geometrydash/geometrydash.c include/kernel.h include/geometrydash.h | geometrydash
	$(CC) -c src/geometrydash/geometrydash.c -o geometrydash/geometrydash.o $(CFLAGS)

fnaf/fnaf.o: src/fnaf/fnaf.c include/kernel.h include/fnaf.h | fnaf
	$(CC) -c src/fnaf/fnaf.c -o fnaf/fnaf.o $(CFLAGS)

sharkscript/shs.o: src/sharkscript/shs.c include/kernel.h include/sharkscript.h | sharkscript
	$(CC) -c src/sharkscript/shs.c -o sharkscript/shs.o $(CFLAGS)

sharkos.bin: Makefile boot.o arch/early.o arch/io.o arch/interrupts.o arch/cpu.o arch/mtrr.o drivers/keyboard.o drivers/pci.o \
             drivers/mouse.o drivers/rtc.o drivers/ata.o drivers/audio/audio.o drivers/audio/mixer.o drivers/audio/ac97.o drivers/audio/hda.o drivers/audio/pcspkr.o drivers/audio/wav.o fs/fs.o fs/fat16.o ui/terminal.o ui/ui.o ui/fastfetch.o ui/mouse.o \
             shell/commands.o shell/spkg.o shell/main.o shell/lite.o \
             lib/lib.o lib/globals.o lib/pmm.o lib/elf.o lib/sharkapi.o lib/plugin_manager.o \
             plugins/python-interp.o plugins/doom/doom_plugin.o plugins/flappybird/flappybird_plugin.o plugins/pong/pong_plugin.o plugins/smb/smb_plugin.o plugins/geometrydash/geometrydash_plugin.o plugins/sharkcube/sharkcube_plugin.o plugins/sharkcraft/sharkcraft_plugin.o plugins/fnaf/fnaf_plugin.o sharkscript/shs.o doom/doom.o flappybird/flappybird.o pong/pong.o smb/smb.o geometrydash/geometrydash.o fnaf/fnaf.o \
             desktop/bootscreen.o desktop/windowmanager.o desktop/appwindows.o desktop/startmenu.o desktop/desktop.o desktop/icons.o desktop/win98_widgets.o desktop/theme.o desktop/diskmgmt.o desktop/png.o desktop/gif.o desktop/jpeg.o desktop/webp.o browser/svg.o net/net.o net/crypto.o net/tls.o \
             browser/html.o browser/css.o browser/layout.o browser/js.o browser/browser.o browser/font.o linker.ld
	$(CC) -T linker.ld -o sharkos.bin $(LDFLAGS) boot.o arch/early.o arch/io.o arch/interrupts.o arch/cpu.o arch/mtrr.o \
		drivers/keyboard.o drivers/pci.o drivers/mouse.o drivers/rtc.o drivers/ata.o drivers/audio/audio.o drivers/audio/mixer.o drivers/audio/ac97.o drivers/audio/hda.o drivers/audio/pcspkr.o drivers/audio/wav.o fs/fs.o fs/fat16.o ui/terminal.o ui/ui.o \
		ui/fastfetch.o ui/mouse.o shell/commands.o shell/spkg.o shell/main.o shell/lite.o lib/lib.o lib/globals.o lib/pmm.o lib/elf.o lib/sharkapi.o lib/plugin_manager.o plugins/python-interp.o plugins/doom/doom_plugin.o plugins/flappybird/flappybird_plugin.o plugins/pong/pong_plugin.o plugins/smb/smb_plugin.o plugins/geometrydash/geometrydash_plugin.o plugins/sharkcube/sharkcube_plugin.o plugins/sharkcraft/sharkcraft_plugin.o plugins/fnaf/fnaf_plugin.o sharkscript/shs.o doom/doom.o flappybird/flappybird.o pong/pong.o smb/smb.o geometrydash/geometrydash.o fnaf/fnaf.o \
		desktop/bootscreen.o desktop/windowmanager.o desktop/appwindows.o desktop/startmenu.o desktop/desktop.o desktop/icons.o desktop/win98_widgets.o desktop/theme.o desktop/diskmgmt.o desktop/png.o desktop/gif.o desktop/jpeg.o desktop/webp.o browser/svg.o net/net.o net/crypto.o net/tls.o \
		browser/html.o browser/css.o browser/layout.o browser/js.o browser/browser.o browser/font.o -lgcc

LIMINE_DIR ?= limine

sharkos-server.bin: Makefile boot.o arch/early.o arch/io.o drivers/pci.o fs/fs.o lib/pmm.o server-build/main.o server-build/globals.o server-build/lib.o server-build/interrupts.o net/net.o net/crypto.o net/tls.o net/httpd.o net/ed25519.o net/ssh.o linker.ld
	$(CC) -T linker.ld -o sharkos-server.bin $(LDFLAGS) boot.o arch/early.o arch/io.o drivers/pci.o fs/fs.o lib/pmm.o server-build/main.o server-build/globals.o server-build/lib.o server-build/interrupts.o net/net.o net/crypto.o net/tls.o net/httpd.o net/ed25519.o net/ssh.o -lgcc

sharkos-server.iso: sharkos-server.bin limine-server.conf
	mkdir -p isodir-server/boot/limine
	mkdir -p isodir-server/EFI/BOOT
	cp sharkos-server.bin isodir-server/boot/sharkos-server.bin
	cp limine-server.conf isodir-server/boot/limine/limine.conf
	cp limine-server.conf isodir-server/limine.conf
	cp limine-server.conf isodir-server/boot/limine/limine.cfg
	cp limine-server.conf isodir-server/limine.cfg
	cp limine-server.conf isodir-server/boot/limine.cfg
	cp $(LIMINE_DIR)/limine-bios.sys isodir-server/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin isodir-server/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin isodir-server/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI isodir-server/EFI/BOOT/BOOTX64.EFI
	cp $(LIMINE_DIR)/BOOTIA32.EFI isodir-server/EFI/BOOT/BOOTIA32.EFI
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		isodir-server -o sharkos-server.iso
	$(LIMINE_DIR)/limine bios-install sharkos-server.iso

server: sharkos-server.iso

sharkos.iso: Makefile sharkos.bin limine.conf plugins/sharkcube/sharkcube.plg plugins/sharkcraft/sharkcraft.plg plugins/fnaf/fnaf.plg
	mkdir -p isodir/boot/limine
	mkdir -p isodir/boot
	mkdir -p isodir/EFI/BOOT
	mkdir -p isodir/System/Plugins
	mkdir -p isodir/pc
	cp sharkos.bin isodir/boot/sharkos.bin
	cp limine.conf isodir/boot/limine/limine.conf
	cp limine.conf isodir/limine.conf
	cp limine.conf isodir/boot/limine/limine.cfg
	cp limine.conf isodir/limine.cfg
	cp limine.conf isodir/boot/limine.cfg
	cp $(LIMINE_DIR)/limine-bios.sys isodir/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin isodir/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin isodir/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI isodir/EFI/BOOT/BOOTX64.EFI
	cp $(LIMINE_DIR)/BOOTIA32.EFI isodir/EFI/BOOT/BOOTIA32.EFI
	@echo "# SharkOS Plugins Directory" > isodir/System/Plugins/README.txt
	@echo "Place plugin binaries here" >> isodir/System/Plugins/README.txt
	cp plugins/flappybird/flappybird_plugin.o isodir/System/Plugins/flappybird.plg
	cp plugins/doom/doom_plugin.o isodir/System/Plugins/doom.plg
	cp plugins/pong/pong_plugin.o isodir/System/Plugins/pong.plg
	cp plugins/smb/smb_plugin.o isodir/System/Plugins/smb.plg
	cp plugins/geometrydash/geometrydash_plugin.o isodir/System/Plugins/gdash.plg
	cp plugins/sharkcube/sharkcube.plg isodir/System/Plugins/sharkcube.plg
	cp plugins/sharkcraft/sharkcraft.plg isodir/System/Plugins/sharkcraft.plg
	cp plugins/fnaf/fnaf.plg isodir/System/Plugins/fnaf.plg
	cp pc/9fc3fc59d52dc24748feb8836feded7e.png isodir/pc/
	xorriso -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		isodir -o sharkos.iso
	$(LIMINE_DIR)/limine bios-install sharkos.iso

clean:
	rm -rf isodir isodir-server server server-build arch drivers fs ui shell lib sharkscript doom flappybird pong smb geometrydash fnaf desktop net browser
	rm -f *.o sharkos.bin sharkos.iso sharkos-server.bin sharkos-server.iso sharkscript plugins/*.o plugins/flappybird/*.o plugins/pong/*.o plugins/smb/*.o plugins/geometrydash/*.o plugins/sharkcube/*.o plugins/sharkcube/*.plg plugins/sharkcraft/*.o plugins/sharkcraft/*.plg plugins/fnaf/*.o plugins/fnaf/*.plg

.PHONY: all clean server $(DIRS)
gcc-info:
	$(HOSTCC) --version
	$(HOSTCC) -dumpmachine

sharkcc: gcc-info
	tools/sharkcc tools/gcc_smoke.c /tmp/sharkos-gcc-smoke.o
	readelf -h /tmp/sharkos-gcc-smoke.o
	rm -f /tmp/sharkos-gcc-smoke.o

.PHONY: gcc-info sharkcc
