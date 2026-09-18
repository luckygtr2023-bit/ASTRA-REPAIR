#!/usr/bin/env python3
"""gen_v14_catalog.py — deterministic real-catalog exporter (v1.4).

Fetches NOTHING itself (reproducibility lives in the pinned registry):
inputs are the raw upstream CSVs whose sha256 MUST match astra.catalog.sources.
Outputs the renderer assets + provenance manifest; the binary bodies are
byte-identical across runs for identical inputs (tested).

Usage:
    python3 scripts/gen_v14_catalog.py --hyg-csv hygdata_v41.csv \
        --ngc-csv NGC.csv [--out-dir native_renderer/assets]
"""

from __future__ import annotations

import argparse
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO_ROOT)

from astra.catalog.pipeline import (  # noqa: E402
    emit_binary_catalog, ingest_hyg_csv, ingest_openngc_csv,
)

UPSTREAM_NOTES = {
    "hyg": ("HYG v4.1 positions/distances are largely Gaia-DR3-derived for faint "
            "stars and Hipparcos-based nearby; epoch AND equinox J2000.0 "
            "(README). Barnard's Star pmdec reads exactly 9999.99 (upstream "
            "clamp of the true ~+10327 mas/yr) — ingested verbatim."),
    "openngc": ("OpenNGC redshifts/radvels are measured values compiled from NED "
                "with per-field Sources codes (preserved in the Python records; "
                "binary carries the measured values + a redshift_present flag)."),
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--hyg-csv", required=True)
    ap.add_argument("--ngc-csv", required=True)
    ap.add_argument("--out-dir", default=os.path.join(REPO_ROOT, "native_renderer", "assets"))
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    stars_path = os.path.join(args.out_dir, "astro_stars.v14.bin")
    dso_path = os.path.join(args.out_dir, "astro_dso.v14.bin")
    manifest_path = os.path.join(args.out_dir, "astro_catalog_manifest.json")

    print("[v14] ingest HYG:", args.hyg_csv)
    hyg = ingest_hyg_csv(args.hyg_csv)
    print(f"[v14]   seen={hyg.rows_seen} accepted={hyg.rows_accepted} "
          f"rejected={hyg.rows_rejected} stats={hyg.stats}")
    if hyg.rejected:
        print(f"[v14]   rejection histogram: {hyg.rejection_histogram()}")
        for r in hyg.rejected[:5]:
            print(f"[v14]     sample rejection line {r.line_no}: {r.reason}")

    print("[v14] ingest OpenNGC:", args.ngc_csv)
    ngc = ingest_openngc_csv(args.ngc_csv)
    print(f"[v14]   seen={ngc.rows_seen} accepted={ngc.rows_accepted} "
          f"rejected={ngc.rows_rejected} stats={ngc.stats}")
    if ngc.rejected:
        print(f"[v14]   rejection histogram: {ngc.rejection_histogram()}")

    emitted = emit_binary_catalog(hyg.records, ngc.records, stars_path, dso_path,
                                  manifest_path, hyg, ngc, UPSTREAM_NOTES)
    print(f"[v14] EMITTED {len(hyg.records)} stars -> {stars_path}")
    print(f"[v14]       body sha256: {emitted.stars_body_sha256}")
    print(f"[v14] EMITTED {len(ngc.records)} DSOs  -> {dso_path}")
    print(f"[v14]       body sha256: {emitted.dso_body_sha256}")
    print(f"[v14] manifest -> {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
