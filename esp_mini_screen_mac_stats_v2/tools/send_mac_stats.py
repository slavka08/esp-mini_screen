#!/usr/bin/env python3

from pathlib import Path
import runpy


SOURCE = (
    Path(__file__).resolve().parents[2]
    / "esp_mini_screen_mac_stats"
    / "tools"
    / "send_mac_stats.py"
)


if __name__ == "__main__":
    runpy.run_path(str(SOURCE), run_name="__main__")
