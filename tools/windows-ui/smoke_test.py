#!/usr/bin/env python3
"""Exercise a real Sakura Win32 window and retain screenshots and evidence."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

import win32gui
from PIL import ImageStat
from pywinauto import Application


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
        + ["GUI_SMOKE_SELECTABLE_TOKEN line 09"]
        + [f"GUI_SMOKE_BASELINE line {line:02d}" for line in range(10, 25)]
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
    steps: list[dict[str, object]] = evidence["steps"]  # type: ignore[assignment]

    def record(name: str, **values: object) -> None:
        steps.append({"step": name, "result": "passed", **values})

    app: Application | None = None
    try:
        app = Application(backend="win32").start(
            f'"{exe}" "{sample}"', timeout=30
        )
        window = app.top_window()
        window.wait("visible enabled", timeout=45)
        window.set_focus()
        if not window.is_visible() or not window.is_enabled():
            raise AssertionError("main Sakura window is not visible and enabled")
        hwnd = window.handle
        evidence["window_title"] = window.window_text()
        evidence["window_handle"] = hwnd
        window.move_window(x=40, y=40, width=1280, height=820, repaint=True)
        time.sleep(1)
        launch_image = window.capture_as_image()
        launch_image.save(output / "01-launch.png")
        if launch_image.width < 640 or launch_image.height < 400:
            raise AssertionError("captured Sakura window is unexpectedly small")
        if max(ImageStat.Stat(launch_image.convert("RGB")).stddev) < 2:
            raise AssertionError("Sakura window screenshot is blank or uniform")
        record("launch_and_window_capture", width=launch_image.width, height=launch_image.height)

        # Exercise actual mouse input against the editor's client area.
        window.click_input(coords=(500, 260))
        window.right_click_input(coords=(500, 260))
        time.sleep(0.2)
        window.type_keys("{ESC}")
        window.double_click_input(coords=(500, 260))
        window.drag_mouse_input(src=(480, 260), dst=(620, 260), duration=0.3)
        record("mouse_left_right_double_click_and_drag")

        window.set_focus()
        window.type_keys("^{END}")
        window.type_keys("GUI_SMOKE_EDITED", with_spaces=True)
        window.type_keys("^s")
        deadline = time.time() + 10
        edited = sample.read_bytes()
        while b"GUI_SMOKE_EDITED" not in edited and time.time() < deadline:
            time.sleep(0.2)
            edited = sample.read_bytes()
        if b"GUI_SMOKE_EDITED" not in edited:
            raise AssertionError("keyboard input and Ctrl+S did not update the opened file")
        window.capture_as_image().save(output / "02-edited.png")
        record("keyboard_edit_and_save", saved_bytes=len(edited))

        window.type_keys("^z")
        window.type_keys("^s")
        time.sleep(0.5)
        undone = sample.read_bytes()
        if undone != original:
            raise AssertionError(f"Ctrl+Z did not restore original bytes: {undone!r}")
        window.capture_as_image().save(output / "03-undo.png")
        record("undo_and_save")

        window.type_keys("^y")
        window.type_keys("^s")
        time.sleep(0.5)
        redone = sample.read_bytes()
        if b"GUI_SMOKE_EDITED" not in redone:
            raise AssertionError("Ctrl+Y did not restore the edited text")
        window.capture_as_image().save(output / "04-redo.png")
        record("redo_and_save")

        window.close()
        app.wait_for_process_exit(timeout=20)
        record("close_application")

        app = Application(backend="win32").start(
            f'"{exe}" "{sample}"', timeout=30
        )
        window = app.top_window()
        window.wait("visible enabled", timeout=45)
        window.set_focus()
        restarted = window.capture_as_image()
        restarted.save(output / "05-restart.png")
        if b"GUI_SMOKE_EDITED" not in sample.read_bytes():
            raise AssertionError("saved document content was not preserved after restart")
        record("restart_and_reopen_saved_document", screenshot_size=list(restarted.size))
        window.close()
        app.wait_for_process_exit(timeout=20)
        record("close_after_restart")
        evidence["status"] = "passed"
        return 0
    except Exception as exc:  # retain failure evidence from the real runner
        evidence["status"] = "failed"
        evidence["error"] = f"{type(exc).__name__}: {exc}"
        if app is not None:
            try:
                window = app.top_window()
                evidence["failure_window_title"] = window.window_text()
                window.capture_as_image().save(output / "failure-window.png")
            except Exception as capture_exc:
                evidence["failure_capture_error"] = str(capture_exc)
        raise
    finally:
        (output / "ui-smoke.json").write_text(
            json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        if app is not None:
            try:
                app.kill()
            except Exception:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
