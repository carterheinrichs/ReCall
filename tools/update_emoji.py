"""Regenerate the embedded Unicode 16 fully-qualified emoji dictionary (build-time only)."""
import json
from pathlib import Path
import urllib.request

URL = "https://www.unicode.org/Public/emoji/16.0/emoji-test.txt"

def main():
    text = urllib.request.urlopen(URL, timeout=60).read().decode("utf-8")
    items = []
    group = subgroup = ""
    for line in text.splitlines():
        if line.startswith("# group:"):
            group = line.split(":", 1)[1].strip()
        elif line.startswith("# subgroup:"):
            subgroup = line.split(":", 1)[1].strip()
        elif "; fully-qualified" in line:
            codes, comment = line.split("#", 1)
            glyph = "".join(chr(int(c, 16)) for c in codes.split(";")[0].split())
            name = comment.strip().split(" ", 2)[2]
            items.append({"glyph": glyph, "shortcode": name.replace(" ", "_"),
                          "tags": f"{name} {group} {subgroup}"})
    target = Path(__file__).resolve().parents[1] / "assets" / "emoji.json"
    target.write_text(json.dumps(items, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(f"Generated {len(items)} Unicode 16 emoji in {target}")

if __name__ == "__main__":
    main()
