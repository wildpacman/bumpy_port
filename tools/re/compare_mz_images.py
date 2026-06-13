from __future__ import annotations

import hashlib
from dataclasses import dataclass

from tools.re.mz import load_image, parse_mz, relocation_entries


@dataclass(frozen=True)
class ExecutionSemantics:
    load_image: bytes
    load_image_sha256: str
    relocations: tuple[tuple[int, int], ...]
    initial_cs: int
    initial_ip: int
    initial_ss: int
    initial_sp: int
    min_extra_paragraphs: int
    max_extra_paragraphs: int


@dataclass(frozen=True)
class SemanticComparison:
    semantic_match: bool
    differences: tuple[str, ...]
    first: ExecutionSemantics
    second: ExecutionSemantics


def execution_semantics(data: bytes) -> ExecutionSemantics:
    header = parse_mz(data)
    image = load_image(data)
    return ExecutionSemantics(
        load_image=image,
        load_image_sha256=hashlib.sha256(image).hexdigest(),
        relocations=relocation_entries(data),
        initial_cs=header.initial_cs,
        initial_ip=header.initial_ip,
        initial_ss=header.initial_ss,
        initial_sp=header.initial_sp,
        min_extra_paragraphs=header.min_extra_paragraphs,
        max_extra_paragraphs=header.max_extra_paragraphs,
    )


def compare_execution_semantics(first: bytes, second: bytes) -> SemanticComparison:
    first_semantics = execution_semantics(first)
    second_semantics = execution_semantics(second)
    fields = (
        "load_image",
        "relocations",
        "initial_cs",
        "initial_ip",
        "initial_ss",
        "initial_sp",
        "min_extra_paragraphs",
        "max_extra_paragraphs",
    )
    differences = tuple(
        field
        for field in fields
        if getattr(first_semantics, field) != getattr(second_semantics, field)
    )
    return SemanticComparison(
        semantic_match=not differences,
        differences=differences,
        first=first_semantics,
        second=second_semantics,
    )
