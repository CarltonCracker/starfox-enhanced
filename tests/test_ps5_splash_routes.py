"""Verify every desktop presenter reaches the PS5 splash handoff."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src/app/starfox_pc.cpp").read_text(encoding="utf-8")
body = re.search(r"const auto complete_presentation=\[&\] \{(.*?)\n        \};", source, re.S)
assert body, "Missing shared successful-presentation handoff"
assert "StarfoxPS5_HideSplashScreen();" in body.group(1)
assert "if(presented_frames==0)" not in body.group(1), "Failed splash hide cannot retry"
assert body.group(1).count("++presented_frames") == 1
assert source.count("++presented_frames") == 1, "A presenter bypasses the shared splash handoff"
assert len(re.findall(r"\bcomplete_presentation\(\);", source)) == 4, "Review every presenter, including early returns"
plain = source[source.index("window.present_plain_ui(setup_overlay"):]
assert plain.index("complete_presentation();") < plain.index("continue;"), "Preview OFF leaves the launch splash covering VideoOut"
print("All four presentation paths reach the PS5 splash handoff; failures can retry")
