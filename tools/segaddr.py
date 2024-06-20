#!/usr/bin/env python3
import re

ADDR_RE = re.compile('([0-9a-fA-F]{1,4}):([0-9a-fA-F]{1,4})')

print('Provide addresses in seg:offset format:')
while addr := input('> '):
    if (m := ADDR_RE.fullmatch(addr)) is None:
        print('Invalid address format')
        continue
    segment = int(m.group(1), base=16)
    offset = int(m.group(2), base=16)
    linear = hex(segment * 16 + offset)
    print(linear)
