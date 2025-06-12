from __future__ import annotations

import logging as logging
import typing

import amulet.nbt
from amulet.anvil.dimension import AnvilDimension, AnvilDimensionLayer
from amulet.anvil.region import AnvilRegion, RegionDoesNotExist

from . import _amulet_anvil, _version, dimension, region

__all__ = [
    "AnvilDimension",
    "AnvilDimensionLayer",
    "AnvilRegion",
    "RawChunkType",
    "RegionDoesNotExist",
    "compiler_config",
    "dimension",
    "logging",
    "region",
]

def _init() -> None: ...

RawChunkType: typing.TypeAlias = dict[str, amulet.nbt.NamedTag]
__version__: str
compiler_config: dict
