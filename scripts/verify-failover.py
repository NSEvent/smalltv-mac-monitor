#!/usr/bin/env python3
"""Exercise failover on an updated cube, then save its requested final fallback.

Usage: python3 scripts/verify-failover.py <cube-ip> <fallback-url>
The existing primary/settings are preserved; temporary URLs are cleaned up even
if the test fails. This briefly reboots the cube twice.
"""
import json
import sys
import time
import urllib.parse
import urllib.request


def main():
    cube, fallback = sys.argv[1:]
    root = f"http://{cube}"

    def status():
        with urllib.request.urlopen(root + "/api/status", timeout=4) as response:
            return json.load(response)

    def save(primary, backup):
        data = urllib.parse.urlencode({"url": primary, "fallback": backup}).encode()
        with urllib.request.urlopen(root + "/save", data=data, timeout=8) as response:
            assert response.status == 200

    def wait_for(url, backup):
        deadline = time.monotonic() + 45
        last = None
        while time.monotonic() < deadline:
            try:
                last = status()
                if (last.get("active_url") == url and
                        last.get("fallback_url") == backup and
                        0 <= last["sample_age_ms"] < 2500):
                    return last
            except (OSError, ValueError):
                pass
            time.sleep(1)
        raise RuntimeError(f"No fresh readings from {url}: {last}")

    deadline = time.monotonic() + 45
    before = None
    while time.monotonic() < deadline:
        try:
            before = status()
            break
        except (OSError, ValueError):
            time.sleep(1)
    assert before is not None, "Cube did not return after OTA"
    assert before.get("firmware") in ("2026-10-06-fallback-2", "2026-10-07-amber-terminal-3", "2026-10-07-signal-traces-4", "2026-10-07-large-traces-5", "2026-10-07-thick-traces-6"), before
    primary = before["url"]
    try:
        # Closed local port: deterministic failure, no external traffic.
        save("http://127.0.0.1:1/metrics", primary)
        tested = wait_for(primary, primary)
        assert tested["url"] == "http://127.0.0.1:1/metrics"
        print(cube, "forced primary failure: fallback served fresh metrics", flush=True)
    finally:
        save(primary, fallback)
    final = wait_for(primary, fallback)
    for key in ("host", "rotation", "brightness"):
        assert final[key] == before[key], (key, before, final)
    with open(f"/tmp/smalltv-{cube}-fallback-verification.json", "w") as output:
        json.dump({"before": before, "forced_failover": tested, "final": final}, output, indent=2)
    print(json.dumps(final), flush=True)


if __name__ == "__main__":
    main()
