from __future__ import annotations

import faulthandler as faulthandler

from . import _test_amulet_anvil, test_region_

__all__ = ["compiler_config", "faulthandler", "test_region_"]

def _init() -> None: ...

compiler_config: dict
