#!/usr/bin/env python3
"""Exercise a real Sakura Win32 window and retain screenshots and evidence."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import win32api
import win32clipboard
import win32con
import win32gui
from PIL import ImageChops
from PIL import ImageStat
from pywinauto import Application, mouse


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    exe = args.exe.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sample = output / "gui-smoke.txt"
    original_text = "\r\n".join(
        [f"GUI_SMOKE_BASELINE line {line:02d}" for line in range(1, 9)]
        + ["GUI_SMOKE_SELECTABLE_TOKEN line 09", "GUI-SMOKE-HYPHEN line 10"]
        + [f"GUI_SMOKE_BASELINE line {line:02d}" for line in range(11, 26)]
    ) + "\r\n"
    original = original_text.encode("ascii")
    sample.write_bytes(original)
    evidence: dict[str, object] = {
        "executable": str(exe),
        "executable_bytes": exe.stat().st_size,
        "python": sys.version,
        "interactive_station": os.environ.get("SESSIONNAME", "unknown"),
        "steps": [],
    }
    screen_width = win32api.GetSystemMetrics(0)
    screen_height = win32api.GetSystemMetrics(1)
    evidence["screen_resolution"] = [screen_width, screen_height]
    steps: list[dict[str, object]] = evidence["steps"]  # type: ignore[assignment]

    def record(name: str, **values: object) -> None:
        steps.append({"step": name, "result": "passed", **values})

    def visible_child_by_class(window, class_name: str) -> bool:
        return any(
            child.class_name() == class_name and child.is_visible()
            for child in window.descendants()
        )

    def visible_child_texts(window, class_name: str) -> list[str]:
        return [
            child.window_text()
            for child in window.descendants()
            if child.class_name() == class_name and child.is_visible()
        ]

    def toolbar_button(window, command_id: int):
        for child in window.descendants():
            if child.class_name() != "ToolbarWindow32" or not child.is_visible():
                continue
            index = int(child.send_message(0x419, command_id))  # TB_COMMANDTOINDEX
            if index >= 0:
                return child, index
        return None

    def toolbar_state(toolbar, command_id: int) -> int:
        return int(toolbar.send_message(0x412, command_id))  # TB_GETSTATE (WM_USER + 18)

    def click_toolbar_button(toolbar, index: int) -> None:
        # The generic ButtonWrapper exposes the toolbar HWND state, not fsState.
        item = toolbar.button(index).rectangle()
        owner = win32gui.GetWindowRect(toolbar.handle)
        x = owner[0] + (item.left + item.right) // 2
        y = owner[1] + (item.top + item.bottom) // 2
        mouse.click(button="left", coords=(x, y))

    def clipboard_text() -> str:
        win32clipboard.OpenClipboard()
        try:
            if not win32clipboard.IsClipboardFormatAvailable(win32con.CF_UNICODETEXT):
                return ""
            return str(win32clipboard.GetClipboardData(win32con.CF_UNICODETEXT))
        finally:
            win32clipboard.CloseClipboard()

    app: Application | None = None
    try:
        app = Application(backend="win32").start(
            f'"{exe}" "{sample}"', timeout=30
        )
        window = app.top_window()
        window.wait("visible enabled", timeout=45)
        deadline = time.time() + 10
        while sample.name.casefold() not in window.window_text().casefold() and time.time() < deadline:
            time.sleep(0.2)
            window = app.top_window()
        if sample.name.casefold() not in window.window_text().casefold():
            raise AssertionError(f"Sakura did not open the