"""Catalog-layer errors. All failures are loud and structured."""


class CatalogError(Exception):
    """Base error for the catalog authority."""


class MalformedCatalogRow(CatalogError):
    """A raw row failed strict validation; the row is rejected (never partially ingested)."""

    def __init__(self, line_no: int, reason: str):
        super().__init__(f"line {line_no}: {reason}")
        self.line_no = line_no
        self.reason = reason


class CatalogUnavailableError(CatalogError):
    """A required dataset is unavailable; emit NOT AVAILABLE, never fallback data."""


class ProvenanceMismatchError(CatalogError):
    """Raw-file sha256 did not match the pinned registry value."""
