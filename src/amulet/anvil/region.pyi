from __future__ import annotations

import collections.abc
import typing

import amulet.nbt
import amulet.utils.lock

__all__ = ["AnvilRegion", "RegionDoesNotExist"]

class AnvilRegion:
    """
    A class to read and write Minecraft Java Edition Region files.
    Only one instance should exist per region file at any given time otherwise bad things may happen.
    This class is internally thread safe but a public lock is provided to enable external synchronisation.
    Upstream locks from the level must also be adhered to.
    """

    class FileCloser:
        """
        A class to manage closing the region file.
        When the instance is deleted the region file will be closed.
        The region file can be manually closed before this is deleted.
        """

    @typing.overload
    def __init__(
        self,
        directory: str,
        file_name: str,
        rx: typing.SupportsInt,
        rz: typing.SupportsInt,
        mcc: bool = False,
    ) -> None:
        """
        Construct from the directory path, name of the file and region coordinates.
        """

    @typing.overload
    def __init__(
        self,
        directory: str,
        rx: typing.SupportsInt,
        rz: typing.SupportsInt,
        mcc: bool = False,
    ) -> None:
        """
        Construct from the directory path and region coordinates.
        File name is computed from region coordinates.
        """

    @typing.overload
    def __init__(self, path: str, mcc: bool = False) -> None:
        """
        Construct from the path to the region file.
        Coordinates are computed from the file name.
        File name must match "r.X.Z.mca".
        """

    def close(self) -> None:
        """
        Close the file object if open.
        This is automatically called when the instance is destroyed but may be called earlier.
        Thread safe.
        """

    def compact(self) -> None:
        """
        Compact the region file.
        Defragments the file and deletes unused space.
        If there are no chunks remaining in the region file it will be deleted.
        External ReadWrite:SharedReadWrite lock required.
        """

    def contains(self, cx: typing.SupportsInt, cz: typing.SupportsInt) -> bool:
        """
        Is the coordinate in the region.
        This returns true even if there is no value for the coordinate.
        Coordinates are in world space.
        Thread safe.
        """

    def delete_batch(
        self,
        coords: collections.abc.Sequence[tuple[typing.SupportsInt, typing.SupportsInt]],
    ) -> None:
        """
        Delete multiple chunk's data.
        Coordinates are in world space.
        External ReadWrite:SharedReadWrite lock required.
        """

    def delete_value(self, cx: typing.SupportsInt, cz: typing.SupportsInt) -> None:
        """
        Delete the chunk data.
        Coordinates are in world space.
        External ReadWrite:SharedReadWrite lock required.
        """

    def destroy(self) -> None:
        """
        Destroy the instance.
        Calls made after this will fail.
        This may only be called by the owner of the instance.
        External ReadWrite:Unique lock required.
        """

    def get_coords(self) -> list[tuple[int, int]]:
        """
        Get the coordinates of all values in the region file.
        Coordinates are in world space.
        External Read:SharedReadWrite lock required.
        External Read:SharedReadOnly lock optional.
        """

    def get_file_closer(self) -> AnvilRegion.FileCloser:
        """
        Get the object responsible for closing the region file.
        When this object is deleted it will close the region file
        This means that holding a reference to this will delay when the region file is closed.
        The region file may still be closed manually before this object is deleted.
        Thread safe.
        """

    def get_value(
        self, cx: typing.SupportsInt, cz: typing.SupportsInt
    ) -> amulet.nbt.NamedTag:
        """
        Get the value for this coordinate.
        Coordinates are in world space.
        External Read:SharedReadWrite lock required.
        """

    def has_value(self, cx: typing.SupportsInt, cz: typing.SupportsInt) -> bool:
        """
        Is there a value stored for this coordinate.
        Coordinates are in world space.
        External Read:SharedReadWrite lock required.
        External Read:SharedReadOnly lock optional.
        """

    def is_destroyed(self) -> bool:
        """
        Has the instance been destroyed.
        If this is false, other calls will fail.
        External Read:SharedReadWrite lock required.
        """

    def set_value(
        self, cx: typing.SupportsInt, cz: typing.SupportsInt, tag: amulet.nbt.NamedTag
    ) -> None:
        """
        Set the value for this coordinate.
        Coordinates are in world space.
        External ReadWrite:SharedReadWrite lock required.
        """

    @property
    def lock(self) -> amulet.utils.lock.OrderedLock:
        """
        A lock which can be used to synchronise calls.
        Thread safe.
        """

    @property
    def path(self) -> str:
        """
        The path of the region file.
        Thread safe.
        """

    @property
    def rx(self) -> int:
        """
        The region x coordinate of the file.
        Thread safe.
        """

    @property
    def rz(self) -> int:
        """
        The region z coordinate of the file.
        Thread safe.
        """

class RegionDoesNotExist(RuntimeError):
    pass
