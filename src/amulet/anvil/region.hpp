#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <amulet/utils/mutex.hpp>
#include <amulet/utils/threading/mutex.hpp>
#include <amulet/utils/threading/thread_safety.hpp>

#include <amulet/nbt/tag/named_tag.hpp>

#include <amulet/anvil/dll.hpp>

#include "sector_manager.hpp"

namespace Amulet {

AMULET_ANVIL_EXPORT std::pair<std::int64_t, std::int64_t> parse_region_filename(const std::string& filename);

// A class to read and write Minecraft Java Edition Region files.
// Only one instance should exist per region file at any given time otherwise bad things may happen.
// This class is internally thread safe but a public mutex is provided to enable external synchronisation.
// Upstream locks from the level must also be adhered to.
class AMULET_ANVIL_EXPORT AnvilRegion {
public:
    class Impl;

    // A class to manage closing the region file.
    // When the instance is deleted the region file will be closed.
    // The region file can be manually closed before this is deleted.
    class AMULET_ANVIL_EXPORT FileCloser {
    private:
        // Data shared between the region and closer.
        std::shared_ptr<Impl> _impl;

    public:
        FileCloser(std::shared_ptr<Impl> shared);
        ~FileCloser();
    };

private:
    // The public mutex.
    Amulet::OrderedMutex _public_mutex;

    // Data shared between the region and closer.
    const std::shared_ptr<Impl> _impl;

    AnvilRegion(const std::filesystem::path& directory, const std::string& file_name, const std::pair<std::int64_t, std::int64_t>& region_coordinate, bool mcc = false);

public:
    // Constructors.
    AnvilRegion() = delete;
    AnvilRegion(const AnvilRegion&) = delete;
    AnvilRegion(AnvilRegion&&) = delete;

    // Construct from the directory path, name of the file and region coordinates.
    AnvilRegion(const std::filesystem::path& directory, const std::string& file_name, std::int64_t rx, std::int64_t rz, bool mcc = false);

    // Construct from the directory path and region coordinates.
    // File name is computed from region coordinates.
    AnvilRegion(const std::filesystem::path& directory, std::int64_t rx, std::int64_t rz, bool mcc = false);

    // Construct from the path to the region file.
    // Coordinates are computed from the file name.
    // File name must match "r.X.Z.mca".
    AnvilRegion(std::filesystem::path path, bool mcc = false);

    // Destructor
    ~AnvilRegion();

    // Assignment operators
    AnvilRegion& operator=(const AnvilRegion&) = delete;
    AnvilRegion& operator=(AnvilRegion&&) = delete;

    // A mutex which can be used to synchronise calls.
    // Thread safe.
    Amulet::OrderedMutex& get_mutex();

    // The path of the region file.
    // Thread safe.
    std::filesystem::path path() const;

    // The region x coordinate of the file.
    // Thread safe.
    std::int64_t rx() const;

    // The region z coordinate of the file.
    // Thread safe.
    std::int64_t rz() const;

    // Get the coordinates of all values in the region file.
    // Coordinates are in world space.
    // External Read:SharedReadWrite lock required.
    // External Read:SharedReadOnly lock optional.
    std::vector<std::pair<std::int64_t, std::int64_t>> get_coords();

    // Is the coordinate in the region.
    // This returns true even if there is no value for the coordinate.
    // Coordinates are in world space.
    // Thread safe.
    bool contains(std::int64_t cx, std::int64_t cz) const;

    // Is there a value stored for this coordinate.
    // Coordinates are in world space.
    // External Read:SharedReadWrite lock required.
    // External Read:SharedReadOnly lock optional.
    bool has_value(std::int64_t cx, std::int64_t cz);

    // Get the value for this coordinate.
    // Coordinates are in world space.
    // External Read:SharedReadWrite lock required.
    Amulet::NBT::NamedTag get_value(std::int64_t cx, std::int64_t cz);

    // AMULET_ANVIL_EXPORT std::vector<std::optional<Amulet::NBT::NamedTag>> get_batch(std::vector<std::pair<std::int64_t, std::int64_t>>& coords);

    // Set the value for this coordinate.
    // Coordinates are in world space.
    // External ReadWrite:SharedReadWrite lock required.
    void set_value(std::int64_t cx, std::int64_t cz, const Amulet::NBT::NamedTag& tag);

    // AMULET_ANVIL_EXPORT void set_batch(std::vector<std::tuple<std::int64_t, std::int64_t, Amulet::NBT::NamedTag>>& batch);

    // Delete the chunk data.
    // Coordinates are in world space.
    // External ReadWrite:SharedReadWrite lock required.
    void delete_value(std::int64_t cx, std::int64_t cz);

    // Delete multiple chunk's data.
    // Coordinates are in world space.
    // External ReadWrite:SharedReadWrite lock required.
    void delete_batch(std::vector<std::pair<std::int64_t, std::int64_t>>& coords);

    // Compact the region file.
    // Defragments the file and deletes unused space.
    // If there are no chunks remaining in the region file it will be deleted.
    // External ReadWrite:SharedReadWrite lock required.
    void compact();

    // Close the file object if open.
    // This is automatically called when the instance is destroyed but may be called earlier.
    // Thread safe.
    void close();

    // Destroy the instance.
    // Calls made after this will fail.
    // This may only be called by the owner of the instance.
    // External ReadWrite:Unique lock required.
    void destroy();

    // Has the instance been destroyed.
    // If this is false, other calls will fail.
    // External Read:SharedReadWrite lock required.
    bool is_destroyed();

    // Get the object responsible for closing the region file.
    // When this object is deleted it will close the region file
    // This means that holding a reference to this will delay when the region file is closed.
    // The region file may still be closed manually before this object is deleted.
    // Thread safe.
    std::shared_ptr<FileCloser> get_file_closer();
};

class AMULET_ANVIL_EXPORT RegionDoesNotExist : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    RegionDoesNotExist()
        : RegionDoesNotExist("RegionDoesNotExist")
    {
    }
    ~RegionDoesNotExist() noexcept override;
};

class AMULET_ANVIL_EXPORT RegionEntryDoesNotExist : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    RegionEntryDoesNotExist()
        : RegionEntryDoesNotExist("RegionDoesNotExist")
    {
    }
    ~RegionEntryDoesNotExist() noexcept override;
};

} // namespace Amulet
