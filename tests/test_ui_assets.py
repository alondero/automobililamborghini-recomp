#!/usr/bin/env python3

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    repo = Path(sys.argv[1]).resolve()
    ui_root = repo / "assets" / "ui"
    required_documents = {
        "launcher.rml",
        "settings.rml",
        "pages/graphics.rml",
        "pages/enhancements.rml",
        "pages/controls.rml",
        "pages/haptics.rml",
    }

    for relative in required_documents:
        require((ui_root / relative).is_file(), f"missing required UI document: {relative}")

    setting_actions: set[str] = set()
    page_actions: set[str] = set()
    for document_path in ui_root.rglob("*.rml"):
        document = ET.parse(document_path)
        for element in document.iter():
            href = element.attrib.get("href")
            if href:
                target = (document_path.parent / href).resolve()
                require(target.is_relative_to(ui_root), f"reference escapes UI assets: {href}")
                require(target.is_file(), f"missing reference from {document_path.name}: {href}")
            onclick = element.attrib.get("onclick", "")
            if onclick.startswith("setting:"):
                setting_actions.add(onclick.removeprefix("setting:"))
            elif onclick.startswith("page:"):
                page_actions.add(onclick.removeprefix("page:"))

    expected_settings = {
        "res:auto", "res:original", "res:2x",
        "ss:1", "ss:2", "ss:3", "ss:4",
        "aspect:original", "aspect:expand",
        "hud:original", "hud:16x9", "hud:full",
        "rate:original", "rate:display", "rate:30", "rate:60", "rate:90",
        "rate:120", "rate:144", "rate:165", "rate:240",
        "msaa:off", "msaa:2", "msaa:4", "msaa:8",
        "hpfb:auto", "hpfb:on", "hpfb:off",
        "api:auto", "api:d3d12", "api:vulkan",
        "fog:toggle", "sky:toggle", "lod:toggle",
        "circuit:1", "circuit:2", "circuit:3", "circuit:4", "circuit:5", "circuit:6",
        "distance:1", "distance:1.5", "distance:2", "distance:3", "distance:unlimited",
        "fogdensity:off", "fogdensity:0.5", "fogdensity:0.75", "fogdensity:1",
        "fogdensity:1.5", "fogdensity:2",
    }
    require(setting_actions == expected_settings,
            f"setting actions differ: missing={expected_settings - setting_actions}, "
            f"unexpected={setting_actions - expected_settings}")
    require({"graphics", "enhancements", "controls", "haptics"} <= page_actions,
            "settings hub is missing a stable route")

    launcher = (ui_root / "launcher.rml").read_text(encoding="utf-8")
    require("v1.0.0" not in launcher, "launcher must not hardcode a release version")

    cmake = (repo / "CMakeLists.txt").read_text(encoding="utf-8")
    require("copy_directory" in cmake and "assets/ui" in cmake,
            "UI asset directory is not packaged beside the executable")
    main_source = (repo / "src" / "main.cpp").read_text(encoding="utf-8")
    for component in ("MAJOR", "MINOR", "PATCH"):
        macro = f"LAMBO_VERSION_{component}"
        require(macro in cmake and macro in main_source,
                f"launcher and runtime must share CMake's {component.lower()} version")
    for font in ("LatoLatin-Regular.ttf", "LatoLatin-Bold.ttf"):
        require((repo / "lib" / "RmlUi" / "Samples" / "assets" / font).is_file(),
                f"missing pinned font source: {font}")
        require(font in cmake, f"font is not included in the packaging command: {font}")

    rcss = (ui_root / "lambo.rcss").read_text(encoding="utf-8")
    require("position: absolute" in rcss and "left: 7%" in rcss and "top: 7%" in rcss,
            "panel layout must bypass RmlUi's padded percentage-width behavior")
    require("font-family: Lato" in rcss, "UI stylesheet must request the registered font family")
    require("h1, h2, p, .summary, .footer" in rcss and "display: block" in rcss,
            "semantic text elements must not collapse into RmlUi's inline default")
    ui_source = (repo / "src" / "ui" / "lambo_ui.cpp").read_text(encoding="utf-8")
    require('data, "Lato"' in ui_source,
            "fonts must be registered explicitly under the stylesheet family name")
    require("font_data" in ui_source and "font_data.reserve(2)" in ui_source,
            "memory-loaded font bytes must outlive RmlUi font rasterization")
    workflow = (repo / ".github" / "workflows" / "build-release.yml").read_text(encoding="utf-8")
    require("cp -r build/assets dist/" in workflow,
            "Linux release archive must include packaged UI assets")
    require("'build\\assets'" in workflow and "'build\\freetype.dll'" in workflow,
            "Windows release archive must include UI assets and the font runtime")
    for reference in re.findall(r"url\(['\"]?([^)'\"]+)", rcss):
        require((ui_root / reference).is_file(), f"missing RCSS reference: {reference}")


if __name__ == "__main__":
    main()
