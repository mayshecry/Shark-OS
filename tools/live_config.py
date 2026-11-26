from pathlib import Path
import sys
import secrets

source, dest = map(Path, sys.argv[1:3])
installer = sys.argv[3] == 'live'
global_options, *entries = source.read_text().strip().split('\n\n')
if installer:
    entries.append('/SharkOS (live only, lower RAM)\n    protocol: multiboot1\n    kernel_path: boot():/boot/sharkos.bin')
if not installer:
    token = secrets.token_hex(16)
    (dest.parent / 'install-build' / 'volume-id').write_text(token)
config = [global_options]
for entry in entries:
    if installer and entry.startswith(('/SharkOS\n', '/SharkOS Kawaii\n')):
        entry += '\n    module_path: $boot():/boot/install.pack.gz'
    if not installer:
        if '    cmdline:' in entry:
            entry += ' sharkfs=' + token
        else:
            entry += '\n    cmdline: sharkfs=' + token
    # This kernel is linked at 16 MiB and boots with either firmware.
    config.append(entry.replace('boot():/boot/sharkos.bin', '$boot():/boot/sharkos-uefi.gz' if installer else 'boot():/boot/desktop.bin'))
for path in ('limine.conf','limine.cfg','boot/limine.cfg','boot/limine/limine.conf','boot/limine/limine.cfg'):
    (dest / path).write_text('\n\n'.join(config) + '\n')
