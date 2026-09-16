#!/usr/bin/env python3
"""Download all linuxdeploy executables only from the reviewed SHA256 lock."""
import hashlib
import json
from pathlib import Path
import sys
import urllib.request

root = Path(__file__).resolve().parents[1]
target = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/linux-tools'
target.mkdir(parents=True, exist_ok=True)
for entry in json.loads((root / 'packaging/linux-tools.json').read_text()):
    path = target / entry['name']
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
        request = urllib.request.Request(entry['url'], headers={'User-Agent': 'GitCanvas-packager', 'Accept': 'application/octet-stream'})
        with urllib.request.urlopen(request, timeout=120) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise SystemExit(f'Checksum mismatch for {path.name}; no downloaded code executed')
        path.write_bytes(data)
    path.chmod(0o755)
print(target.resolve())
