from __future__ import annotations

import hashlib
from dataclasses import dataclass

from tools.re.mz import load_image


@dataclass(frozen=True)
class Comparison:
    equal: bool
    first_sha256: str
    second_sha256: str
    size: int


def compare_load_images(first: bytes, second: bytes) -> Comparison:
    first_image = load_image(first)
    second_image = load_image(second)
    return Comparison(
        equal=first_image == second_image,
        first_sha256=hashlib.sha256(first_image).hexdigest(),
        second_sha256=hashlib.sha256(second_image).hexdigest(),
        size=len(first_image),
    )
