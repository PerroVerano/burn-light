#!/usr/bin/env python3
"""Write the standard PNG ICNS elements without requiring iconutil's GUI services."""
import pathlib
import struct
import sys

folder = pathlib.Path(sys.argv[1])
elements = [('ic07', '128x128'), ('ic08', '256x256'), ('ic09', '512x512'),
            ('ic10', '512x512@2x'), ('ic11', '16x16@2x'), ('ic12', '32x32@2x'),
            ('ic13', '128x128@2x'), ('ic14', '256x256@2x')]
body = b''
for kind, name in elements:
    data = (folder / ('icon_' + name + '.png')).read_bytes()
    body += struct.pack('>4sI', kind.encode('ascii'), len(data) + 8) + data
pathlib.Path(sys.argv[2]).write_bytes(struct.pack('>4sI', b'icns', len(body) + 8) + body)
