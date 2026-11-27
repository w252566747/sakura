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
            raise AssertionError(f"Sakura did not open the requested document: {window.window_text()!r}")
        window.set_focus()
        if not window.is_visible() or not window.is_enabled():
            raise AssertionError("main Sakura window is not visible and enabled")
        hwnd = window.handle
        evidence["window_title"] = window.window_text()
        evidence["window_handle"] = hwnd
        width = max(640, int(screen_width * 0.92))
        height = max(480, int(screen_height * 0.92))
        window.move_window(x=0, y=0, width=width, height=height, repaint=True)
        time.sleep(1)
        resized_image = window.capture_as_image()
        resized_image.save(output / "01-resized.png")
        window.maximize()
        time.sleep(0.5)
        launch_image = window.capture_as_image()
        launch_image.save(output / "01-launch.png")
        if launch_image.width < 640 or launch_image.height < 400:
            raise AssertionError("captured Sakura window is unexpectedly small")
        if launch_image.width < screen_width * 0.85 or launch_image.height < screen_height * 0.85:
            raise AssertionError(
                f"maximized window capture is smaller than the active screen: "
                f"capture={launch_image.size}, screen={(screen_width, screen_height)}"
            )
        if max(ImageStat.Stat(launch_image.convert("RGB")).stddev) < 2:
            raise AssertionError("Sakura window screenshot is blank or uniform")
        record("launch_and_window_capture", width=launch_image.width, height=launch_image.height)

        # Leave an unsaved edit in the original document before opening the filter.
        window.set_focus()
        window.click_input(coords=(100, 447))
        window.type_keys("GUI_SMOKE_FILTER_SAVE", with_spaces=True)
        filter_saved = original + b"GUI_SMOKE_FILTER_SAVE"
        if sample.read_bytes() != original:
            raise AssertionError("the filter Ctrl+S scenario was saved before the filter was opened")

        window.click_input(coords=(30, 38))
        time.sleep(0.2)
        menu_image = window.capture_as_image()
        menu_image.save(output / "02-file-menu.png")
        menu_delta = ImageStat.Stat(
            ImageChops.difference(
                launch_image.convert("RGB").crop((0, 25, 450, 420)),
                menu_image.convert("RGB").crop((0, 25, 450, 420)),
            )
        ).sum
        if sum(menu_delta) < 5000:
            raise AssertionError("File menu click did not visibly open a menu")
        window.type_keys("{ESC}")
        filter_control = toolbar_button(window, 0x7CF6)  # REALTIME_FILTER_BUTTON
        if filter_control is None:
            raise AssertionError("real-time filter toolbar button was not found by command ID")
        filter_toolbar, filter_index = filter_control
        filter_state_before = toolbar_state(filter_toolbar, 0x7CF6)
        if not (filter_state_before & 0x04):
            raise AssertionError(
                f"real-time filter toolbar button is disabled: TB_GETSTATE=0x{filter_state_before:02x}"
            )
        click_toolbar_button(filter_toolbar, filter_index)
        filter_deadline = time.time() + 5
        while not visible_child_by_class(window, "SysListView32") and time.time() < filter_deadline:
            time.sleep(0.1)
        filter_state_after = toolbar_state(filter_toolbar, 0x7CF6)
        window.capture_as_image().save(output / "03-filter-toolbar-on.png")
        if not visible_child_by_class(window, "SysListView32"):
            raise AssertionError(
                "real filter toolbar click did not show the results list: "
                f"before=0x{filter_state_before:02x}, after=0x{filter_state_after:02x}, "
                f"button_rect={filter_toolbar.button(filter_index).rectangle()}"
            )
        window.type_keys("GUI_SMOKE_SELECTABLE_TOKEN", with_spaces=True)
        deadline = time.time() + 10
        stats_texts: list[str] = []
        while time.time() < deadline:
            stats_texts = visible_child_texts(window, "Static")
            if any("一致 1 行" in text and "表示 1 行" in text for text in stats_texts):
                break
            time.sleep(0.2)
        else:
            raise AssertionError(f"live filter did not report the single matching line: {stats_texts!r}")
        result_lists = [
            child for child in window.descendants()
            if child.class_name() == "SysListView32" and child.is_visible()
        ]
        if len(result_lists) != 1 or int(result_lists[0].send_message(0x1004)) != 1:
            raise AssertionError("live filter did not populate exactly one result row")
        filter_inputs = [
            child for child in window.descendants()
            if child.class_name() == "Edit" and child.is_visible()
            and child.window_text() == "GUI_SMOKE_SELECTABLE_TOKEN"
        ]
        if len(filter_inputs) != 1:
            raise AssertionError(
                "could not uniquely identify the live-filter query edit before testing Ctrl+S: "
                f"visible edit texts={visible_child_texts(window, 'Edit')!r}"
            )
        if sample.read_bytes() != original:
            raise AssertionError("activating and using the live filter changed the source document")
        filter_inputs[0].set_focus()
        time.sleep(0.1)
        window.type_keys("^s")
        deadline = time.time() + 10
        while sample.read_bytes() != filter_saved and time.time() < deadline:
            time.sleep(0.2)
        if sample.read_bytes() != filter_saved:
            raise AssertionError(
                "Ctrl+S while filtering did not save the complete original document: "
                f"expected {filter_saved!r}, got {sample.read_bytes()!r}"
            )
        window.capture_as_image().save(output / "04-filter-results.png")
        filter_control = toolbar_button(window, 0x7CF6)
        if filter_control is None:
            raise AssertionError("real-time filter toolbar button disappeared while filtering")
        filter_toolbar, filter_index = filter_control
        click_toolbar_button(filter_toolbar, filter_index)
        filter_deadline = time.time() + 5
        while visible_child_by_class(window, "SysListView32") and time.time() < filter_deadline:
            time.sleep(0.1)
        filter_off = window.capture_as_image()
        filter_off.save(output / "05-filter-toolbar-off.png")
        if visible_child_by_class(window, "SysListView32"):
            raise AssertionError("Filter toolbar second click did not hide the results list")
        if not window.is_visible():
            raise AssertionError("menu/toolbar input left the main window unavailable")
        record("file_menu_filter_and_ctrl_s_saves_full_original", expected_bytes=len(filter_saved),
               focused_filter_edit=filter_inputs[0].handle)

        # Exercise actual mouse input against the editor's client area.
        window.double_click_input(coords=(90, 223))
        window.type_keys("^c")
        hyphen_word = clipboard_text()
        if hyphen_word != "GUI-SMOKE-HYPHEN":
            raise AssertionError(f"custom hyphen boundary split a word: {hyphen_word!r}")

        window.double_click_input(coords=(90, 260))
        window.type_keys("^c")
        underscore_word = clipboard_text()
        if underscore_word != "GUI_SMOKE_BASELINE":
            raise AssertionError(f"custom underscore boundary split a word: {underscore_word!r}")
        window.click_input(coords=(90, 260))
        window.right_click_input(coords=(90, 260))
        time.sleep(0.2)
        window.capture_as_image().save(output / "06-context-menu.png")
        window.type_keys("{ESC}")
        window.double_click_input(coords=(90, 260))
        window.drag_mouse_input(src=(60, 260), dst=(160, 260))
        window.type_keys("^c")
        selected_text = clipboard_text()
        selection_image = window.capture_as_image()
        selection_image.save(output / "07-mouse-selection.png")
        if selected_text != "GUI_SMOKE_BASELINE":
            raise AssertionError(f"mouse drag did not select the intended full word: {selected_text!r}")
        marker_crop = selection_image.convert("RGB").crop((32, 90, 210, 108))
        warm_marker_pixels = sum(
            1 for red, green, blue in marker_crop.getdata()
            if red >= 180 and red - green >= 35 and green >= 45 and blue <= 110
        )
        if warm_marker_pixels < 12:
            raise AssertionError(
                "automatic same-word highlighting was not visible in the real editor screenshot: "
                f"warm marker pixels={warm_marker_pixels}"
            )
        record(
            "custom_word_boundaries_and_automatic_same_word_highlight",
            hyphen_double_click=hyphen_word,
            underscore_double_click=underscore_word,
            dragged_text=selected_text,
            warm_marker_pixels=warm_marker_pixels,
        )

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
        if edited != filter_saved + b"GUI_SMOKE_EDITED":
            raise AssertionError(f"keyboard edit changed unexpected source bytes: {edited!r}")
        window.capture_as_image().save(output / "08-edited.png")
        record("keyboard_edit_and_save", saved_bytes=len(edited))

        undo_count = 0
        undone = sample.read_bytes()
        for _ in range(len(b"GUI_SMOKE_EDITED") + 2):
            if undone == filter_saved:
                break
            previous = undone
            window.type_keys("^z")
            window.type_keys("^s")
            time.sleep(0.15)
            undone = sample.read_bytes()
            undo_count += 1
            if not undone.startswith(filter_saved) or len(undone) >= len(previous):
                raise AssertionError(f"Ctrl+Z did not progressively undo the appended text: {undone!r}")
        if undone != filter_saved:
            raise AssertionError(f"bounded Ctrl+Z sequence did not restore original bytes: {undone!r}")
        window.capture_as_image().save(output / "09-undo.png")
        record("undo_and_save", undo_steps=undo_count)

        for _ in range(undo_count):
            previous = sample.read_bytes()
            window.type_keys("^y")
            window.type_keys("^s")
            time.sleep(0.15)
            redone = sample.read_bytes()
            if not redone.startswith(previous) or len(redone) <= len(previous):
                raise AssertionError(f"Ctrl+Y did not progressively redo the text: {redone!r}")
        redone = sample.read_bytes()
        if redone != filter_saved + b"GUI_SMOKE_EDITED":
            raise AssertionError(f"bounded Ctrl+Y sequence did not restore the edited text: {redone!r}")
        window.capture_as_image().save(output / "10-redo.png")
        record("redo_and_save", redo_steps=undo_count)

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
        restarted.save(output / "11-restart.png")
        if b"GUI_SMOKE_EDITED" not in sample.read_bytes():
            raise AssertionError("saved document content was not preserved after restart")
        record("restart_and_reopen_saved_document", screenshot_size=list(restarted.size))
        window.close()
        app.wait_for_process_exit(timeout=20)
        record("close_after_restart")

        curl_sample = output / "gui-smoke.curl"
        curl_source = (
            '{TextField value = "ok"}\r\n'
            '{TextFeld value = "x"}\r\n'
            "{call (value]}\r\n"
        )
        curl_sample.write_text(curl_source, encoding="ascii", newline="")
        app = Application(backend="win32").start(
            f'"{exe}" "{curl_sample}"', timeout=30
        )
        window = app.top_window()
        window.wait("visible enabled", timeout=45)
        deadline = time.time() + 10
        while curl_sample.name.casefold() not in window.window_text().casefold() and time.time() < deadline:
            time.sleep(0.2)
            window = app.top_window()
        if curl_sample.name.casefold() not in window.window_text().casefold():
            raise AssertionError(f"Sakura did not open the Curl fixture: {window.window_text()!r}")
        window.set_focus()
        window.maximize()
        time.sleep(0.5)
        curl_open_image = window.capture_as_image()
        curl_open_image.save(output / "12-curl-open.png")
        curl_control = toolbar_button(window, 0x7CFF)
        if curl_control is None:
            raise AssertionError("Curl real-time syntax toolbar button was not found")
        curl_toolbar, curl_index = curl_control
        curl_state_before = toolbar_state(curl_toolbar, 0x7CFF)
        if not curl_state_before & 0x01:
            if not curl_state_before & 0x04:
                raise AssertionError(f"Curl toolbar button is natively disabled: TB_GETSTATE=0x{curl_state_before:02x}")
            click_toolbar_button(curl_toolbar, curl_index)
        deadline = time.time() + 10
        curl_state_after = toolbar_state(curl_toolbar, 0x7CFF)
        while not curl_state_after & 0x01 and time.time() < deadline:
            time.sleep(0.2)
            curl_state_after = toolbar_state(curl_toolbar, 0x7CFF)
        if not curl_state_after & 0x01:
            raise AssertionError(
                "Curl real-time syntax toolbar did not become checked after mouse click: "
                f"TB_GETSTATE before=0x{curl_state_before:02x}, after=0x{curl_state_after:02x}, "
                f"item_rect={curl_toolbar.button(curl_index).rectangle()}, "
                f"toolbar_rect={win32gui.GetWindowRect(curl_toolbar.handle)}"
            )
        deadline = time.time() + 10
        error_lists = [
            child for child in window.descendants()
            if child.class_name() == "SysListView32" and child.is_visible()
        ]
        while not error_lists and time.time() < deadline:
            time.sleep(0.2)
            error_lists = [
                child for child in window.descendants()
                if child.class_name() == "SysListView32" and child.is_visible()
            ]
        toolbar_error_rows = int(error_lists[0].send_message(0x1004)) if error_lists else 0
        manual_error_rows = 0
        if toolbar_error_rows == 0:
            # Diagnostic only: distinguish a toolbar notification failure from
            # a document/type/configuration problem without treating it as pass.
            window.send_message(0x111, 31571, 0)  # WM_COMMAND / F_CURL_SYNTAX_RUN
            deadline = time.time() + 10
            while time.time() < deadline:
                error_lists = [
                    child for child in window.descendants()
                    if child.class_name() == "SysListView32" and child.is_visible()
                ]
                if error_lists:
                    manual_error_rows = int(error_lists[0].send_message(0x1004))
                    break
                time.sleep(0.2)
        curl_syntax_image = window.capture_as_image()
        curl_syntax_image.save(output / "13-curl-realtime-syntax.png")
        if toolbar_error_rows < 2:
            raise AssertionError(
                "Curl real-time syntax did not show the expected two diagnostics: "
                f"toolbar_rows={toolbar_error_rows}, manual_command_rows={manual_error_rows}, "
                f"TB_GETSTATE before=0x{curl_state_before:02x}, after=0x{curl_state_after:02x}, "
                f"toolbar_hwnd={curl_toolbar.handle}, toolbar_rect={win32gui.GetWindowRect(curl_toolbar.handle)}"
            )
        record("curl_live_syntax_diagnostic_list", toolbar_checked=bool(curl_state_after & 0x01),
               diagnostic_rows=toolbar_error_rows)

        rainbow_control = toolbar_button(window, 0x7CFD)
        if rainbow_control is None:
            raise AssertionError("rainbow bracket toolbar button was not present")
        rainbow_toolbar, rainbow_index = rainbow_control
        rainbow_state = toolbar_state(rainbow_toolbar, 0x7CFD)
        if not rainbow_state & 0x01:
            if not rainbow_state & 0x04:
                raise AssertionError(f"Rainbow toolbar button is natively disabled: TB_GETSTATE=0x{rainbow_state:02x}")
            click_toolbar_button(rainbow_toolbar, rainbow_index)
            time.sleep(0.5)
            rainbow_state = toolbar_state(rainbow_toolbar, 0x7CFD)
        if not rainbow_state & 0x01:
            raise AssertionError("rainbow bracket toolbar did not become checked after mouse click")
        rainbow_image = window.capture_as_image()
        rainbow_image.save(output / "14-rainbow-brackets.png")
        bracket_pixels = [
            rainbow_image.convert("RGB").crop(box).getdata()
            for box in ((32, 90, 48, 108), (186, 90, 202, 108))
        ]
        saturated_bracket_pixels = sum(
            1 for pixels in bracket_pixels for red, green, blue in pixels
            if max(red, green, blue) - min(red, green, blue) >= 80
        )
        if saturated_bracket_pixels < 4:
            raise AssertionError(
                "rainbow bracket colors were not visible on the real Curl source screenshot: "
                f"saturated pixels={saturated_bracket_pixels}"
            )
        record("rainbow_bracket_toolbar_and_color_pixels", toolbar_checked=bool(rainbow_state & 0x01),
               saturated_bracket_pixels=saturated_bracket_pixels)
        window.close()
        app.wait_for_process_exit(timeout=20)
        record("close_curl_test_application")
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
