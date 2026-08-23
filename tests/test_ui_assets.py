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
        "res:next", "ss:next", "aspect:next", "hud:next", "rate:next",
        "msaa:next", "hpfb:next", "api:next",
        "fog:toggle", "sky:toggle", "lod:toggle",
        "circuit:1", "circuit:2", "circuit:3", "circuit:4", "circuit:5", "circuit:6",
        "distance:next", "fogdensity:next",
    }
    require(setting_actions == expected_settings,
            f"setting actions differ: missing={expected_settings - setting_actions}, "
            f"unexpected={setting_actions - expected_settings}")
    require({"graphics", "enhancements", "controls", "haptics"} <= page_actions,
            "settings hub is missing a stable route identifier")

    graphics = ET.parse(ui_root / "pages" / "graphics.rml")
    graphics_controls = [element for element in graphics.iter("button")
                         if element.attrib.get("onclick", "").startswith("setting:")]
    require(len(graphics_controls) == 8,
            "graphics must expose one control per setting, not one button per value")
    for value_id in ("graphics-resolution", "graphics-supersampling", "graphics-aspect",
                     "graphics-hud", "graphics-refresh", "graphics-msaa", "graphics-hpfb",
                     "graphics-api"):
        require(any(element.attrib.get("id") == value_id for element in graphics.iter()),
                f"graphics control is missing current-value target: {value_id}")

    settings = ET.parse(ui_root / "settings.rml")
    unavailable = [element for element in settings.iter("button")
                   if element.attrib.get("disabled") == "disabled"]
    require(len(unavailable) == 1 and
            {element.attrib.get("onclick") for element in unavailable} ==
            {"page:haptics"},
            "only the unfinished Haptics page may remain disabled")

    controls = ET.parse(ui_root / "pages" / "controls.rml")
    controls_ids = {element.attrib.get("id") for element in controls.iter() if element.attrib.get("id")}
    required_control_ids = {
        "controls-controller-list", "controls-selected-name", "controls-selected-status",
        "controls-selected-layout", "controls-selected-guid", "controls-bindings",
        "controls-raw-preview", "controls-evaluated-preview", "controls-persistence-status",
        "controls-warnings", "controls-capture-modal", "controls-capture-message",
        "controls-conflict-modal",
    }
    require(required_control_ids <= controls_ids,
            f"Controls page is missing state targets: {required_control_ids - controls_ids}")
    control_actions = {element.attrib.get("onclick") for element in controls.iter()
                       if element.attrib.get("onclick", "").startswith("control:")}
    require({"control:reset-profile", "control:capture-cancel", "control:conflict-accept"}
            <= control_actions, "Controls page is missing modal/profile actions")
    controls_source = (repo / "src" / "ui" / "lambo_ui_controls.cpp").read_text(encoding="utf-8")
    require("Target::Count" in controls_source and "target_name(target)" in controls_source,
            "Controls rows must be generated from the complete typed target set")
    domain_source = (repo / "src" / "controls" / "lambo_controls.cpp").read_text(encoding="utf-8")
    for target in ("a", "b", "z", "start", "l", "r", "dpad_up", "dpad_down",
                   "dpad_left", "dpad_right", "c_up", "c_down", "c_left", "c_right",
                   "stick_x", "stick_y"):
        require(f'"{target}"' in domain_source,
                f"Controls generation does not cover target {target}")

    graphics_text = (ui_root / "pages" / "graphics.rml").read_text(encoding="utf-8")
    require("saved for the next launch" in graphics_text and "do not apply immediately" in graphics_text,
            "graphics API must not pretend to apply live")

    enhancements_text = (ui_root / "pages" / "enhancements.rml").read_text(encoding="utf-8")
    require(enhancements_text.index('class="help"') < enhancements_text.index('class="columns"'),
            "enhancement guidance must sit above both columns to keep their controls aligned")

    launcher = (ui_root / "launcher.rml").read_text(encoding="utf-8")
    require("v1.0.0" not in launcher, "launcher must not hardcode a release version")
    require('onclick="play:game"' in launcher,
            "Play must use the same explicit custom-event form as other launcher actions")

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
    semantic_rule = re.search(r"h1,\s*h2,\s*p,\s*\.summary,\s*\.footer\s*\{([^}]*)\}",
                              rcss, re.DOTALL)
    require(semantic_rule is not None and "display: block" in semantic_rule.group(1),
            "semantic text elements must not collapse into RmlUi's inline default")
    require(semantic_rule is not None and "width: 100%" in semantic_rule.group(1),
            "semantic text elements must fill the panel instead of using min-content width")
    columns_rule = re.search(r"\.columns\s*\{([^}]*)\}", rcss, re.DOTALL)
    require(columns_rule is not None and "width: 100%" in columns_rule.group(1),
            "settings columns must fill the panel before percentage children are resolved")
    panel_rule = re.search(r"\.panel\s*\{([^}]*)\}", rcss, re.DOTALL)
    require(panel_rule is not None and "overflow-y: auto" in panel_rule.group(1),
            "only overflowing settings pages should create a vertical scrollbar")
    scrollbar_rule = re.search(r"scrollbarvertical\s*\{([^}]*)\}", rcss, re.DOTALL)
    require(scrollbar_rule is not None and "width:" in scrollbar_rule.group(1),
            "RmlUi scrollbars need an explicit width or they consume the content area")
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
