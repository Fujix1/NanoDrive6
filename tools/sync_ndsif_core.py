"""Check or synchronize the shared NDSIF core in sibling NanoDrive projects."""
from argparse import ArgumentParser
from datetime import datetime
from pathlib import Path
import os
import sys

root = Path(__file__).resolve().parents[1]
parser = ArgumentParser(description=__doc__)
parser.add_argument('--write', action='store_true', help='copy the ND6 core into ND7 and ND8')
args = parser.parse_args()
changed = False
missing = False
for name in ('NanoDrive7', 'NanoDrive8'):
    target_root = root.parent / name
    for filename in ('ndsif.h', 'ndsif.cpp'):
        source = root / 'lib' / 'ndsif' / filename
        target = target_root / 'lib' / 'ndsif' / filename
        if not target.is_file():
            print(f'Missing: {target}')
            changed = True
            missing = True
            continue
        data = source.read_bytes()
        if target.read_bytes().replace(bytes((13, 10)), bytes((10,))) == data.replace(bytes((13, 10)), bytes((10,))):
            continue
        changed = True
        if not args.write:
            print(f'Different: {target}')
            continue
        backup = target_root / '.pio' / 'ndsif_backups' / (datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '_sync_core')
        backup.mkdir(parents=True)
        (backup / filename).write_bytes(target.read_bytes())
        temporary = target.with_name(target.name + '.tmp')
        temporary.write_bytes(data)
        os.replace(temporary, target)
        print(f'Updated: {target}')
if not changed:
    print('NDSIF core is identical in ND6, ND7 and ND8.')
if missing or (changed and not args.write):
    sys.exit(1)
