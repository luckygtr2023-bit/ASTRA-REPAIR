"""ASTRA v1.9 — WINDOWS PRODUCTIZATION static verification battery.

What is verifiable offline (everything below): build-system reproducibility
properties, launcher safety properties, distribution-layout structure, and
absence of secrets. What is NOT verifiable here (labeled, never claimed):
MSVC/PE output, on-Windows runtime matrix, GPU execution — see
ASTRA_V1_9_WINDOWS_PRODUCTIZATION_REPORT.md.
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKE = (ROOT / "native_renderer/CMakeLists.txt").read_text(encoding="utf-8")
START = (ROOT / "START.bat").read_text(encoding="utf-8")


# ---------------- build system reproducibility ----------------

def test_cmake_minimum_and_standard_pinned():
    assert re.search(r"cmake_minimum_required\(VERSION 3\.21\)", CMAKE)
    assert "CMAKE_CXX_STANDARD 20" in CMAKE
    assert "CMAKE_CXX_STANDARD_REQUIRED ON" in CMAKE


def test_cmake_has_no_machine_specific_paths():
    for bad in ("C:/VulkanSDK", "C:\\VulkanSDK", "Program Files", "/Users/", "/home/"):
        assert bad not in CMAKE, f"hardcoded path {bad!r} in CMakeLists"


def test_cmake_vulkan_required_fails_closed():
    assert "find_package(Vulkan)" in CMAKE
    assert "Vulkan SDK not found" in CMAKE and "FATAL_ERROR" in CMAKE


def test_cmake_shader_compilation_steps_registered():
    assert "glslang" in CMAKE.lower() or "glslc" in CMAKE.lower()
    # v1.5/v1.7 shaders must be part of the asset pipeline
    assert "v15_travel.vert" in CMAKE and "v17_taa_resolve.frag" in CMAKE


# ---------------- launcher safety ----------------

def test_launcher_requires_no_powershell_or_policy_changes():
    bad = ("powershell", "ExecutionPolicy", "Invoke-", "Set-ExecutionPolicy", "irm ", "iwr ")
    for b in bad:
        assert b.lower() not in START.lower(), f"launcher uses {b}"


def test_launcher_refuses_gracefully_without_binary():
    assert "could not start" in START
    assert "compiled" in START.lower()  # explains the ZIP is source-only honestly


def test_launcher_sets_utf8_and_no_admin():
    assert "chcp 65001" in START
    assert "runas" not in START.lower() and "net session" not in START.lower()


# ---------------- distribution structure ----------------

def test_dist_layout_dirs_present():
    base = ROOT / "release/ASTRA-COSMOS"
    for d in ("assets", "bin", "config", "data", "documentation", "shaders"):
        assert (base / d).is_dir(), f"missing dist dir {d}"


def test_acceptance_template_parses():
    doc = json.loads((ROOT / "distribution/acceptance.template.json").read_text(encoding="utf-8"))
    assert isinstance(doc, dict) and doc, "acceptance template empty"


def test_release_tools_contract_intact():
    src = (ROOT / "distribution/release_tools.py").read_text(encoding="utf-8")
    for fn in ("check_pe", "safe_path", "inspect_archive", "verify_archive", "evidence_gate"):
        assert f"def {fn}(" in src, f"release tool missing {fn}"
    # PE check exists to gate Windows binaries — never faked on Linux


# ---------------- secrets hygiene in shipped tree ----------------

def test_no_secrets_in_release_tree():
    offenders = []
    for p in (ROOT / "release").rglob("*"):
        if not p.is_file() or p.stat().st_size > 2_000_000:
            continue
        try:
            txt = p.read_bytes()
        except OSError:
            continue
        import re as _re
        # token-shaped matches only (documentation prose legitimately
        # mentions the marker prefixes when explaining the policy)
        pats = (
            _re.compile(rb"sb_secret_[A-Za-z0-9]{16,}"),
            _re.compile(rb"service_role_[A-Za-z0-9]{16,}"),
            _re.compile(rb"-----BEGIN [A-Z ]*PRIVATE KEY-----"),
        )
        for pat in pats:
            if pat.search(txt):
                offenders.append((str(p), pat.pattern))
    assert offenders == [], f"secret-like content in release tree: {offenders}"
