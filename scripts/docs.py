import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

root = Path(__file__).resolve().parent.parent
docs = root / "docs"
for file in [root / "README.md", *docs.rglob("*.md")]:
    text = file.read_text()
    if "\u2014" in text:
        raise SystemExit(f"{file}: disallowed punctuation")
    for href in re.findall(r"\]\(([^)]+)\)", text):
        if "://" not in href and href.split("#")[0] and not (file.parent / href.split("#")[0]).exists():
            raise SystemExit(f"{file}: unresolved link {href}")
diagrams = sorted((docs / "diagrams").glob("*.dot"))
if len(diagrams) != 12:
    raise SystemExit("missing editable diagrams")
for source in diagrams:
    checked = ET.parse(source.with_suffix(".svg")).getroot()
    fresh = ET.fromstring(subprocess.check_output(["dot", "-Tsvg", source]))
    if len(checked.findall(".//{http://www.w3.org/2000/svg}g")) != len(fresh.findall(".//{http://www.w3.org/2000/svg}g")):
        raise SystemExit(f"{source}: diagram SVG does not match graph structure")
print("Checked public links and 12 diagram exports")
