#include <pybind11/pybind11.h>

#include <amulet/pybind11_extensions/compatibility.hpp>

namespace py = pybind11;
namespace pyext = Amulet::pybind11_extensions;

py::module init_anvil_region(py::module);
py::module init_anvil_dimension(py::module);

void init_module(py::module m)
{
    pyext::init_compiler_config(m);
    pyext::check_compatibility(py::module::import("amulet.utils"), m);
    pyext::check_compatibility(py::module::import("amulet.zlib"), m);
    pyext::check_compatibility(py::module::import("amulet.nbt"), m);
    pyext::check_compatibility(py::module::import("amulet.core"), m);

    auto region = init_anvil_region(m);
    auto dimension = init_anvil_dimension(m);

    m.attr("AnvilRegion") = region.attr("AnvilRegion");
    m.attr("RegionDoesNotExist") = region.attr("RegionDoesNotExist");
    m.attr("AnvilDimensionLayer") = dimension.attr("AnvilDimensionLayer");
    m.attr("AnvilDimension") = dimension.attr("AnvilDimension");
    m.attr("RawChunkType") = dimension.attr("RawChunkType");
}

PYBIND11_MODULE(_amulet_anvil, m)
{
    py::options options;
    options.disable_function_signatures();
    m.def("init", &init_module, py::doc("init(arg0: types.ModuleType) -> None"));
    options.enable_function_signatures();
}
