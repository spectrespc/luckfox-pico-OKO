#!/usr/bin/env python3
"""Render the divinus WebUI in headless Chromium and check the new layout.

Requires Playwright with a Chromium build:

    python3 -m venv /tmp/pw && /tmp/pw/bin/pip install playwright
    /tmp/pw/bin/playwright install chromium
    /tmp/pw/bin/python bench/ui_check.py
"""
import os
import re
import sys

from playwright.sync_api import sync_playwright

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.97")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
SHOTS = os.path.join("output", "bench", "ui")

SECTIONS = ["live", "image", "streams", "events", "storage", "network", "osd", "system"]


def check_fit(page, label, failures):
    """The shell is one viewport tall on every tab: only #content scrolls."""
    for sec in SECTIONS:
        page.click('#sidebar button[data-sec="%s"]' % sec)
        page.wait_for_timeout(350)
        horizontal = page.evaluate(
            "() => document.documentElement.scrollWidth - window.innerWidth")
        vertical = page.evaluate(
            "() => document.body.scrollHeight - window.innerHeight")
        if horizontal > 1:
            failures.append("%s %s: horizontal page overflow %d" %
                            (label, sec, horizontal))
        if vertical > 1:
            failures.append("%s %s: vertical page overflow %d" %
                            (label, sec, vertical))
    print("%s fit: checked %d tabs" % (label, len(SECTIONS)))


