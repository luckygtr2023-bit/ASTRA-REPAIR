"""ASTRA Audio — request bus (v1.6).

Python-side parity of native AudioBus: default classification, honesty
policy, deterministic FIFO with drop-oldest at capacity. Integrates with
the EXISTING engine EventBus (astra.core.events.EventBus) via a duck-typed
hook — the same way the v1.5 JourneyEngine publishes — never inventing a
parallel event system.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, List, Optional

from astra.audio.classification import (
    AudioClass,
    AudioEventKind,
    default_classification,
    request_is_honest,
)

BUS_CAPACITY = 256  # native kCapacity


@dataclass
class AudioRequest:
    kind: AudioEventKind
    classification: AudioClass
    subject: str
    sim_time_s: float
    gain: float = 1.0


class AudioRequestBus:
    def __init__(self, event_hook: Optional[Callable[[str, dict], None]] = None) -> None:
        self._queue: List[AudioRequest] = []
        self._dropped_dishonest = 0
        self._pushed = 0
        self._hook = event_hook

    def push(
        self,
        kind: AudioEventKind,
        subject: str,
        sim_time_s: float,
        gain: float = 1.0,
    ) -> bool:
        cls = default_classification(kind)
        if not request_is_honest(kind, cls, subject):
            self._dropped_dishonest += 1
            return False
        req = AudioRequest(kind, cls, subject, sim_time_s, gain)
        if len(self._queue) >= BUS_CAPACITY:
            self._queue.pop(0)  # drop oldest (documented, matches native)
        self._queue.append(req)
        self._pushed += 1
        if self._hook is not None:
            self._hook(
                "audio_request",
                {
                    "kind": kind.value,
                    "classification": cls.value,
                    "subject": subject,
                    "sim_time_s": sim_time_s,
                    "gain": gain,
                },
            )
        return True

    def pop(self) -> Optional[AudioRequest]:
        if not self._queue:
            return None
        return self._queue.pop(0)

    def drain(self) -> List[AudioRequest]:
        out = list(self._queue)
        self._queue.clear()
        return out

    @property
    def size(self) -> int:
        return len(self._queue)

    @property
    def dropped_dishonest(self) -> int:
        return self._dropped_dishonest

    @property
    def total_pushed(self) -> int:
        return self._pushed
