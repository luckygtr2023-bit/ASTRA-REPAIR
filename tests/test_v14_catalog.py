"""v1.4 catalog authority: parser/units/coords/measure/export reference tests.

Real-data reference values come from the pinned HYG v4.1 row for Sirius
(HYG 32263 / HIP 32349: ra 6.752481 h, dec −16.716116°, dist 2.6371 pc,
mag −1.44, absmag 1.454, spect "A0m...", pm −546.01/−1223.08 mas/yr) and the
parsec definition itself (1 pc ↔ 1 arcsec at 1 AU). Binary-contract tests run
against the COMMITTED renderer assets (deterministic, CI-safe); full-CSV
ingest tests run only when the raw bytes are present (never fabricated).
"""

from __future__ import annotations

import json
import math
import os
import struct
import sys

import pytest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from astra.catalog.errors import MalformedCatalogRow, ProvenanceMismatchError  # noqa: E402
from astra.catalog.hyg import HYG_HEADER, parse_hyg_row  # noqa: E402
from astra.catalog.measure import (  # noqa: E402
    ObservatoryFrame, angular_separation_deg, fov_contains, measure_from_observer,
)
from astra.catalog.openngc import (  # noqa: E402
    OPENNGC_HEADER, parse_dec_dms, parse_openngc_row, parse_ra_hms,
)
from astra.catalog.pipeline import (  # noqa: E402
    DSO_MAGIC, DSO_RECORD_SIZE, STARS_MAGIC, STAR_RECORD_SIZE, _HEADER_FORMAT,
    _star_body, emit_binary_catalog, ingest_hyg_csv, ingest_openngc_csv,
)
from astra.catalog.spectra import spectral_temperature_k  # noqa: E402
from astra.catalog.transform import (  # noqa: E402
    AU_KM, C_KM_S, JULIAN_YEAR_S, LY_KM, PC_KM, apparent_magnitude_at_distance,
    apply_proper_motion_deg, light_travel_years, parsec_to_ly, radec_to_unit,
)

ASSETS = os.path.join(REPO_ROOT, "native_renderer", "assets")
STARS_BIN = os.path.join(ASSETS, "astro_stars.v14.bin")
DSO_BIN = os.path.join(ASSETS, "astro_dso.v14.bin")
MANIFEST = os.path.join(ASSETS, "astro_catalog_manifest.json")
RAW_HYG = os.environ.get("ASTRA_TEST_HYG_CSV", "/tmp/hygdata_v41.csv")
RAW_NGC = os.environ.get("ASTRA_TEST_NGC_CSV", "/tmp/OpenNGC-master/database_files/NGC.csv")

SIRIUS_ID = 32263


def _hyg_row(**over):
    base = {k: "" for k in HYG_HEADER}
    base.update({
        "id": "32263", "hip": "32349", "proper": "Sirius",
        "ra": "6.752481", "dec": "-16.716116", "dist": "2.6371",
        "rarad": "1.7677953696021995", "decrad": "-0.291751258517685",
        "pmra": "-546.01", "pmdec": "-1223.08", "rv": "-5.5",
        "mag": "-1.44", "absmag": "1.454", "spect": "A0m...",
        "ci": "0.01", "x": "-0.494323", "y": "2.476731", "z": "-0.758485",
        "var": "", "var_min": "", "var_max": "", "lum": "25.4",
        "bayer": "AlpCMa", "flam": "9", "con": "CMa",
    })
    base.update(over)
    return base


def _ngc_row(**over):
    base = {k: "" for k in OPENNGC_HEADER}
    base.update({
        "Name": "NGC0224", "Type": "G", "RA": "00:42:44.3", "Dec": "+41:16:09",
        "MajAx": "179.0", "MinAx": "46.0", "V-Mag": "3.44", "Hubble": "SA(s)b",
        "RadVel": "-297", "Redshift": "-0.001000", "M": "31",
        "Common names": "Andromeda Galaxy",
        "Sources": "Type:1|RA:1|Dec:1",
    })
    base.update(over)
    return base