def main():
    os.makedirs(SHOTS, exist_ok=True)
    failures = []
    console_errors = []
    page_errors = []

    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(
            http_credentials={"username": USER, "password": PASSWORD},
            viewport={"width": 1440, "height": 900})
        page = context.new_page()
        page.on("console", lambda msg: console_errors.append(msg.text)
                if msg.type == "error" else None)
        page.on("pageerror", lambda err: page_errors.append(str(err)))

        page.goto("http://%s/" % HOST, wait_until="load")
        page.wait_for_timeout(2500)

        if page.title() != "Divinus":
            failures.append("unexpected title %r" % page.title())

        # The status bar is filled from /api/status.
        ip = page.inner_text("#sb_ip")
        if ip == "–":
            failures.append("status bar IP is not populated")
        print("status bar IP:", ip)

        # Navigation: eight buttons, each switches the visible section.
        nav = page.locator("#sidebar button")
        if nav.count() != 8:
            failures.append("expected 8 navigation buttons, got %d" % nav.count())
        for sec in SECTIONS:
            page.click('#sidebar button[data-sec="%s"]' % sec)
            page.wait_for_timeout(1200)
            visible = page.is_visible("#sec-" + sec)
            if not visible:
                failures.append("section %s did not become visible" % sec)
            page.screenshot(path=os.path.join(SHOTS, "%s.png" % sec), full_page=True)
            print("section %-8s visible=%s" % (sec, visible))

        # Live: the focus assist card moved here, the ROI box lives inside the
        # preview and the stream starts without a click.
        page.click('#sidebar button[data-sec="live"]')
        page.wait_for_timeout(1500)
        for selector in ("#preview", "#focus-box", "#focus_enable",
                         "#focus-metric", "#focus-state", "#focus-save-btn",
                         "#focus-select-btn", "#af-state", "#af-fine-btn"):
            if page.locator(selector).count() == 0:
                failures.append("live focus element %s is missing" % selector)
        if page.locator("#focus_motor").count() != 0:
            failures.append("#focus_motor must be gone (the driver arms itself)")
        if not page.evaluate(
                "() => document.getElementById('preview')"
                ".contains(document.getElementById('focus-box'))"):
            failures.append("#focus-box is not inside #preview")

        # Desktop: Live is two columns, the focus panel sits to the right of
        # the preview card.
        preview_box = page.locator("#preview-card").bounding_box()
        focus_box = page.locator("#focus-card").bounding_box()
        print("desktop columns: preview right %.0f, focus left %.0f" %
              (preview_box["x"] + preview_box["width"], focus_box["x"]))
        if focus_box["x"] < preview_box["x"] + preview_box["width"] - 5:
            failures.append("Live is not two columns at 1440px")

        # Auto-start cascade: a video element, an MJPEG image or a snapshot
        # must appear without pressing a button (headless Chromium may have no
        # H.265, in which case the fallback is expected).
        media = False
        for _ in range(16):
            media = page.evaluate(
                "() => { var p = document.getElementById('preview');"
                " return !!(p && p.querySelector('video, img')); }")
            if media:
                break
            page.wait_for_timeout(500)
        if not media:
            failures.append("the preview did not start by itself")
        mode_badge = (page.text_content("#preview-mode") or "").strip()
        print("preview mode badge:", mode_badge)
        if page.locator("#preview-mode").count() == 0:
            failures.append("the #preview-mode badge is missing")
        elif mode_badge not in ("MP4 · main", "MJPEG · separate channel",
                                "still · JPEG", "stopped"):
            failures.append("unexpected preview mode badge: %r" % mode_badge)
        elif media:
            media_kind = page.evaluate(
                "() => { var p = document.getElementById('preview');"
                " var m = p && p.querySelector('video, img');"
                " return m ? (m.tagName === 'IMG' ? (m.alt || '') : 'video') : ''; }")
            if media_kind == "MJPEG" and "MJPEG" not in mode_badge:
                failures.append("the badge does not report the MJPEG channel")
            if media_kind == "video" and "MP4" not in mode_badge:
                failures.append("the badge does not report the MP4 channel")
        page.screenshot(path=os.path.join(SHOTS, "live-focus.png"), full_page=True)

        # Manual lens panel: step/speed selectors, jog buttons, readout and the
        # home/zero job controls.
        for selector in ("#manual_step", "#manual_speed", "#manual-focus-dec",
                         "#manual-focus-inc", "#manual-zoom-dec",
                         "#manual-zoom-inc", "#manual-stop-btn",
                         "#manual-readout", "#home-focus-btn",
                         "#home-zoom-btn", "#zero-focus-btn",
                         "#zero-zoom-btn", "#home-state-note"):
            if page.locator(selector).count() == 0:
                failures.append("manual lens element %s is missing" % selector)
        steps = page.eval_on_selector_all("#manual_step option",
            "els => els.map(e => e.value)")
        speeds = page.eval_on_selector_all("#manual_speed option",
            "els => els.map(e => e.value)")
        if steps != ["1", "10", "50", "200"]:
            failures.append("unexpected step options: %r" % steps)
        if speeds[:5] != ["50", "100", "200", "300", "400"]:
            failures.append("unexpected speed options: %r" % speeds)
        if any(not v.isdigit() for v in speeds[5:]):
            failures.append("unexpected extra speed options: %r" % speeds[5:])
        if page.input_value("#manual_step") != "50":
            failures.append("the step selector does not default to 50")
        page.evaluate("focusPoll()")
        page.wait_for_timeout(1200)
        cfg_speed = page.evaluate(
            "() => (focusState && focusState.motor && "
            "focusState.motor.speed) || null")
        if cfg_speed is not None and \
           page.input_value("#manual_speed") != str(cfg_speed):
            failures.append("the speed selector does not show focus.speed %r"
                            % cfg_speed)
        readout = page.inner_text("#manual-readout")
        print("manual readout:", readout)
        if "focus:" not in readout or "zoom:" not in readout:
            failures.append("manual readout is not populated: %r" % readout)
        movable = bool(page.evaluate(
            "() => { var s = focusState; return !!(s && s.motor && "
            "s.motor.present && !s.motor.bootloader); }"))
        if not movable:
            if not page.is_disabled("#manual-focus-dec") or \
               not page.is_disabled("#home-focus-btn"):
                failures.append("manual buttons are enabled without a motor")
        if movable and os.environ.get("OKO_FOCUS_MOTOR_TESTS") == "1":
            before = readout
            page.click("#manual-zoom-inc")
            page.wait_for_timeout(1500)
            page.evaluate("focusPoll()")
            page.wait_for_timeout(800)
            after = page.inner_text("#manual-readout")
            print("manual readout after zoom +50:", after)
            if after == before:
                failures.append("zoom jog did not update the readout")
        # Opt-in: a running search must lock the manual/home/zero buttons but
        # keep STOP reachable (it cancels the search).
        if movable and os.environ.get("OKO_FOCUS_AF_TESTS") == "1":
            page.evaluate("focusPost('enable=1&motor=1&af=1')")
            page.wait_for_timeout(900)
            page.evaluate("focusPoll()")
            page.wait_for_timeout(400)
            running = page.evaluate(
                "() => { var s = focusState; return !!(s && s.af && "
                "s.af.state === 'running'); }")
            print("af running in the UI:", running)
            if running:
                if not page.is_disabled("#manual-focus-dec") or \
                   not page.is_disabled("#home-focus-btn") or \
                   not page.is_disabled("#af-fine-btn"):
                    failures.append("manual buttons stay enabled during AF")
                if page.is_disabled("#manual-stop-btn"):
                    failures.append("STOP is disabled during AF")
            page.evaluate("focusPost('af=cancel&stop=1&motor=0&enable=0')")
            page.wait_for_timeout(800)

        # Opt-in: the Autofocus button runs the micro-fine scope and is
        # locked like the other controls while that job runs.
        if movable and os.environ.get("OKO_FOCUS_AF_TESTS") == "1":
            page.locator("#af-fine-btn").scroll_into_view_if_needed()
            page.wait_for_timeout(300)
            fine_check = (
                "() => { var s = focusState; return !!(s && s.af && "
                "(s.af.state === 'running' || s.af.state === 'done') && "
                "s.af.scope === 'fine'); }")
            page.click("#af-fine-btn")
            page.wait_for_timeout(900)
            page.evaluate("focusPoll()")
            page.wait_for_timeout(400)
            fine = page.evaluate(fine_check)
            if not fine:
                # The cancelled worker may still have been unwinding.
                page.click("#af-fine-btn")
                page.wait_for_timeout(900)
                page.evaluate("focusPoll()")
                page.wait_for_timeout(400)
                fine = page.evaluate(fine_check)
            print("Autofocus button af=fine:", fine)
            if not fine:
                failures.append("the Autofocus button did not start af=fine")
            if page.evaluate("() => { var s = focusState; return !!(s && "
                             "s.af && s.af.state === 'running'); }"):
                if not page.is_disabled("#af-fine-btn"):
                    failures.append("the Autofocus button stays enabled "
                                    "during AF")
            page.evaluate("focusPost('af=cancel&stop=1&motor=0')")
            page.wait_for_timeout(800)
        # Zone selection is two-step: dragging outside the mode only shows a
        # hint, "Select zone" enters the mode, a tiny rectangle is discarded,
        # and a real rectangle stays pending until Apply or Escape.
        page.locator("#preview").scroll_into_view_if_needed()
        page.wait_for_timeout(300)
        box = page.evaluate(
            "() => { var m = document.querySelector('#preview video, #preview img');"
            " if (!m) return null; var r = m.getBoundingClientRect();"
            " return {x: r.left + r.width / 2, y: r.top + r.height / 2}; }")
        if box:
            x = min(max(box["x"], 10), 1430)
            y = min(max(box["y"], 80), 860)
            # Outside the mode: the drag must not create a pending zone.  An applied
            # ROI may legitimately stay visible, so only the class is checked.
            page.mouse.move(x, y)
            page.mouse.down()
            page.mouse.move(x + 80, y + 60, steps=4)
            outside = page.evaluate(
                "() => document.getElementById('focus-box').className")
            page.mouse.up()
            print("focus box class on a drag outside the mode:", outside)
            if "pending" in outside:
                failures.append("a drag outside the selection mode drew a box")

            label = page.text_content("#focus-select-btn").strip()
            print("select-zone button:", label)
            if label not in ("Select zone", "Выделить зону"):
                failures.append("unexpected select-zone label: %r" % label)
            page.click("#focus-select-btn")
            page.wait_for_timeout(200)
            armed = page.text_content("#focus-select-btn").strip()
            if armed not in ("Cancel", "Отмена"):
                failures.append("the select-zone button did not switch to Cancel")

            # A tiny rectangle stays below the 0.03 threshold and is dropped; the
            # applied ROI, if any, is redrawn instead of a pending zone.
            page.mouse.move(x, y)
            page.mouse.down()
            page.mouse.move(x + 2, y + 2, steps=2)
            drawn = page.evaluate(
                "() => document.getElementById('focus-box').style.display")
            page.mouse.up()
            after = page.evaluate(
                "() => document.getElementById('focus-box').className")
            print("roi box during/after tiny drag:", drawn, after)
            if drawn != "block":
                failures.append("the ROI drag did not draw the focus box")
            if "pending" in after:
                failures.append("a tiny ROI drag kept the focus box")

            # A real rectangle becomes a pending zone; it is not applied yet.
            page.mouse.move(x, y)
            page.mouse.down()
            page.mouse.move(x + 120, y + 90, steps=4)
            page.mouse.up()
            pending = page.evaluate(
                "() => document.getElementById('focus-box').className")
            print("focus box after a real drag:", pending)
            if "pending" not in pending:
                failures.append("a real drag did not mark the pending zone")

            page.keyboard.press("Escape")
            page.wait_for_timeout(200)
            pending = page.evaluate(
                "() => document.getElementById('focus-box').className")
            print("focus box after Escape:", pending)
            if "pending" in pending:
                failures.append("Escape did not cancel the pending zone")
            if page.text_content("#focus-select-btn").strip() not in \
                    ("Select zone", "Выделить зону"):
                failures.append("Escape did not leave the selection mode")

            # The focus card participates in the shared Apply/dirty model: an
            # unsaved "Enable" must not be overwritten by the 2 s poll.
            page.wait_for_timeout(2200)
            current = page.evaluate(
                "() => document.getElementById('focus_enable').value")
            target = "false" if current == "true" else "true"
            page.select_option("#focus_enable", target)
            page.wait_for_timeout(300)
            save_btn = page.locator("#focus-save-btn")
            dirty_dot = page.locator(
                '#sidebar button[data-sec="live"].dirty').count()
            print("focus dirty dot:", dirty_dot,
                  "save disabled:", save_btn.is_disabled())
            if save_btn.is_disabled():
                failures.append("focus Apply stayed disabled after an edit")
            if not dirty_dot:
                failures.append(
                    "navigation dot did not appear for a dirty focus card")
            page.wait_for_timeout(3200)
            kept = page.evaluate(
                "() => document.getElementById('focus_enable').value")
            print("focus enable after a poll cycle:", kept)
            if kept != target:
                failures.append(
                    "the poll overwrote the unsaved focus enable value")
            page.evaluate("() => focusApplyValues(focusState || {}, true)")
            page.wait_for_timeout(200)
            if page.locator('#sidebar button[data-sec="live"].dirty').count():
                failures.append(
                    "the focus dirty dot did not clear after the revert")
        else:
            failures.append("no preview media element to drag the ROI on")
        page.screenshot(path=os.path.join(SHOTS, "live-focus.png"), full_page=True)

        # Exercise the card's POST path (mutations are POST-only).
        page.evaluate("focusPost('enable=1')")
        page.wait_for_timeout(1500)
        enabled = page.evaluate(
            "fetch('/api/focus').then(function (r) { return r.json(); })"
            ".then(function (d) { return d.enable; })")
        if enabled is not True:
            failures.append("focusPost did not enable the focus thread")
        page.evaluate("focusPost('enable=0')")
        page.wait_for_timeout(500)

        # System: controller firmware card and flash form.
        page.click('#sidebar button[data-sec="system"]')
        page.wait_for_timeout(1500)
        for selector in ("#fw-versions", "#flash_target",
                         "#flash_file", "#flash_confirm", "#flash_start"):
            if page.locator(selector).count() == 0:
                failures.append("firmware element %s is missing" % selector)
        page.screenshot(path=os.path.join(SHOTS, "system-firmware.png"), full_page=True)

        # Status card: the SD capacity and free rows sit next to Memory and are
        # formatted from /api/status.  The expectation is recomputed from the
        # page's own cached answer, so the check works with or without a card.
        page.evaluate("apiRead('status')")
        page.wait_for_timeout(700)
        st = page.evaluate("() => window.lastStatus || {}")
        total_mb = st.get("sd_total_mb") or 0
        free_mb = st.get("sd_free_mb") or 0
        expected_total = page.evaluate("mb => fmtMiB(mb)", total_mb)
        expected_free = page.evaluate("mb => fmtMiB(mb)", free_mb)
        total_text = (page.text_content("#status_sd_total_mb") or "").strip()
        free_text = (page.text_content("#status_sd_free_mb") or "").strip()
        print("SD summary:", total_text, "/", free_text,
              "(raw %s/%s MiB)" % (total_mb, free_mb))
        if page.locator("#status_sd_total_mb").count() == 0:
            failures.append("SD capacity summary is missing in System")
        if page.locator("#status_sd_free_mb").count() == 0:
            failures.append("SD free summary is missing in System")
        if total_text != expected_total or free_text != expected_free:
            failures.append("SD summary does not match /api/status: %r/%r vs %r/%r"
                            % (total_text, free_text, expected_total, expected_free))
        if total_mb > 0:
            if not re.match(r"^\d+(\.\d+)? (GiB|MiB)$", total_text):
                failures.append("unexpected SD capacity format: %r" % total_text)
            if free_mb > total_mb:
                failures.append("SD free space exceeds the card capacity")
        elif total_text != "–":
            failures.append("an unmounted card must render a dash, got %r" % total_text)

        # Events: the NPU settings card and the model table are rendered.
        page.click('#sidebar button[data-sec="events"]')
        page.wait_for_timeout(1500)
        if not page.is_visible("#yolo_enable"):
            failures.append("YOLO settings card is missing in Events")
        options = page.eval_on_selector_all("#yolo_source option",
            "els => els.map(e => e.value)")
        print("yolo source options:", options)
        if "vi1" in options:
            failures.append("the UI still offers yolo.source=vi1")
        if page.locator("#models_table").count() == 0:
            failures.append("model table is missing in Events")
        else:
            print("models table text:", page.inner_text("#models_table")[:100].replace("\n", " "))
        page.screenshot(path=os.path.join(SHOTS, "events-models.png"), full_page=True)

        # Model upload/activate/delete through the UI.  The synthetic file is
        # stored but cannot be loaded by rknn_init: the error toast must show
        # the server reason and the row must still be deletable.
        fake = os.path.join(SHOTS, "ui-test.rknn")
        with open(fake, "wb") as fh:
            fh.write(b"not a real rknn model" * 64)
        page.set_input_files("#model_file", fake)
        page.click("button[onclick='uploadModel()']")
        page.wait_for_timeout(1800)
        table = page.inner_text("#models_table")
        if "ui-test.rknn" not in table:
            failures.append("model upload through the UI did not list ui-test.rknn")
        row = page.locator("#models_table div").filter(has_text="ui-test.rknn").first
        if row.count():
            row.locator("button").first.click()
            page.wait_for_timeout(1800)
            toast = page.inner_text("#toasts") if page.locator("#toasts .toast").count() else ""
            print("activation error toast:", toast.replace("\n", " ")[:100])
            if "rknn_init failed" not in toast:
                failures.append("activation error toast is missing the rknn_init reason")
            page.once("dialog", lambda dialog: dialog.accept())
            row = page.locator("#models_table div").filter(has_text="ui-test.rknn").first
            if row.count():
                row.locator("button").nth(1).click()
                page.wait_for_timeout(1500)
            if "ui-test.rknn" in page.inner_text("#models_table"):
                failures.append("model delete through the UI did not remove ui-test.rknn")
        else:
            failures.append("uploaded model row is missing")
        os.unlink(fake)
        page.screenshot(path=os.path.join(SHOTS, "events-models.png"), full_page=True)

        # Telemetry renders tiles when the daemon writes fresh data.
        page.click('#sidebar button[data-sec="live"]')
        page.wait_for_timeout(2500)
        tiles = page.locator("#telemetry .tel").count()
        print("telemetry tiles:", tiles)
        if tiles == 0:
            failures.append("telemetry card is empty")

        # MJPEG preview, when the stream is enabled.
        mjpeg_on = page.evaluate(
            "() => document.getElementById('mjpeg_enable').value")
        if mjpeg_on == "true":
            page.click('button[onclick="setSource(\'mjpeg\')"]')
            page.wait_for_timeout(3000)
            painted = page.evaluate(
                "() => { var i = document.querySelector('#preview img');"
                " return !!i && i.naturalWidth > 0; }")
            print("mjpeg preview painted:", painted)
            if not painted:
                failures.append("MJPEG preview did not paint a frame")
        else:
            print("mjpeg preview: skipped (stream disabled)")

        # Leaving Live releases the persistent stream: only the idle text
        # remains in the preview container.
        page.click('#sidebar button[data-sec="image"]')
        page.wait_for_timeout(1200)
        idle = page.evaluate(
            "() => { var p = document.getElementById('preview');"
            " return !!p && !!p.querySelector('p')"
            " && !p.querySelector('video, img'); }")
        print("preview idle after leaving live:", idle)
        if not idle:
            failures.append("the preview stream was not released on leave")

        # Dirty tracking: edit a field, the Apply button enables and the
        # navigation shows a dot; applying it disables the button again.
        page.click('#sidebar button[data-sec="image"]')
        page.wait_for_timeout(1200)
        mirror = page.locator("#isp_mirror")
        original = mirror.input_value()
        page.select_option("#isp_mirror", "true" if original == "false" else "false")
        page.wait_for_timeout(300)
        apply_btn = page.locator('[data-apply="isp"]')
        if apply_btn.is_disabled():
            failures.append("Apply button stayed disabled after an edit")
        if not page.locator('#sidebar button[data-sec="image"].dirty').count():
            failures.append("navigation dot did not appear for a dirty section")
        apply_btn.click()
        page.wait_for_timeout(1500)
        if not apply_btn.is_disabled():
            failures.append("Apply button stayed enabled after applying")
        page.select_option("#isp_mirror", original)
        page.click('[data-apply="isp"]')
        page.wait_for_timeout(1200)
        if page.locator("#isp_mirror").input_value() != original:
            failures.append("ISP mirror did not come back after the revert")

        # Gating error: HDR is refused on SC230AI, the UI must show the reason.
        page.click('#sidebar button[data-sec="image"]')
        page.wait_for_timeout(1200)
        page.select_option("#hdr_mode", "hdr2")
        page.click('[data-apply="hdr"]')
        page.wait_for_timeout(1800)
        toast = page.inner_text("#toasts") if page.locator("#toasts .toast").count() else ""
        print("gating toast:", toast.replace("\n", " ")[:80])
        if "HDR" not in toast or ("HDR is not supported" not in toast and
                                  "cannot be combined" not in toast):
            failures.append("gating error toast is missing the server reason")
        page.evaluate("apiRead('hdr')")
        page.wait_for_timeout(800)

        # OSD regions are generated at startup.
        page.click('#sidebar button[data-sec="osd"]')
        page.wait_for_timeout(800)
        regions = page.locator(".osd-region").count()
        print("OSD regions:", regions)
        if regions != 10:
            failures.append("expected 10 OSD regions, got %d" % regions)
        page.click("#osd-region-0 .osd-head")
        page.wait_for_timeout(300)
        if not page.locator('[id="osd/0_text"]').is_visible():
            failures.append("OSD region did not expand")

        # VPN card in the Network section: profile selector, type selector,
        # paste area and the import buttons must be rendered.
        page.click('#sidebar button[data-sec="network"]')
        page.wait_for_timeout(1500)
        for selector in ("#vpn-card", "#vpn_profile", "#vpn_type",
                         "#vpn_text", "#vpn_file", "#vpn-connect",
                         "#vpn-disconnect", "#vpn-auto", "#vpn-delete",
                         "#vpn-live"):
            if page.locator(selector).count() == 0:
                failures.append("VPN element %s is missing" % selector)
        vpn_types = page.eval_on_selector_all("#vpn_type option",
            "els => els.map(e => e.value)")
        if vpn_types != ["auto", "wg", "awg"]:
            failures.append("unexpected VPN type options: %r" % vpn_types)
        if not page.is_visible("#vpn-card"):
            failures.append("VPN card is not visible in Network")
        page.screenshot(path=os.path.join(SHOTS, "network-vpn.png"), full_page=True)

        # Archive and storage fields are populated.
        page.click('#sidebar button[data-sec="storage"]')
        page.wait_for_timeout(1500)
        total = page.inner_text("#storage_total_mb")
        print("storage total:", total)
        if total.strip() in ("", "–"):
            failures.append("storage capacity is not populated")

        # Language toggle.
        page.click("#lang-ru")
        page.wait_for_timeout(500)
        label = page.inner_text('#sidebar button[data-sec="live"]')
        print("RU nav label:", label)
        if "Видео" not in label:
            failures.append("language toggle did not switch to RU")
        vpn_label = page.text_content("#vpn-connect") or ""
        if "Подключить" not in vpn_label:
            failures.append("VPN controls did not switch to RU")
        home_label = page.text_content("#home-focus-btn") or ""
        if "Упор" not in home_label:
            failures.append("manual lens controls did not switch to RU")
        page.click("#lang-en")
        page.wait_for_timeout(300)

        # Layout: no horizontal overflow on desktop, untouched Apply buttons
        # disabled, and every tab fits in the window (only #content scrolls).
        overflow = page.evaluate(
            "() => document.documentElement.scrollWidth - window.innerWidth")
        print("desktop overflow:", overflow)
        if overflow > 1:
            failures.append("horizontal overflow at 1440px: %d" % overflow)
        check_fit(page, "desktop 1440x900", failures)

        enabled = page.evaluate(
            "() => Array.from(document.querySelectorAll('[data-apply]'))"
            ".filter(b => !b.disabled && b.getAttribute('data-apply').indexOf('osd-') !== 0).length")
        print("enabled apply buttons after refresh:", enabled)
        if enabled:
            failures.append("%d Apply buttons are enabled without edits" % enabled)

        # Mobile: the automatic breakpoint moves the navigation into a bottom
        # 4x2 grid with short labels, drops the noisy status fields and keeps
        # every tab inside one viewport.
        page.set_viewport_size({"width": 390, "height": 844})
        page.wait_for_timeout(800)
        page.click("#lang-ru")
        page.wait_for_timeout(400)
        mobile_label = page.inner_text('#sidebar button[data-sec="live"]')
        print("mobile RU nav label:", mobile_label)
        if mobile_label.strip() != "Видео":
            failures.append("mobile nav did not use the short RU label: %r"
                            % mobile_label)
        page.click("#lang-en")
        page.wait_for_timeout(300)
        mobile_overflow = page.evaluate(
            "() => document.documentElement.scrollWidth - window.innerWidth")
        sidebar_box = page.locator("#sidebar").bounding_box()
        print("mobile overflow:", mobile_overflow, "sidebar:",
              sidebar_box["width"], "x", sidebar_box["height"])
        if mobile_overflow > 1:
            failures.append("horizontal overflow at 390px: %d" % mobile_overflow)
        if sidebar_box["y"] + sidebar_box["height"] < 844 - 2:
            failures.append("the mobile navbar is not anchored to the bottom")
        if not page.is_hidden(".sb-load") or not page.is_hidden(".sb-temp"):
            failures.append("mobile statusline still shows load/temperature")
        button_height = page.eval_on_selector(
            "#sidebar button", "b => b.getBoundingClientRect().height")
        print("mobile nav button height:", button_height)
        if button_height < 40:
            failures.append("mobile nav buttons are smaller than 40px")
        check_fit(page, "mobile 390x844", failures)
        page.screenshot(path=os.path.join(SHOTS, "mobile-system.png"), full_page=True)

        browser.close()

    if console_errors:
        failures.append("console errors: %r" % console_errors[:5])
    if page_errors:
        failures.append("page errors: %r" % page_errors[:5])

    print("\nscreenshots:", SHOTS)
    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("UI check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())