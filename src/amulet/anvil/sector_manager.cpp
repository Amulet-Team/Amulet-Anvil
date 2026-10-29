#include "sector_manager.hpp"
#include <list>
#include <set>
#include <vector>

namespace Amulet {

NoValidSector::~NoValidSector() noexcept { }

// Do the two sectors intersect each other.
bool Sector::intersects(const Sector& other) const
{
    return !(other.stop <= start || stop <= other.start);
}

// Is the other sector entirely within this sector.
bool Sector::contains(const Sector& other) const
{
    return start <= other.start && other.stop <= stop;
}

// Do the two sectors neighbour but not intersect.
bool Sector::neighbours(const Sector& other) const
{
    return other.stop == start or stop == other.start;
}

// Split this sector around another sector.
std::vector<Sector> Sector::split(const Sector& other) const
{
    std::vector<Sector> sectors;
    if (start < other.start) {
        if (other.start < stop) {
            sectors.emplace_back(start, other.start);
        } else {
            sectors.emplace_back(start, stop);
        }
    }
    if (other.stop < stop) {
        if (other.stop < start) {
            sectors.emplace_back(start, stop);
        } else {
            sectors.emplace_back(other.stop, stop);
        }
    }
    return sectors;
}

Sector SectorManager::reserve_space(size_t length)
{
    if (length == 0) {
        throw std::invalid_argument("Cannot reserve a sector with zero length.");
    }
    auto it = _free_size.lower_bound(Sector(0, length));
    if (it != _free_size.end()) {
        // There exists a section large enough to fit the length
        Sector free_sector = *it;
        // Remove the section from the free sections
        it = _free_size.erase(it);
        _free_start.erase(free_sector);
        // Create the reserved sector.
        Sector sector(free_sector.start, free_sector.start + length);
        if (length < free_sector.length()) {
            // Re-add the remainder to the free sections.
            _free_size.emplace_hint(it, sector.stop, free_sector.stop);
            _free_start.emplace(sector.stop, free_sector.stop);
        }
        _reserved.emplace(sector, ReservedSector { });
        return sector;
    } else {
        size_t start = _stop;
        if (!_free_start.empty() && _free_start.rbegin()->stop == _stop) {
            // The last sector is unreserved but not large enough to store the data.
            // Extend the sector.
            const auto old_sector = *_free_start.rbegin();
            _free_start.erase(old_sector);
            _free_size.erase(old_sector);
            start = old_sector.start;
        }
        // Resize to fit
        Sector sector(start, start + length);
        _stop = sector.stop;
        _reserved.emplace(sector, ReservedSector { });
        return sector;
    }
}

void SectorManager::reserve(const Sector& sector)
{
    if (sector.stop < sector.start) {
        throw std::invalid_argument("stop must be at least start");
    }
    if (sector.length() == 0) {
        return; // Nothing to do.
    }

    // Find the sector greater than or equal to this sector
    auto new_reserved_it = _reserved.lower_bound(sector);

    // If the found sector is the sector we are looking for
    if (new_reserved_it != _reserved.end() && new_reserved_it->first == sector) {
        // This sector is already reserved.
        // Increase its use count and return.
        new_reserved_it->second.count += 1;
        return;
    }

    // Remove or extend free sectors
    if (_stop < sector.start) {
        // If this sector is past the end
        // It cannot intersect any free sectors.
        // We must extend the free sectors to the start of this sector.

        // The start of the new sector.
        size_t start;

        if (!_free_start.empty() && _free_start.rbegin()->stop == _stop) {
            // Last free sector is at the end. Merge the new sector with it.
            auto free_it = std::prev(_free_start.end());

            // Update the start to the start of the last free sector.
            start = free_it->start;

            // Erase the last sector
            _free_size.erase(*free_it);
            _free_start.erase(free_it);
        } else {
            // New sector starts at the end of the file
            start = _stop;
        }

        // Add the new sector.
        _free_start.emplace(start, sector.start);
        _free_size.emplace(start, sector.start);

    } else {
        // Remove intersecting free sectors

        // Find the first free sector that starts at or before the sector.
        auto free_it = _free_start.lower_bound(sector);
        if (free_it != _free_start.end() && free_it->start == sector.start) {
            // Found the correct sector
        } else if (free_it != _free_start.begin()) {
            // Seek to the previous value if this is not the beginning.
            free_it--;
        }

        // Remove intersecting sectors.
        // Iterate from the value at or before sector until the value after sector.
        while (free_it != _free_start.end() && free_it->start < sector.stop) {
            if (sector.intersects(*free_it)) {
                // If the sector intersects

                // Get the sectors that do not intersect
                auto new_free_sectors = free_it->split(sector);

                // Erase the old sector.
                _free_size.erase(*free_it);
                free_it = _free_start.erase(free_it);

                // Add the new sectors.
                _free_size.insert(new_free_sectors.begin(), new_free_sectors.end());
                _free_start.insert(new_free_sectors.begin(), new_free_sectors.end());
            } else {
                // If it does not intersect, go to the next sector.
                free_it++;
            }
        }
    }

    // Extend the stop to the end of the sector
    if (_stop < sector.stop) {
        _stop = sector.stop;
    }

    std::set<SectorManagerReservedMap::iterator, SectorMapItStartLengthSort> intersecting_sectors;

    auto reserved_it = new_reserved_it;

    // Find intersections from prior sectors.
    if (reserved_it != _reserved.begin()) {
        // Go to the previous sector if there is one.
        auto previous_reserved_it = std::prev(reserved_it);

        // If it intersects the previous sector then add it.
        if (sector.intersects(previous_reserved_it->first)) {
            intersecting_sectors.emplace(previous_reserved_it);
        }

        // Check intersections with prior sectors
        for (const auto& intersecting_reserved_it : previous_reserved_it->second.intersecting_sectors) {
            if (intersecting_reserved_it == reserved_it) {
                // Sector is beyond the start of the sector we are adding.
                // These are checked below.
                break;
            }
            if (sector.intersects(intersecting_reserved_it->first)) {
                // Add the intersection
                intersecting_sectors.emplace(intersecting_reserved_it);
            }
        }
    }

    // Find intersections with future sectors.
    for (; reserved_it != _reserved.end() && reserved_it->first.start < sector.stop; reserved_it++) {
        intersecting_sectors.emplace(reserved_it);
    }

    // Reserve the sector.
    new_reserved_it = _reserved.emplace_hint(new_reserved_it, sector, ReservedSector { });

    // Add the new sector to all the intersecting sectors.
    for (auto it : intersecting_sectors) {
        it->second.intersecting_sectors.emplace(new_reserved_it);
    }

    // Move the intersecting sectors map
    new_reserved_it->second.intersecting_sectors = std::move(intersecting_sectors);
}

void SectorManager::free(const Sector& sector)
{
    if (sector.stop < sector.start) {
        throw std::invalid_argument("stop must be at least start");
    }
    if (sector.length() == 0) {
        return; // Nothing to do.
    }

    // Find the sector
    auto reserved_it = _reserved.find(sector);
    if (reserved_it == _reserved.end()) {
        throw std::invalid_argument("Sector was not reserved");
    }

    auto& sector_metadata = reserved_it->second;

    // Free one use of this sector.
    sector_metadata.count -= 1;

    if (sector_metadata.count) {
        // This sector is still reserved.
        return;
    }

    // Extract the sectors this sector intersects.
    auto intersecting_sectors = std::move(sector_metadata.intersecting_sectors);

    // Sectors that can be freed.
    std::list<Sector> free_sectors { sector };

    // Remove backlinks from all intersecting sectors.
    for (const auto& intersecting_it : intersecting_sectors) {
        // Remove this sector from the other sector's intersections
        intersecting_it->second.intersecting_sectors.erase(reserved_it);

        const auto& intersecting_sector = intersecting_it->first;

        // We must subtract the intersections because they are still reserved.
        for (auto free_it = free_sectors.begin(); free_it != free_sectors.end();) {
            if (free_it->intersects(intersecting_sector)) {
                // Split the sector
                auto split_free_sections = free_it->split(intersecting_sector);
                // Add the new sectors
                free_sectors.insert(free_sectors.end(), split_free_sections.begin(), split_free_sections.end());
                // Erase the old sector
                free_it = free_sectors.erase(free_it);
            } else {
                free_it++;
            }
        }
    }

    // Remove the sector from the reserved storage.
    _reserved.erase(reserved_it);

    // Add all the free sectors to the free containers.
    for (const auto& free_sector : free_sectors) {
        // Find the first sector that starts after this sector.
        auto free_it = _free_start.upper_bound(free_sector);

        Sector new_sector = free_sector;
        if (free_it != _free_start.end() && free_it->start == new_sector.stop) {
            // The next free sector starts at the end of the freed sector. Merge them.
            new_sector.stop = free_it->stop;
            _free_size.erase(*free_it);
            free_it = _free_start.erase(free_it);
        }

        if (free_it != _free_start.begin()) {
            auto prev = std::prev(free_it);
            if (prev->stop == new_sector.start) {
                // The previous free sector starts at the beginning of the freed sector. Merge them.
                new_sector.start = free_it->start;
                _free_size.erase(*free_it);
                _free_start.erase(free_it);
            }
        }

        _free_start.emplace_hint(free_it, new_sector);
        _free_size.emplace(new_sector);
    }
}

} // namespace Amulet
