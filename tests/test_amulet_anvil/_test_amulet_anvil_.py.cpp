#include <pybind11/pybind11.h>

namespace py = pybind11;

void init_test_region(py::module m_parent);

void init_test_amulet_anvil(py::module m){
    init_test_region(m);
}
