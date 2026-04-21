#pragma once

#ifndef AMULET_ANVIL_EXPORT
    #if defined(WIN32) || defined(_WIN32)
        #ifdef ExportAmuletAnvil
            #define AMULET_ANVIL_EXPORT __declspec(dllexport)
        #else
            #define AMULET_ANVIL_EXPORT __declspec(dllimport)
        #endif
    #else
        #define AMULET_ANVIL_EXPORT __attribute__((visibility("default")))
    #endif
#endif
