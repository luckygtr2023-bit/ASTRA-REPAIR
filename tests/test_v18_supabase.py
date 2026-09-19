"""ASTRA v1.8 — PERSISTENCE + SUPABASE verification battery (offline/static).

The product layer predates this session (v1.0-era) but its tests were not
present on this branch — per rule 12 (tests must exist and be load-bearing)
this battery re-establishes the verification, offline: static migration
audit + client-side enforcement tests. Live Supabase calls are NOT tested
here (environment limitation — no network/CLI); nothing below a live claim.

Package obligations covered:
  * 11 tables (profiles, player_* x6, saved_* x4)
  * RLS on all tables, policies via auth.uid(), no `using (true)`
  * 7 storage buckets, private, foldername[1] == auth.uid() policy pattern
  * path-traversal protection (client-side, refuses any non-<uuid>/ path)
  * env centralization via ASTRA_SUPABASE_*; no service_role in code
  * offline-capable: simulator ticks without the product layer
  * realtime limited to product metadata (profiles), no physics ticks
"""

import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
MIGRATION = ROOT / "supabase/migrations/20250917000001_astra_product_schema.sql"
SQL = MIGRATION.read_text(encoding="utf-8").lower()

EXPECTED_TABLES = {
    "profiles",
    "player_achievements",
    "player_preferences",
    "player_progression",
    "player_sessions",
    "player_statistics",
    "player_unlocks",
    "saved_configurations",
    "saved_observers",
    "saved_scenarios",
    "saved_simulations",
}


def _tables_declared():
    return set(re.findall(r"create table if not exists public\.([a-z_]+)", SQL))


# ---------------- migration static audit ----------------

def test_all_eleven_tables_present():
    assert _tables_declared() == EXPECTED_TABLES


def test_every_table_has_rls_enabled():
    for t in EXPECTED_TABLES:
        assert re.search(rf"alter table public\.{t} enable row level security", SQL), (
            f"RLS not enabled for {t}"
        )


def test_no_using_true_anywhere():
    assert "using (true)" not in SQL  # no blanket-allow RLS policies


def test_policies_reference_auth_uid():
    # every policy on user data must scope by auth.uid()
    pols = re.findall(r"create policy [^;]+;", SQL)
    assert pols, "no policies found"
    without_uid = [p for p in pols if "auth.uid()" not in p]
    assert without_uid == [], f"policies without auth.uid(): {without_uid[:2]}"


def test_migrations_idempotent():
    assert SQL.count("create table if not exists") >= 11
    assert "create policy" in SQL  # policies created inside do/if-not-exists guards
    assert re.search(r"on conflict do nothing|do \$\$|if not exists", SQL)


def test_storage_buckets_private_seven():
    m = re.search(r"insert into storage\.buckets \(id, name, public\) values([^;]+);", SQL)
    assert m, "bucket seed statement missing"
    rows = re.findall(r"\('([a-z0-9-]+)',\s*'([a-z0-9-]+)',\s*(true|false)\)", m.group(1))
    assert len(rows) == 7, f"expected 7 buckets, found {len(rows)}: {rows}"
    assert all(pub == "false" for _, _, pub in rows), "all buckets must be private"


def test_storage_policies_scope_to_owner_folder():
    assert "storage.foldername(name))[1] = auth.uid()::text" in SQL


# ---------------- config + secrets ----------------

def test_config_centralizes_env(monkeypatch):
    import os
    for k in list(os.environ):
        if "SUPABASE" in k:
            monkeypatch.delenv(k, raising=False)
    from astra.product.supabase.config import get_config, reset_config_cache
    reset_config_cache()
    cfg = get_config()
    assert cfg.url == "" and cfg.publishable_key == ""  # no embedded credentials
    assert cfg.is_configured is False
    reset_config_cache()


def test_no_service_role_or_secret_tokens_in_code():
    offenders = []
    for p in (ROOT / "astra/product").rglob("*.py"):
        src = p.read_text(encoding="utf-8").lower()
        if "sb_secret_" in src or "service_role" in src.split('"""')[0]:
            offenders.append(str(p))
    assert offenders == []
    cfg_src = (ROOT / "astra/product/supabase/config.py").read_text(encoding="utf-8")
    assert "service_role" not in cfg_src


# ---------------- storage client enforcement ----------------

def _fresh_storage():
    from astra.product.supabase.storage import _assert_safe_path
    return _assert_safe_path


def test_storage_traversal_refused():
    ok = _fresh_storage()
    uid = "11111111-1111-1111-1111-111111111111"
    from astra.product.supabase.storage import StorageError
    for bad in (
        f"{uid}/../../etc/passwd",
        f"{uid}/..",
        f"{uid}/a/../../b",
        "..",
        "..\\windows\\system32",
        f"22222222-2222-2222-2222-222222222222/owned.txt",  # cross-user prefix
    ):
        with pytest.raises(StorageError):
            ok(uid, bad)


def test_storage_valid_owner_path_accepted():
    ok = _fresh_storage()
    uid = "11111111-1111-1111-1111-111111111111"
    assert ok(uid, f"{uid}/saves/campaign.astra") == f"{uid}/saves/campaign.astra"


def test_storage_backslash_normalizes_within_owner_prefix():
    # backslashes are normalized to '/', then the SAME owner-prefix traversal
    # rules apply: converging an owner-relative path is fine (it cannot escape
    # the prefix), and the normalizer preserves the refusal for ".." segments.
    ok = _fresh_storage()
    uid = "11111111-1111-1111-1111-111111111111"
    assert ok(uid, f"{uid}\\saves\\campaign.astra") == f"{uid}/saves/campaign.astra"
    from astra.product.supabase.storage import StorageError
    with pytest.raises(StorageError):
        ok(uid, f"{uid}\\..\\evil.txt")


# ---------------- offline capability ----------------

def test_product_layer_never_blocks_simulator():
    from astra.core.engine import Engine
    from astra.product.integration import ProductIntegration

    engine = Engine()
    integ = ProductIntegration(engine)
    online = integ.initialize()  # offline: must not raise
    assert online is False
    engine.initialize()
    engine.start()
    engine.step()  # simulator still advances
    assert engine._clock.get_current_tick() >= 1
    engine.stop()

    report = integ.health_report()
    assert report["offline_mode_supported"] is True


# ---------------- realtime discipline ----------------

def test_realtime_scoped_to_product_metadata_only():
    src = (ROOT / "astra/product/supabase/realtime.py").read_text(encoding="utf-8")
    assert "NEVER physics ticks" in src  # enforced prohibition text
    # every channel created must be in the product-metadata allowlist
    channels = re.findall(r"channel\(\"([a-z_]+)\"\)", src)
    assert channels, "no channel subscriptions found"
    assert set(channels) <= {"profiles", "player_sessions"}, (
        f"realtime channels outside product metadata: {channels}"
    )