# --- constants & units ------------------------------------------------------
class TestConstants:
    def test_pc_km_iaub2(self):
        assert PC_KM == pytest.approx(3.0856775814913673e13, rel=1e-15)

    def test_pc_to_ly(self):
        assert PC_KM / LY_KM == pytest.approx(3.261563777, rel=1e-9)

    def test_c_exact(self):
        assert C_KM_S == 299792.458
        assert JULIAN_YEAR_S == 365.25 * 86400.0

    def test_sirius_ly_reference(self):
        assert parsec_to_ly(2.6371) == pytest.approx(8.6011, abs=1e-3)


# --- HYG parser -------------------------------------------------------------
class TestHygParser:
    def test_sirius_row_reference(self):
        s = parse_hyg_row(_hyg_row(), 2)
        assert s.hyg_id == 32263 and s.hip_id == 32349
        assert s.ra_deg == pytest.approx(101.287215, rel=1e-12)
        assert s.dec_deg == pytest.approx(-16.716116, rel=1e-12)
        assert s.dist_pc == pytest.approx(2.6371)
        assert s.distance_available and s.mag == pytest.approx(-1.44)
        assert s.proper == "Sirius" and s.spect == "A0m..."

    def test_blank_numeric_becomes_none(self):
        s = parse_hyg_row(_hyg_row(rv="", ci="", var_min="", hd=""), 2)
        assert s.rv_km_s is None and s.ci is None

    def test_sentinel_distance_is_not_available_not_clamped(self):
        s = parse_hyg_row(_hyg_row(dist="100000", x="92324.5", y="67.2", z="-38421.0"), 2)
        assert s.dist_pc is None and not s.distance_available
        assert s.x_pc is None and s.y_pc is None and s.z_pc is None  # 100 kpc junk never ingested

    def test_dist_zero_rejected(self):
        with pytest.raises(MalformedCatalogRow):
            parse_hyg_row(_hyg_row(dist="0"), 2)

    def test_nonfinite_rejected(self):
        for bad in ("nan", "inf", "-inf"):
            with pytest.raises(MalformedCatalogRow):
                parse_hyg_row(_hyg_row(dec=bad), 2)

    def test_range_violations(self):
        with pytest.raises(MalformedCatalogRow):
            parse_hyg_row(_hyg_row(ra="24.5"), 2)
        with pytest.raises(MalformedCatalogRow):
            parse_hyg_row(_hyg_row(dec="-91.0"), 2)
        with pytest.raises(MalformedCatalogRow):
            parse_hyg_row(_hyg_row(ra_deg_invalid="x", ra="6.752481", extra="1"), 2)

    def test_header_shape_enforced(self):
        row = _hyg_row()
        del row["lum"]
        with pytest.raises(MalformedCatalogRow):
            parse_hyg_row(row, 2)

    def test_rarad_disagreement_rejected(self):
        with pytest.raises(MalformedCatalogRow):
            parse_hyg_row(_hyg_row(rarad="1.8"), 2)  # >3e-4 deg off

    def test_dist_unknown_xyz_rejected_defensively(self):
        # constructing a record with dist=NONE plus geometry is a contract violation
        from astra.catalog.hyg import HygStar
        with pytest.raises(MalformedCatalogRow):
            HygStar(hyg_id=1, ra_deg=0.0, dec_deg=0.0, dist_pc=None,
                    distance_available=False, mag=None, absmag=None,
                    pmra_mas_yr=None, pmdec_mas_yr=None, rv_km_s=None,
                    spect=None, ci=None, lum_solar=None, x_pc=1.0)


