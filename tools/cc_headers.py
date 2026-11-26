import json
from pathlib import Path

headers = Path('cc-build/include')
files = {n: headers / n for n in ('stdint.h', 'stddef.h', 'stdbool.h', 'bits/stdint.h', 'bits/alltypes.h')}
files['sharkapi.h'] = Path('include/sharkapi.h')
with Path('cc-build/headers.inc').open('w') as out:
    for name, path in files.items():
        print('{"/System/Include/' + name + '",', file=out)
        for line in path.read_text().splitlines(keepends=True):
            print(json.dumps(line), file=out)
        print('},', file=out)
