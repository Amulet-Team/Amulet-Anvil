#pragma once

#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include <amulet/anvil/dll.hpp>

namespace Amulet {

class AMULET_ANVIL_EXPORT NoValidSector : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    ~NoValidSector() noexcept override;
};

class Sector {
public:
    size_t start;
    size_t stop;

    Sector(size_t start = 0, size_t stop = 0)
        : start(start)
        , stop(stop)
    {
    }

    size_t length() const
    {
        return stop - start;
    }

    // Do the two sectors intersect each other.
    bool intersects(const Sector& other) const;

    // Is the other sector entirely within this sector.
    bool contains(const Sector& other) const;

    // Do the two sectors neighbour but not intersect.
    bool neighbours(const Sector& other) const;

    // Split this sector around another sector.
    std::vector<Sector> split(const Sector& other) const;
};

inline bool operator==(const Sector& lhs, const Sector& rhs)
{
    return lhs.start == rhs.start && lhs.stop == rhs.stop;
}

// Sort by start position
struct SectorStartSort {
    bool operator()(const Sector& a, const Sector& b) const
    {
        return a.start < b.start;
    }
};

// Sort by length then start.
struct SectorLengthStartSort {
    bool operator()(const Sector& a, const Sector& b) const
    {
        if (a.length() == b.length()) {
            return a.start < b.start;
        } else {
            return a.length() < b.length();
        }
    }
};

// Sort by start then length.
struct SectorStartLengthSort {
    bool operator()(const Sector& a, const Sector& b) const
    {
        if (a.start == b.start) {
            return a.length() < b.length();
        } else {
            return a.start < b.start;
        }
    }
};

struct ReservedSector;
using SectorManagerReservedMap = std::map<Sector, ReservedSector, SectorStartLengthSort>;
using SectorManagerReservedMapIt = SectorManagerReservedMap::iterator;

struct SectorMapItStartLengthSort {
    bool operator()(const SectorManagerReservedMapIt& a, const SectorManagerReservedMapIt& b) const;
};

struct ReservedSector {
    // The number of times this sector is reserved.
    size_t count = 1;

    // Other sectors that intersect this sector.
    std::set<SectorManagerReservedMap::iterator, SectorMapItStartLengthSort> intersecting_sectors;
};

inline bool SectorMapItStartLengthSort::operator()(const SectorManagerReservedMapIt& a, const SectorManagerReservedMapIt& b) const
{
    return SectorStartLengthSort()(a->first, b->first);
}

// A class to manage a contiguous group of sectors.
// This class is not thread safe. The owner must handle synchronisation.
class SectorManager {
private:
    size_t _stop;

    // A list of free sectors ordered by start location
    std::set<Sector, SectorStartSort> _free_start;

    // A list of free sectors ordered by (length, start).
    //  This makes it easier to find the first sector large enough
    std::set<Sector, SectorLengthStartSort> _free_size;

    // A map of reserved sectors to metadata.
    SectorManagerReservedMap _reserved;

public:
    explicit SectorManager(size_t size = 0)
        : _stop(size)
    {
        if (size) {
            _free_start.emplace(0, size);
            _free_size.emplace(0, size);
        }
    }

    SectorManager(const SectorManager&) = delete;
    SectorManager& operator=(const SectorManager&) = delete;
    SectorManager(SectorManager&&) = default;
    SectorManager& operator=(SectorManager&&) = default;

    // All free sectors ordered by start address
    const std::set<Sector, SectorStartSort>& sectors() const { return _free_start; }

    // Reserve a sector of size length that does not intersect any reserved sectors.
    Sector reserve_space(size_t length);

    // Reserve the requested sector.
    // If the sector intersects a reserved sector, the intersection reserved twice.
    void reserve(const Sector& sector);

    // Free the specified sector.
    // The sector must have been previously reserved.
    void free(const Sector& sector);
};

} // namespace Amulet
