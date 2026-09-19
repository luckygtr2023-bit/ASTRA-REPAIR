#!/usr/bin/env python3
"""ASTRA v1.6 — reference fixture generator (authority-derived, NO fabricated
recordings). Emits native_renderer/tests/fixtures/v16_audio_reference.txt.

Record classes:
  defaults,KIND,CLASS                    default classification mapping (parity)
  policy,KIND,CLASS,SUBJECT,HONEST       honesty policy vector (parity)
  synth,ID,N,SR                          synth scenario header
  sfreq,ID,VALUE                         float channel (tolerance 1e-12)
  s16,ID,INDEX,VALUE                     quantized int16 sample (exact)
"""

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from astra.audio.classification import (  # noqa: E402
    AudioClass,
    AudioEventKind,
    default_classification,
    request_is_honest,
)
from astra.audio.synthesis import chirp, orbital_hum, sine  # noqa: E402
from astra.audio.transforms import (  # noqa: E402
    Transform,
    TransformChain,
    quantize_int16,
)
from astra.catalog.transform import AU_KM  # noqa: E402
from astra.physics.constants import GRAVITATIONAL_CONSTANT  # noqa: E402

OUT = Path("native_renderer/tests/fixtures/v16_audio_reference.txt")


def emit_policy(lines):
    for kind in AudioEventKind:
        lines.append(f"defaults,{kind.value},{default_classification(kind).value}")
    vectors = []
    for k in AudioEventKind:
        cls = default_classification(k)
        for subj in ("", "earth.orbit"):
            vectors.append((k.value, cls.value, subj,
                            "1" if request_is_honest(k, cls, subj) else "0"))
    extra = [
        (AudioEventKind.UI_SELECT, AudioClass.REAL_ACOUSTIC, "atmosphere"),
        (AudioEventKind.IMPACT_MODELED, AudioClass.SPECULATIVE, "x"),
        (AudioEventKind.TRAVEL_BEGIN, AudioClass.DATA_DERIVED, ""),
    ]
    for k, c, subj in extra:
        vectors.append((k.value, c.value, subj, "1" if request_is_honest(k, c, subj) else "0"))
    for v in vectors:
        lines.append("policy,%s,%s,%s,%s" % v)


def emit_synth(lines, sid, buf, sr):
    q = quantize_int16(buf)
    lines.append(f"synth,{sid},{len(q)},{sr:g}")
    for i, v in enumerate(q):
        lines.append(f"s16,{sid},{i},{v}")


def main():
    AU_M = AU_KM * 1.0e3
    MU = GRAVITATIONAL_CONSTANT * 1.9885e30
    lines = [
        "# ASTRA v1.6 audio reference — authority-derived; generator scripts/gen_v16_reference.py; DO NOT EDIT BY HAND",
    ]
    emit_policy(lines)

    # S1: pure sine
    emit_synth(lines, "S1", sine(440.0, 0.004, 8000.0), 8000.0)
    # S2: linear chirp
    emit_synth(lines, "S2", chirp(220.0, 660.0, 0.005, 8000.0), 8000.0)
    # S3: orbital hum (Earth @ AU around in-repo solar mass) + freq channel
    freq_buf = orbital_hum(AU_M, MU, 365.25 * 86400.0, 55.0, 0.004, 8000.0, 1.0)
    period = 2.0 * math.pi * math.sqrt((AU_M * AU_M) * AU_M / MU)
    freq = 55.0 * math.pow(365.25 * 86400.0 / period, 1.0)
    lines.append("sfreq,S3,%.17g" % freq)
    emit_synth(lines, "S3", freq_buf, 8000.0)
    # T1: resample 1.5x
    t1 = TransformChain([Transform("resample_linear", 1.5)]).apply(sine(100.0, 0.004, 8000.0))
    emit_synth(lines, "T1", t1, 8000.0)
    # T2: gain then normalize
    t2 = TransformChain([Transform("gain_db", -6.0), Transform("normalize", 0.5)]).apply(
        chirp(110.0, 330.0, 0.004, 8000.0)
    )
    emit_synth(lines, "T2", t2, 8000.0)
    # T3: resample 0.5x (downsample)
    t3 = TransformChain([Transform("resample_linear", 0.5)]).apply(sine(320.0, 0.004, 8000.0))
    emit_synth(lines, "T3", t3, 8000.0)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {sum(1 for l in lines if not l.startswith('#'))} records -> {OUT}")


if __name__ == "__main__":
    main()
