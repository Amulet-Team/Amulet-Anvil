#include <pybind11/pybind11.h>

namespace py = pybind11;

py::module init_anvil_region(py::module);
py::module init_anvil_dimension(py::module);

void init_amulet_anvil(py::module m)
{
    auto region = init_anvil_region(m);
    auto dimension = init_anvil_dimension(m);

    m.attr("AnvilRegion") = region.attr("AnvilRegion");
    m.attr("RegionDoesNotExist") = region.attr("RegionDoesNotExist");
    m.attr("RegionEntryDoesNotExist") = region.attr("RegionEntryDoesNotExist");
    m.attr("AnvilDimensionLayer") = dimension.attr("AnvilDimensionLayer");
    m.attr("AnvilDimension") = dimension.attr("AnvilDimension");
    m.attr("RawChunkType") = dimension.attr("RawChunkType");
}