# --- OpenNGC parser ----------------------------------------------------------
class TestNgcParser:
    def test_m31_row_reference(self):
        o = parse_openngc_row(_ngc_row(), 2)
        assert o.identifier == "NGC0224" and o.messier == "M031"
        assert o.ra_deg == pytest.approx((0 + 42 / 60 + 44.3 / 3600) * 15, rel=1e-12)
        assert o.dec_deg == pytest.approx(41 + 16 / 60 + 9 / 3600, rel=1e-12)
        assert o.redshift == pytest.approx(-0.001)
        assert o.type_code == 7 and o.maj_arcmin == pytest.approx(179.0)
        assert o.hubble == "SA(s)b"

    def test_sexagesimal_parsers(self):
        assert parse_ra_hms("23:59:59.5", 2) == pytest.approx(23 + 59 / 60 + 59.5 / 3600)
        assert parse_dec_dms("-00:00:30", 2) == pytest.approx(-30 / 3600)
        with pytest.raises(MalformedCatalogRow):
            parse_ra_hms("24:00:00", 2)
        with pytest.raises(MalformedCatalogRow):
            parse_dec_dms("+91:00:00", 2)

    def test_blank_direction_rejected(self):
        with pytest.raises(MalformedCatalogRow):
            parse_openngc_row(_ngc_row(RA=""), 2)

    def test_unknown_type_not_guessed(self):
        o = parse_openngc_row(_ngc_row(Type="Gal?"), 2)
        assert o.type_code == 22  # '?' — never silently mapped


# --- transforms --------------------------------------------------------------
class TestTransforms:
    def test_radec_unit_axes(self):
        assert radec_to_unit(0.0, 0.0) == pytest.approx((1.0, 0.0, 0.0), abs=1e-15)
        assert radec_to_unit(90.0, 0.0) == pytest.approx((0.0, 1.0, 0.0), abs=1e-15)
        assert radec_to_unit(0.0, 90.0) == pytest.approx((0.0, 0.0, 1.0), abs=1e-15)

    def test_radec_invalid(self):
        with pytest.raises(ValueError):
            radec_to_unit(360.0, 0.0)
        with pytest.raises(ValueError):
            radec_to_unit(0.0, -90.1)

    def test_proper_motion_convention(self):
        # μ_α* convention (includes cos δ — verified against α Cen raw values)
        ra2, dec2 = apply_proper_motion_deg(0.0, 45.0, 1000.0, 0.0, 1.0)
        expected_dra_deg = (1000.0 / 1000.0 / 3600.0) / math.cos(math.radians(45.0))
        assert ra2 == pytest.approx(expected_dra_deg / 1.0, rel=1e-12)
        assert dec2 == pytest.approx(45.0)

    def test_apparent_magnitude_distance_modulus(self):
        # Sirius: m = 1.454 + 5log10(2.6371/10) = −1.44 (catalog truth)
        assert apparent_magnitude_at_distance(1.454, 2.6371 * PC_KM) == pytest.approx(-1.44, abs=1e-3)

    def test_light_travel(self):
        assert light_travel_years(LY_KM) == pytest.approx(1.0, rel=1e-15)


