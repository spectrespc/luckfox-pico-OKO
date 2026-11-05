#!/usr/bin/env python3
"""Render the divinus WebUI in headless Chromium and check the new layout.

Requires Playwright with a Chromium build:

    python3 -m venv /tmp/pw && /tmp/pw/bin/pip install playwright
    /tmp/pw/bin/playwright install chromium
    /tmp/pw/bin/python bench/ui_check.py
"""
import os
import sys

from playwright.sync_api import sync_playwright

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.97")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")
SHOTS = os.path.join("output", "bench", "ui")

SECTIONS = ["live", "image", "streams", "events", "storage", "network", "osd", "system"]


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
        if "HDR is not supported" not in toast:
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
        page.click("#lang-en")
        page.wait_for_timeout(300)

        # Layout: no horizontal overflow on desktop, untouched Apply buttons
        # disabled, and the mobile breakpoint turns the sidebar into a row.
        overflow = page.evaluate(
            "() => document.documentElement.scrollWidth - window.innerWidth")
        print("desktop overflow:", overflow)
        if overflow > 1:
            failures.append("horizontal overflow at 1440px: %d" % overflow)

        enabled = page.evaluate(
            "() => Array.from(document.querySelectorAll('[data-apply]'))"
            ".filter(b => !b.disabled && b.getAttribute('data-apply').indexOf('osd-') !== 0).length")
        print("enabled apply buttons after refresh:", enabled)
        if enabled:
            failures.append("%d Apply buttons are enabled without edits" % enabled)

        page.set_viewport_size({"width": 390, "height": 844})
        page.wait_for_timeout(800)
        page.click('#sidebar button[data-sec="system"]')
        page.wait_for_timeout(800)
        mobile_overflow = page.evaluate(
            "() => document.documentElement.scrollWidth - window.innerWidth")
        sidebar_box = page.locator("#sidebar").bounding_box()
        print("mobile overflow:", mobile_overflow, "sidebar:",
              sidebar_box["width"], "x", sidebar_box["height"])
        if mobile_overflow > 1:
            failures.append("horizontal overflow at 390px: %d" % mobile_overflow)
        if sidebar_box["height"] > 90:
            failures.append("sidebar did not collapse into a row on mobile")
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