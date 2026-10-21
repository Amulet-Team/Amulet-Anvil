#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <filesystem>

#include <amulet/pybind11_extensions/collections.hpp>

#include "dimension.hpp"

namespace py = pybind11;

py::module init_anvil_dimension(py::module m_parent)
{
    py::module m = m_parent.def_submodule("dimension");

    py::classh<Amulet::AnvilDimensionLayer>
        AnvilDimensionLayer(m, "AnvilDimensionLayer", py::release_gil_before_calling_cpp_dtor(),
            "A class to manage a directory of region files.");
    AnvilDimensionLayer.def(
        py::init(
            [](std::filesystem::path directory, bool mcc) {
                return std::make_shared<Amulet::AnvilDimensionLayer>(directory, mcc);
            }),
        py::arg("directory"),
        py::arg("mcc") = false);
    AnvilDimensionLayer.def_property_readonly(
        "lock",
        &Amulet::AnvilDimensionLayer::get_mutex,
        py::doc("External lock.\n"
                "Thread safe."));
    AnvilDimensionLayer.def_property_readonly(
        "directory",
        &Amulet::AnvilDimensionLayer::directory,
        py::doc("The directory this instance manages.\n"
                "Thread safe."));
    AnvilDimensionLayer.def_property_readonly(
        "mcc",
        &Amulet::AnvilDimensionLayer::mcc,
        py::doc("Is mcc file support enabled for this instance.\n"
                "Thread safe."));
    AnvilDimensionLayer.def(
        "all_region_coords",
        [](Amulet::AnvilDimensionLayer& self) {
            return py::make_iterator(
                self.all_region_coords(),
                Amulet::AnvilRegionCoordIterator(),
                py::call_guard<py::gil_scoped_release>());
        },
        py::doc(
            "An iterator of all region coordinates in this layer.\n"
            "External Read:SharedReadWrite lock required.\n"
            "External Read:SharedReadOnly lock optional."));
    AnvilDimensionLayer.def(
        "has_region",
        &Amulet::AnvilDimensionLayer::has_region,
        py::arg("rx"),
        py::arg("rz"),
        py::doc(
            "Check if a region file exists in this layer at given the coordinates.\n"
            "External Read:SharedReadWrite lock required.\n"
            "External Read:SharedReadOnly lock optional."));
    AnvilDimensionLayer.def(
        "has_region_at_chunk",
        &Amulet::AnvilDimensionLayer::has_region_at_chunk,
        py::arg("cx"),
        py::arg("cz"),
        py::doc(
            "Check if a region file exists in this layer that contains the given chunk.\n"
            "External Read:SharedReadWrite lock required.\n"
            "External Read:SharedReadOnly lock optional."));
    AnvilDimensionLayer.def(
        "get_region",
        &Amulet::AnvilDimensionLayer::get_region,
        py::arg("rx"),
        py::arg("rz"),
        py::arg("create") = false,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Get an AnvilRegion instance from chunk coordinates it contains. This must not be stored long-term.\n"
                "Will throw RegionDoesNotExist if create is false and the region does not exist.\n"
                "External Read:SharedReadWrite lock required if only calling Read methods on AnvilRegion.\n"
                "External ReadWrite:SharedReadWrite lock required if calling ReadWrite methods on AnvilRegion."));
    AnvilDimensionLayer.def(
        "get_region_at_chunk",
        &Amulet::AnvilDimensionLayer::get_region_at_chunk,
        py::arg("cx"),
        py::arg("cz"),
        py::arg("create") = false,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Get an AnvilRegion instance from chunk coordinates it contains. This must not be stored long-term.\n"
                "Will throw RegionDoesNotExist if create is false and the region does not exist.\n"
                "External Read:SharedReadWrite lock required if only calling Read methods on AnvilRegion.\n"
                "External ReadWrite:SharedReadWrite lock required if calling ReadWrite methods on AnvilRegion."));
    AnvilDimensionLayer.def(
        "all_chunk_coords",
        [](std::shared_ptr<Amulet::AnvilDimensionLayer> self) {
            return py::make_iterator(
                [](std::shared_ptr<Amulet::AnvilDimensionLayer> self) {
                    py::gil_scoped_release nogil;
                    return Amulet::AnvilChunkCoordIterator(std::move(self));
                }(std::move(self)),
                Amulet::AnvilChunkCoordIterator(),
                py::call_guard<py::gil_scoped_release>());
        },
        py::doc("An iterator of all chunk coordinates in this layer.\n"
                "External Read:SharedReadWrite lock required.\n"
                "External Read:SharedReadOnly lock optional."));
    AnvilDimensionLayer.def(
        "has_chunk",
        &Amulet::AnvilDimensionLayer::has_chunk,
        py::arg("cx"),
        py::arg("cz"),
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Check if the chunk has data in this layer.\n"
                "External Read:SharedReadWrite lock required.\n"
                "External Read:SharedReadOnly lock optional."));
    AnvilDimensionLayer.def(
        "get_chunk_data",
        &Amulet::AnvilDimensionLayer::get_chunk_data,
        py::arg("cx"),
        py::arg("cz"),
        py::call_guard<py::gil_scoped_release>(),
        py::doc(
            "Get a NamedTag of a chunk from the database.\n"
            "Will raise RegionEntryDoesNotExist if the region or chunk does not exist\n"
            "External Read:SharedReadWrite lock required."));
    AnvilDimensionLayer.def(
        "set_chunk_data",
        &Amulet::AnvilDimensionLayer::set_chunk_data,
        py::arg("cx"),
        py::arg("cz"),
        py::arg("tag"),
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Set the chunk data for this layer.\n"
                "External ReadWrite:SharedReadWrite lock required."));
    AnvilDimensionLayer.def(
        "delete_chunk",
        &Amulet::AnvilDimensionLayer::delete_chunk,
        py::arg("cx"),
        py::arg("cz"),
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Delete the chunk data from this layer.\n"
                "External ReadWrite:SharedReadWrite lock required."));
    AnvilDimensionLayer.def(
        "compact",
        &Amulet::AnvilDimensionLayer::compact,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Defragment the region files and remove unused region files.\n"
                "External ReadWrite:SharedReadOnly lock required."));
    AnvilDimensionLayer.def(
        "destroy",
        &Amulet::AnvilDimensionLayer::destroy,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Destroy the instance.\n"
                "Calls made after this will fail.\n"
                "This may only be called by the owner of the instance.\n"
                "External ReadWrite:Unique lock required."));
    AnvilDimensionLayer.def(
        "is_destroyed",
        &Amulet::AnvilDimensionLayer::is_destroyed,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Has the instance been destroyed.\n"
                "If this is false, other calls will fail.\n"
                "External Read:SharedReadWrite lock required."));

    py::classh<Amulet::AnvilDimension>
        AnvilDimension(m, "AnvilDimension", py::release_gil_before_calling_cpp_dtor(),
            "A class to manage the data for a dimension.\n"
            "This can consist of multiple layers. Eg the region layer which contains chunk data and the entities layer which contains entities.");
    AnvilDimension.def(
        py::init(
            [](std::filesystem::path directory, Amulet::pybind11_extensions::collections::Iterable<std::string> layer_names, bool mcc) {
                return std::make_shared<Amulet::AnvilDimension>(directory, layer_names, mcc);
            }),
        py::arg("directory"),
        py::arg("layer_names"),
        py::arg("mcc") = false);
    AnvilDimension.def_property_readonly(
        "lock",
        &Amulet::AnvilDimension::get_mutex,
        py::doc("External lock.\n"
                "Thread safe."));
    AnvilDimension.def_property_readonly(
        "directory",
        &Amulet::AnvilDimension::directory,
        py::doc("The directory this dimension is in.\n"
                "Thread safe."));
    AnvilDimension.def_property_readonly(
        "mcc",
        &Amulet::AnvilDimension::mcc,
        py::doc("Are mcc files enabled for this dimension.\n"
                "Thread safe."));
    AnvilDimension.def_property_readonly(
        "layer_names",
        [](Amulet::AnvilDimension& self) {
            py::gil_scoped_release nogil;
            return self.layer_names();
        },
        py::doc("Get the names of all layers in this dimension.\n"
                "External Read:SharedReadWrite lock required.\n"
                "External Read:SharedReadOnly lock optional."));
    AnvilDimension.def(
        "has_layer",
        &Amulet::AnvilDimension::has_layer,
        py::arg("layer_name"),
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Check if this dimension has the requested layer.\n"
                "External Read:SharedReadWrite lock required.\n"
                "External Read:SharedReadOnly lock optional."));
    AnvilDimension.def(
        "get_layer",
        &Amulet::AnvilDimension::get_layer,
        py::arg("layer_name"),
        py::arg("create") = false,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Get the AnvilDimensionLayer for a specific layer. The returned value must not be stored long-term.\n"
                "If create=true the layer will be created if it doesn't exist.\n"
                "External Read:SharedReadWrite lock required if only calling Read methods on AnvilDimensionLayer.\n"
                "External ReadWrite:SharedReadWrite lock required if create=true or calling ReadWrite methods on AnvilDimensionLayer."));
    AnvilDimension.def(
        "all_chunk_coords",
        [](Amulet::AnvilDimension& self) {
            return py::make_iterator(
                [](Amulet::AnvilDimension& self) {
                    py::gil_scoped_release nogil;
                    return self.all_chunk_coords();
                }(self),
                Amulet::AnvilChunkCoordIterator(),
                py::call_guard<py::gil_scoped_release>());
        },
        py::doc("Get an iterator for all the chunks that exist in this dimension.\n"
                "External Read:SharedReadWrite lock required.\n"
                "External Read:SharedReadOnly lock optional."));
    AnvilDimension.def(
        "has_chunk",
        &Amulet::AnvilDimension::has_chunk,
        py::arg("cx"),
        py::arg("cz"),
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Check if a chunk exists.\n"
                "External Read:SharedReadWrite lock required.\n"
                "External Read:SharedReadOnly lock optional."));
    AnvilDimension.def(
        "get_chunk_data",
        &Amulet::AnvilDimension::get_chunk_data,
        py::arg("cx"),
        py::arg("cz"),
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Get the data for a chunk\n"
                "External Read:SharedReadWrite lock required."));
    AnvilDimension.def(
        "set_chunk_data",
        [](Amulet::AnvilDimension& self, std::int64_t cx, std::int64_t cz, Amulet::pybind11_extensions::collections::Iterable<std::pair<std::string, Amulet::NBT::NamedTag>> py_data_layers) {
            std::vector<std::pair<std::string, Amulet::NBT::NamedTag>> data_layers;
            for (const auto& pair : py_data_layers) {
                data_layers.push_back(pair);
            }
            {
                py::gil_scoped_release nogil;
                self.set_chunk_data(cx, cz, data_layers);
            }
        },
        py::arg("cx"),
        py::arg("cz"),
        py::arg("data_layers"),
        py::doc("Set the data for a chunk.\n"
                "External ReadWrite:SharedReadWrite lock required."));
    AnvilDimension.def(
        "delete_chunk",
        &Amulet::AnvilDimension::delete_chunk,
        py::arg("cx"),
        py::arg("cz"),
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Delete all data for the given chunk.\n"
                "External ReadWrite:SharedReadWrite lock required."));
    AnvilDimension.def(
        "compact",
        &Amulet::AnvilDimension::compact,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Defragment the region files and remove unused region files.\n"
                "External ReadWrite:SharedReadOnly lock required."));
    AnvilDimension.def(
        "destroy",
        &Amulet::AnvilDimension::destroy,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Destroy the instance.\n"
                "Calls made after this will fail.\n"
                "This may only be called by the owner of the instance.\n"
                "External ReadWrite:Unique lock required."));
    AnvilDimension.def(
        "is_destroyed",
        &Amulet::AnvilDimension::is_destroyed,
        py::call_guard<py::gil_scoped_release>(),
        py::doc("Has the instance been destroyed.\n"
                "If this is false, other calls will fail.\n"
                "External Read:SharedReadWrite lock required."));

    auto dict = py::module::import("builtins").attr("dict");
    auto str = py::module::import("builtins").attr("str");
    auto NamedTag = py::module::import("amulet.nbt").attr("NamedTag");
    m.attr("RawChunkType") = dict.attr("__class_getitem__")(py::make_tuple(str, NamedTag));

    return m;
}