# --- measurement ----------------------------------------------------------
class TestMeasure:
    def test_parsec_definition_holds(self):
        # A star at exactly 1 pc observed 1 AU off-axis shows 1 arcsec parallax
        from astra.catalog.hyg import HygStar
        star = HygStar(hyg_id=7, ra_deg=0.0, dec_deg=0.0, dist_pc=1.0,
                       distance_available=True, mag=None, absmag=None,
                       pmra_mas_yr=None, pmdec_mas_yr=None, rv_km_s=None,
                       spect=None, ci=None, lum_solar=None)
        obs = measure_from_observer(star, (0.0, float(AU_KM), 0.0))
        u0 = radec_to_unit(0.0, 0.0)
        sep = angular_separation_deg(u0, obs.direction_from_observer)
        assert sep * 3600.0 == pytest.approx(1.0, abs=1e-6)  # EXACTLY the parsec definition

    def test_sirius_parallax_reference(self):
        star = parse_hyg_row(_hyg_row(), 2)
        obs = measure_from_observer(star, (float(AU_KM), 0.0, 0.0))
        assert obs.distance_pc == pytest.approx(2.6371)
        assert obs.distance_ly_observer == pytest.approx(8.6011, abs=1e-3)
        assert obs.light_travel_delay_years == pytest.approx(8.6011, abs=1e-3)
        assert obs.apparent_mag_observer == pytest.approx(-1.44, abs=1e-3)
        # parallax from 1 AU baseline ≈ 1/2.6371 arcsec
        sep_from_axis = angular_separation_deg(radec_to_unit(obs.observer_ra_deg, obs.observer_dec_deg),
                                               radec_to_unit(star.ra_deg, star.dec_deg))
        assert sep_from_axis * 3600.0 == pytest.approx(math.sin(math.radians(90.0)) / 2.6371 * 1.0, abs=0.05)

    def test_fov_containment(self):
        fwd = (1.0, 0.0, 0.0)
        frame = ObservatoryFrame(observer_km=(0.0, 0.0, 0.0), fwd_km_unit=fwd,
                                 up_km_unit=(0.0, 0.0, 1.0), right_km_unit=(0.0, 1.0, 0.0),
                                 fov_half_rad=math.radians(10.0))
        assert fov_contains(frame, (1.0, 0.05, 0.0))
        assert not fov_contains(frame, (0.0, 1.0, 0.0))
        assert not fov_contains(frame, (-1.0, 0.0, 0.0))

    def test_observer_motion_changes_measurement(self):
        star = parse_hyg_row(_hyg_row(), 2)
        a = measure_from_observer(star, (0.0, 0.0, 0.0))
        b = measure_from_observer(star, (float(AU_KM), float(AU_KM), 0.0))
        assert a.observer_ra_deg != pytest.approx(b.observer_ra_deg)
        assert a.distance_km_observer != pytest.approx(b.distance_km_observer)

    def test_unknown_distance_is_nan_not_fake(self):
        star = parse_hyg_row(_hyg_row(dist="100000", x="1", y="2", z="3"), 2)
        o = measure_from_observer(star, (0.0, 0.0, 0.0))
        assert math.isnan(o.distance_km_observer) and math.isnan(o.distance_ly_observer)
        assert math.isnan(o.light_travel_delay_years)

    def test_frame_validation(self):
        with pytest.raises(ValueError):
            ObservatoryFrame(observer_km=(0, 0, 0), fwd_km_unit=(2.0, 0.0, 0.0),
                             up_km_unit=(0, 0, 1), right_km_unit=(0, 1, 0),
                             fov_half_rad=0.1)


# --- spectra ------------------------------------------------------------------
class TestSpectra:
    def test_table_values(self):
        assert spectral_temperature_k("A0V").temp_k == pytest.approx(9520.0)
        assert spectral_temperature_k("G2V").temp_k == pytest.approx(5770.0)
        assert spectral_temperature_k("M2").temp_k == pytest.approx(3560.0)

    def test_subdwarf_prefix(self):
        assert spectral_temperature_k("sdM4").temp_k == pytest.approx(3210.0)

    def test_not_available_for_out_of_grid(self):
        assert spectral_temperature_k("WN5").temp_k is None
        assert spectral_temperature_k(None).temp_k is None
        e = spectral_temperature_k("G")
        assert e.temp_k == pytest.approx(5660.0)  # class midpoint, documented


# --- deterministic export + binary contract -----------------------------------
class TestEmit:
    def test_record_sizes(self):
        assert struct.calcsize("<dddffffffIIIQ") == STAR_RECORD_SIZE == 68
        assert struct.calcsize("<dddffffffII24s") == DSO_RECORD_SIZE == 80

    def test_body_deterministic_same_input_same_bytes(self):
        rows = []
        for i in range(1, 4):
            ra_hours = 6.752481 + i * 0.0001
            rows.append(parse_hyg_row(_hyg_row(id=str(i), ra=str(ra_hours),
                                               rarad=str(math.radians(ra_hours * 15.0))), 2))
        b1 = _star_body(sorted(rows, key=lambda s: s.hyg_id))
        b2 = _star_body(sorted(rows, key=lambda s: s.hyg_id))
        assert b1 == b2  # identical input → identical bytes


# --- committed binary assets (renderer contract, CI-safe) ----------------------
def _read_header(path, magic):
    with open(path, "rb") as f:
        data = f.read()
    m, ver, _res, count, rec_size, body_sha = struct.unpack_from(_HEADER_FORMAT, data, 0)
    assert m == magic, f"bad magic in {path}"
    assert ver == 1
    import hashlib
    assert hashlib.sha256(data[64:]).hexdigest() == body_sha.hex()
    return data, count, rec_size


