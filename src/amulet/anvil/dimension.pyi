from __future__ import annotations

import collections.abc
import typing

import amulet.anvil.region
import amulet.nbt
import amulet.utils.lock

__all__ = ["AnvilDimension", "AnvilDimensionLayer", "RawChunkType"]

class AnvilDimension:
    """
    A class to manage the data for a dimension.
    This can consist of multiple layers. Eg the region layer which contains chunk data and the entities layer which contains entities.
    """

    def __init__(
        self,
        directory: str,
        layer_names: collections.abc.Iterable[str],
        mcc: bool = False,
    ) -> None: ...
    def all_chunk_coords(self) -> collections.abc.Iterator[tuple[int, int]]:
        """
        Get an iterator for all the chunks that exist in this dimension.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    def compact(self) -> None:
        """
        Defragment the region files and remove unused region files.
        External ReadWrite::SharedReadOnly lock required.
        """

    def delete_chunk(self, cx: typing.SupportsInt, cz: typing.SupportsInt) -> None:
        """
        Delete all data for the given chunk.
        External ReadWrite::SharedReadWrite lock required.
        """

    def destroy(self) -> None:
        """
        Destroy the instance.
        Calls made after this will fail.
        This may only be called by the owner of the instance.
        External ReadWrite:Unique lock required.
        """

    def get_chunk_data(
        self, cx: typing.SupportsInt, cz: typing.SupportsInt
    ) -> dict[str, amulet.nbt.NamedTag]:
        """
        Get the data for a chunk
        External Read::SharedReadWrite lock required.
        """

    def get_layer(self, layer_name: str, create: bool = False) -> AnvilDimensionLayer:
        """
        Get the AnvilDimensionLayer for a specific layer. The returned value must not be stored long-term.
        If create=true the layer will be created if it doesn't exist.
        External Read::SharedReadWrite lock required if only calling Read methods on AnvilDimensionLayer.
        // External ReadWrite::SharedReadWrite lock required if calling ReadWrite methods on AnvilDimensionLayer.
        """

    def has_chunk(self, cx: typing.SupportsInt, cz: typing.SupportsInt) -> bool:
        """
        Check if a chunk exists.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    def has_layer(self, layer_name: str) -> bool:
        """
        Check if this dimension has the requested layer.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    def is_destroyed(self) -> bool:
        """
        Has the instance been destroyed.
        If this is false, other calls will fail.
        External Read:SharedReadWrite lock required.
        """

    def set_chunk_data(
        self,
        cx: typing.SupportsInt,
        cz: typing.SupportsInt,
        data_layers: collections.abc.Iterable[tuple[str, amulet.nbt.NamedTag]],
    ) -> None:
        """
        Set the data for a chunk.
        External ReadWrite::SharedReadWrite lock required.
        """

    @property
    def directory(self) -> str:
        """
        The directory this dimension is in.
        Thread safe.
        """

    @property
    def layer_names(self) -> list[str]:
        """
        Get the names of all layers in this dimension.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    @property
    def lock(self) -> amulet.utils.lock.OrderedLock:
        """
        External lock.
        Thread safe.
        """

    @property
    def mcc(self) -> bool:
        """
        Are mcc files enabled for this dimension.
        Thread safe.
        """

class AnvilDimensionLayer:
    """
    A class to manage a directory of region files.
    """

    def __init__(self, directory: str, mcc: bool = False) -> None: ...
    def all_chunk_coords(self) -> collections.abc.Iterator[tuple[int, int]]:
        """
        An iterator of all chunk coordinates in this layer.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    def all_region_coords(self) -> collections.abc.Iterator[tuple[int, int]]:
        """
        An iterator of all region coordinates in this layer.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    def compact(self) -> None:
        """
        Defragment the region files and remove unused region files.
        External ReadWrite::SharedReadOnly lock required.
        """

    def delete_chunk(self, cx: typing.SupportsInt, cz: typing.SupportsInt) -> None:
        """
        Delete the chunk data from this layer.
        External ReadWrite::SharedReadWrite lock required.
        """

    def destroy(self) -> None:
        """
        Destroy the instance.
        Calls made after this will fail.
        This may only be called by the owner of the instance.
        External ReadWrite:Unique lock required.
        """

    def get_chunk_data(
        self, cx: typing.SupportsInt, cz: typing.SupportsInt
    ) -> amulet.nbt.NamedTag:
        """
        Get a NamedTag of a chunk from the database.
        Will raise ChunkDoesNotExist if the region or chunk does not exist
        External Read::SharedReadWrite lock required.
        """

    def get_region(
        self, rx: typing.SupportsInt, rz: typing.SupportsInt, create: bool = False
    ) -> amulet.anvil.region.AnvilRegion:
        """
        Get an AnvilRegion instance from chunk coordinates it contains. This must not be stored long-term.
        Will throw RegionDoesNotExist if create is false and the region does not exist.
        External Read::SharedReadWrite lock required if only calling Read methods on AnvilRegion.
        External ReadWrite::SharedReadWrite lock required if calling ReadWrite methods on AnvilRegion.
        """

    def get_region_at_chunk(
        self, cx: typing.SupportsInt, cz: typing.SupportsInt, create: bool = False
    ) -> amulet.anvil.region.AnvilRegion:
        """
        Get an AnvilRegion instance from chunk coordinates it contains. This must not be stored long-term.
        Will throw RegionDoesNotExist if create is false and the region does not exist.
        External Read::SharedReadWrite lock required if only calling Read methods on AnvilRegion.
        External ReadWrite::SharedReadWrite lock required if calling ReadWrite methods on AnvilRegion.
        """

    def has_chunk(self, cx: typing.SupportsInt, cz: typing.SupportsInt) -> bool:
        """
        Check if the chunk has data in this layer.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    def has_region(self, rx: typing.SupportsInt, rz: typing.SupportsInt) -> bool:
        """
        Check if a region file exists in this layer at given the coordinates.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    def has_region_at_chunk(
        self, cx: typing.SupportsInt, cz: typing.SupportsInt
    ) -> bool:
        """
        Check if a region file exists in this layer that contains the given chunk.
        External Read::SharedReadWrite lock required.
        External Read::SharedReadOnly lock optional.
        """

    def is_destroyed(self) -> bool:
        """
        Has the instance been destroyed.
        If this is false, other calls will fail.
        External Read:SharedReadWrite lock required.
        """

    def set_chunk_data(
        self, cx: typing.SupportsInt, cz: typing.SupportsInt, tag: amulet.nbt.NamedTag
    ) -> None:
        """
        Set the chunk data for this layer.
        External ReadWrite::SharedReadWrite lock required.
        """

    @property
    def directory(self) -> str:
        """
        The directory this instance manages.
        Thread safe.
        """

    @property
    def lock(self) -> amulet.utils.lock.OrderedLock:
        """
        External lock.
        Thread safe.
        """

    @property
    def mcc(self) -> bool:
        """
        Is mcc file support enabled for this instance.
        Thread safe.
        """

RawChunkType: typing.TypeAlias = dict[str, amulet.nbt.NamedTag]
