"""Check embedded source-key translations and Qt argument placeholders."""
import collections
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
catalog = json.loads((root / "i18n/en.json").read_text(encoding="utf-8"))
sources = set()
for path in (root / "src").rglob("*"):
    if path.suffix not in (".cpp", ".h"):
        continue
    for literal in re.findall(r'\btr\("((?:[^"\\]|\\.)*)"\)', path.read_text(encoding="utf-8-sig")):
        text = json.loads('"' + literal + '"')
        if re.search(r"[\uac00-\ud7a3]", text):
            sources.add(text)
errors = []
for source in sorted(sources):
    translation = catalog.get(source)
    if not translation or re.search(r"[\uac00-\ud7a3]", translation):
        errors.append("Missing English translation: " + ascii(source))
        continue
    placeholders = lambda text: collections.Counter(re.findall(r"%L?\d+|%n", text))
    if placeholders(source) != placeholders(translation):
        errors.append("Placeholder mismatch: " + ascii(source))
if errors:
    raise SystemExit("\n".join(errors))
print(f"Verified {len(sources)} Korean source strings, English translations, and placeholders.")
