#!/usr/bin/env python3
"""ASTRA v1.7 — performance layer reference fixture (authority-derived).
Emits native_renderer/tests/fixtures/v17_perf_reference.txt.

Records:
  halton,BASE,IDX,VALUE              exact Halton values
  jitter,IDX,X,Y                     sub-pixel jitter pairs
  ctrl,ID,TARGET_MS,QSTART           controller scenario header
  cstep,ID,MEASURED,DEC,QUALITY,CD,EMA   one update step (EMA %.17g)
  lod,SIZE,CLASS                     LOD policy classification
  alpha,ACC,DT,VALUE                 interpolation alpha
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from astra.vizperf import (  # noqa: E402
    AdaptiveQualityController,
    halton,
    lod_class_for_size,
    sub_pixel_jitter,
)
from astra.vizperf.pacing import interpolation_alpha  # noqa: E402

OUT = Path("native_renderer/tests/fixtures/v17_perf_reference.txt")


def scenario(lines, cid, target, qstart, samples):
    c = AdaptiveQualityController(target, qstart)
    lines.append(f"ctrl,{cid},{target:g},{qstart}")
    for m in samples:
        d = c.update(m)
        lines.append(f"cstep,{cid},{m:g},{d.value},{c.quality},{c.cooldown},{c.ema_ms:.17g}")


def main():
    lines = [
        "# ASTRA v1.7 perf reference — generator scripts/gen_v17_reference.py; DO NOT EDIT BY HAND",
    ]
    for base in (2, 3):
        for i in range(1, 17):
            lines.append(f"halton,{base},{i},{halton(i, base):.17g}")
    for i in range(1, 17):
        x, y = sub_pixel_jitter(i)
        lines.append(f"jitter,{i},{x:.17g},{y:.17g}")

    scenario(lines, "C1", 16.6667, 3, [16.6667] * 10)                       # steady on-budget
    scenario(lines, "C2", 16.6667, 3, [25.0] * 65)                           # over-budget -> LOWERs
    scenario(lines, "C3", 16.6667, 0, [4.0] * 100)                           # under-budget -> RAISEs
    scenario(lines, "C4", 16.6667, 3, [25.0] * 40 + [16.6667] * 20 + [4.0] * 40)  # oscillation

    for s in (0.0, 1.0, 3.9, 4.0, 33.0, 63.9, 64.0, 640.0, 1.0e6):
        lines.append(f"lod,{s:g},{int(lod_class_for_size(s))}")
    for a, d in ((0.0, 1.0 / 60), (0.5, 1.0), (0.01, 0.02), (2.0, 1.0), (-1.0, 1.0)):
        lines.append(f"alpha,{a:g},{d:g},{interpolation_alpha(a, d):.17g}")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {sum(1 for l in lines if not l.startswith('#'))} records -> {OUT}")


if __name__ == "__main__":
    main()