@pytest.mark.skipif(not os.path.exists(STARS_BIN), reason="renderer assets not generated yet")
class TestCommittedAssets:
    def test_star_binary_contract(self):
        data, count, rec_size = _read_header(STARS_BIN, STARS_MAGIC)
        assert count == 119280 and rec_size == STAR_RECORD_SIZE

    def test_dso_binary_contract(self):
        data, count, rec_size = _read_header(DSO_BIN, DSO_MAGIC)
        assert count == 13962 and rec_size == DSO_RECORD_SIZE

    def test_sirius_record_in_committed_binary(self):
        data, count, rec_size = _read_header(STARS_BIN, STARS_MAGIC)
        found = None
        for i in range(count):
            off = 64 + i * rec_size
            rec = struct.unpack_from("<dddffffffIIIQ", data, off)
            if rec[12] == SIRIUS_ID:
                found = rec
                break
        assert found is not None, "Sirius (HYG 32263) must be in the emitted catalog"
        x, y, z, mag, absmag, pmra, pmdec, temp_k, ci, spect_key, hip, flags, hyg_id = found
        d = math.sqrt(x * x + y * y + z * z)
        assert d == pytest.approx(2.6371, rel=1e-4)
        assert mag == pytest.approx(-1.44, abs=1e-2)
        assert hip == 32349
        assert not (flags & 1)  # distance known → NOT shell-placed
        # Direction matches catalog RA/Dec (J2000) to parse tolerance
        u = radec_to_unit(101.287215, -16.716116)
        adir = (x / d, y / d, z / d)
        sep = angular_separation_deg(adir, u)
        assert sep < 1e-3

    def test_manifest_provenance_chain(self):
        with open(MANIFEST, "r", encoding="utf-8") as f:
            m = json.load(f)
        stages = [s["stage"] for s in m["provenance_chain"]]
        assert stages == ["SOURCE", "NORMALIZATION", "EMITTED-COMPUTATIONS"]
        assert m["outputs"]["stars"]["records"] == 119280
        assert m["ingest"]["HYG_V41"]["stats"]["excluded_central"] == 1
        assert m["ingest"]["OPENNGC"]["stats"]["messier_objects"] == 107
        assert any("NOT AVAILABLE" in s for s in m["not_available_statements"])


# --- full-pipeline (raw bytes present only where they exist; never fabricated) --
@pytest.mark.skipif(not (os.path.exists(RAW_HYG) and os.path.exists(RAW_NGC)),
                    reason="raw upstream CSVs not present in this environment")
class TestFullIngest:
    def test_double_emit_byte_identical(self, tmp_path):
        hyg = ingest_hyg_csv(RAW_HYG)
        ngc = ingest_openngc_csv(RAW_NGC)
        p1 = tmp_path / "a"
        p1.mkdir()
        e1 = emit_binary_catalog(hyg.records, ngc.records,
                                 str(p1 / "s.bin"), str(p1 / "d.bin"), str(p1 / "m.json"),
                                 hyg, ngc, {})
        e2 = emit_binary_catalog(hyg.records, ngc.records,
                                 str(p1 / "s2.bin"), str(p1 / "d2.bin"), str(p1 / "m2.json"),
                                 hyg, ngc, {})
        assert e1.stars_body_sha256 == e2.stars_body_sha256
        assert e1.dso_body_sha256 == e2.dso_body_sha256
        with open(str(p1 / "s.bin"), "rb") as f1, open(str(p1 / "s2.bin"), "rb") as f2:
            assert f1.read() == f2.read()

    def test_census_counts(self):
        hyg = ingest_hyg_csv(RAW_HYG)
        assert hyg.rows_seen == 119626
        assert hyg.stats["distance_known"] == 109056
        assert hyg.stats["excluded_central"] == 1
        ngc = ingest_openngc_csv(RAW_NGC)
        assert ngc.rows_seen == 13970
        assert ngc.stats["galaxies"] == 10749

    def test_provenance_pin_enforced(self, tmp_path):
        bad = tmp_path / "corrupted.csv"
        with open(RAW_HYG, "rb") as fi, open(bad, "wb") as fo:
            data = bytearray(fi.read())
            data[-10:-9] = b"x" if data[-10:-9] != b"x" else b"y"
            fo.write(bytes(data))
        with pytest.raises(ProvenanceMismatchError):
            ingest_hyg_csv(str(bad))
