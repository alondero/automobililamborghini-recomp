"""Check shipped frontend assets without starting a renderer."""
import re
import sys
from pathlib import Path
from xml.etree import ElementTree


def main() -> None:
    root = Path(sys.argv[1]).resolve()
    assets = root / "assets/frontend"
    frontend = root / "lib/RecompFrontend/recompui/src"
    missing = set()
    for source in frontend.rglob("*.cpp"):
        for name in re.findall(r'"(icons/[^"\n]+\.svg)"', source.read_text(encoding="utf-8")):
            if not (assets / name).is_file():
                missing.add(name)
    assert not missing, f"Missing frontend icons: {sorted(missing)}"
    for icon in (assets / "icons").glob("*.svg"):
        ElementTree.parse(icon)
    for name in ("NotoEmoji-Regular.ttf", "NotoEmoji-LICENSE.txt", "promptfont/promptfont.ttf", "promptfont/LICENSE.txt", "Lato-LICENSE.txt", "recomp.rcss"):
        assert (assets / name).is_file(), f"Missing asset/license: {name}"
    assert "LatoLatin" in (assets / "recomp.rcss").read_text(encoding="utf-8")
    print("PASS frontend asset references and font licenses")


if __name__ == "__main__":
    main()
