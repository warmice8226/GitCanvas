"""Build-only offline catalog generator. Requires CTranslate2 and SentencePiece.

Input: GitCanvasTests --export-git-manuals output and Argos en_ko 1.1 model.
The application does not need these Python dependencies or the model.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

import ctranslate2
import sentencepiece

source, model_dir, target = map(Path, sys.argv[1:])
manuals = json.loads(source.read_text(encoding="utf-8"))
sp = sentencepiece.SentencePieceProcessor(model_file=str(model_dir / "sentencepiece.model"))
model = ctranslate2.Translator(str(model_dir / "model"), compute_type="int8", intra_threads=4)
normalize = lambda text: " ".join(text.split())
texts = list(dict.fromkeys(normalize(text) for manual in manuals.values()
                          for text in [manual["summary"], manual["description"]]
                          + [o["description"] for o in manual["options"]] if text.strip()))
translations = {}
for index, text in enumerate(texts):
    chunks = re.split(r"(?<=[.!?])\s+(?=[A-Z])", text)
    tokens = [sp.encode(chunk, out_type=str) for chunk in chunks]
    tokens = [row[start:start+180] for row in tokens for start in range(0, len(row), 180)]
    results = model.translate_batch(tokens, beam_size=2, max_batch_size=32, max_decoding_length=400)
    translations[text] = " ".join(sp.decode(r.hypotheses[0]) for r in results)
    if index % 100 == 0:
        print(f"Translated {index}/{len(texts)}", flush=True)
overrides = json.loads((Path(__file__).resolve().parents[1] / "i18n/git-description-overrides.ko.json").read_text(encoding="utf-8"))
translations.update({text: value for text, value in overrides.items() if text in translations})
catalog = {"metadata": {"translation": "Argos en_ko 1.1, machine translated with reviewed overrides", "source": "Installed Git HTML manuals"},
           "commands": {name: {"summary_en": m["summary"], "summary_ko": translations.get(normalize(m["summary"]), ""),
                               "description_ko": translations.get(normalize(m["description"]), "")}
                        for name, m in manuals.items()},
           "texts": {hashlib.sha256(text.encode()).hexdigest(): translated for text, translated in translations.items()}}
target.write_text(json.dumps(catalog, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
print(f"Wrote {len(texts)} translations to {target}", flush=True)
