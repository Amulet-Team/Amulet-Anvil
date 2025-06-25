if __name__ != "test_amulet_anvil":
    raise RuntimeError(
        f"Module name is incorrect. Expected: 'test_amulet_anvil' got '{__name__}'"
    )


import faulthandler as _faulthandler

_faulthandler.enable()


def _init() -> None:
    import sys

    # Import dependencies
    import amulet.anvil

    # This needs to be an absolute path otherwise it may get called twice
    # on different module objects and crash when the interpreter shuts down.
    from test_amulet_anvil._test_amulet_anvil import init

    init(sys.modules[__name__])


_init()
