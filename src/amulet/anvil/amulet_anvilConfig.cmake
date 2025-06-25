if (NOT TARGET amulet_anvil)
    message(STATUS "Finding amulet_anvil")

    find_package(amulet_io CONFIG REQUIRED)
    find_package(amulet_nbt CONFIG REQUIRED)
    find_package(amulet_core CONFIG REQUIRED)
    find_package(amulet_utils CONFIG REQUIRED)

    set(amulet_anvil_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")
    find_library(amulet_anvil_LIBRARY NAMES amulet_anvil PATHS "${CMAKE_CURRENT_LIST_DIR}")
    message(STATUS "amulet_anvil_LIBRARY: ${amulet_anvil_LIBRARY}")

    add_library(amulet_anvil_bin SHARED IMPORTED)
    set_target_properties(amulet_anvil_bin PROPERTIES
        IMPORTED_IMPLIB "${amulet_anvil_LIBRARY}"
    )

    add_library(amulet_anvil INTERFACE)
    target_link_libraries(amulet_anvil INTERFACE amulet_io)
    target_link_libraries(amulet_anvil INTERFACE amulet_nbt)
    target_link_libraries(amulet_anvil INTERFACE amulet_core)
    target_link_libraries(amulet_anvil INTERFACE amulet_utils)
    target_link_libraries(amulet_anvil INTERFACE amulet_anvil_bin)
    target_include_directories(amulet_anvil INTERFACE ${amulet_anvil_INCLUDE_DIR})
endif()
