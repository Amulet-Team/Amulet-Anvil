#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <list>
#include <regex>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <lz4.h>

#include <amulet/utils/logging.hpp>
#include <amulet/utils/threading/mutex.hpp>
#include <amulet/utils/threading/thread_safety.hpp>

#include <amulet/zlib/zlib.hpp>

#include <amulet/nbt/nbt_encoding/binary.hpp>

#include <amulet/anvil/dll.hpp>

#include "region.hpp"

using namespace Amulet::NBT;

namespace Amulet {

template <typename T>
static void little_endian_swap(T& value)
{
    if constexpr (std::endian::native != std::endian::little) {
        char* vv = reinterpret_cast<char*>(&value);
        std::reverse(vv, vv + sizeof(T));
    }
}

template <typename T>
static void big_endian_swap(T& value)
{
    if constexpr (std::endian::native != std::endian::big) {
        char* vv = reinterpret_cast<char*>(&value);
        std::reverse(vv, vv + sizeof(T));
    }
}

static constexpr std::uint64_t SectorSize = 0x1000;
static constexpr std::uint64_t MaxRegionSize = SectorSize * 255; // The maximum size data in the region file can be
static constexpr std::array<char, SectorSize * 2> EmptyHeader { };

class FileCloserCache {
private:
    using WeakImpl = std::weak_ptr<AnvilRegion::Impl>;
    using SharedCloser = std::shared_ptr<AnvilRegion::FileCloser>;
    using Pair = std::pair<WeakImpl, SharedCloser>;

    astd::mutex _mutex;
    const size_t _max_size;
    std::list<Pair> _values ASTD_GUARDED_BY(_mutex);
    std::map<WeakImpl, typename std::list<Pair>::iterator, std::owner_less<WeakImpl>> _map ASTD_GUARDED_BY(_mutex);

    void remove_extra() ASTD_REQUIRES_UNIQUE(_mutex)
    {
        while (_max_size < _values.size()) {
            _map.erase(_values.front().first);
            _values.pop_front();
        }
    }

public:
    FileCloserCache(size_t max_size)
        : _max_size(max_size) { };

    // Add an item.
    void add(WeakImpl k, SharedCloser v) ASTD_EXCLUDES(_mutex)
    {
        astd::lock_guard lock(_mutex);
        auto it = _map.find(k);
        if (it == _map.end()) {
            // Create and insert the value
            _values.emplace_back(k, std::move(v));
            _map.emplace(k, --_values.end());
            remove_extra();
        } else {
            // Move the value to the end.
            _values.splice(_values.end(), _values, it->second);
        }
    };

    // Remove an item.
    void remove(WeakImpl k) ASTD_EXCLUDES(_mutex)
    {
        astd::lock_guard lock(_mutex);
        auto it = _map.find(k);
        if (it != _map.end()) {
            _values.erase(it->second);
            _map.erase(it);
        }
    }
};

static FileCloserCache region_file_cache(64);

std::pair<std::int64_t, std::int64_t> parse_region_filename(const std::string& filename)
{
    static const std::regex region_regex(R"(^r\.(\-?\d+)\.(\-?\d+)\.mca$)");
    std::smatch match;
    if (!std::regex_search(filename, match, region_regex)) {
        throw std::invalid_argument("Region filename is invalid.");
    }
    return std::make_pair(std::stoll(match[1]), std::stoll(match[2]));
}

class AnvilRegion::Impl : public std::enable_shared_from_this<AnvilRegion::Impl> {
public:
    // The directory the region file is in.
    const std::filesystem::path dir;
    const std::filesystem::path path;

    // The region coordinates.
    const std::int64_t rx;
    const std::int64_t rz;

    // Is support for .mcc files enabled.
    const bool mcc;

    // This mutex must be acquired to access the following attributes.
    astd::recursive_mutex mutex;

    // The region file handle
    std::fstream regionf ASTD_GUARDED_BY(mutex);

    // A class to track which sectors are reserved.
    // Null if it has not been synchronised with the file.
    std::optional<SectorManager> sector_manager ASTD_GUARDED_BY(mutex);

    // A map from the chunk coordinate to the location on disk
    std::map<std::pair<std::int64_t, std::int64_t>, Sector> chunk_locations ASTD_GUARDED_BY(mutex);

    // Has the region been marked as destroyed.
    bool destroyed ASTD_GUARDED_BY(mutex) = false;

    // Region file closer
    astd::mutex file_closer_mutex;
    std::weak_ptr<AnvilRegion::FileCloser> file_closer_ref ASTD_GUARDED_BY(file_closer_mutex);

    Impl(
        std::filesystem::path dir,
        std::filesystem::path path,
        std::int64_t rx,
        std::int64_t rz,
        const bool mcc);

    // Load data from the region file if it exists.
    void read_file_header() ASTD_REQUIRES_UNIQUE(mutex);

    // Create the region file.
    void create_region_file() ASTD_REQUIRES_UNIQUE(mutex);

    // Open the region file and fix any size issues.
    void open_region_file() ASTD_REQUIRES_UNIQUE(mutex);

    // Create or open the region file if it is closed.
    void create_open_region_file_if_closed() ASTD_REQUIRES_UNIQUE(mutex);

    void validate_coord(std::int64_t cx, std::int64_t cz) const;

    // Set chunk data.
    // Caller must ensure the file is open.
    template <typename T>
    void set_data(std::int64_t cx, std::int64_t cz, T data) ASTD_REQUIRES_UNIQUE(mutex);

    // Close the file object.
    // This is automatically called when the instance is destroyed but may be called earlier.
    void _close() ASTD_REQUIRES_UNIQUE(mutex);

    // Close the file object if open.
    // This is automatically called when the instance is destroyed but may be called earlier.
    void _close_if_open() ASTD_REQUIRES_UNIQUE(mutex);

    // Get the coordinates of all values in the region file.
    // Coordinates are in world space.
    // External Read:SharedReadWrite lock required.
    // External Read:SharedReadOnly lock optional.
    std::vector<std::pair<std::int64_t, std::int64_t>> get_coords() ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Is the coordinate in the region.
    // This returns true even if there is no value for the coordinate.
    // Coordinates are in world space.
    // Thread safe.
    bool contains(std::int64_t cx, std::int64_t cz) const ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Is there a value stored for this coordinate.
    // Coordinates are in world space.
    // External Read:SharedReadWrite lock required.
    // External Read:SharedReadOnly lock optional.
    bool has_value(std::int64_t cx, std::int64_t cz) ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Get the value for this coordinate.
    // Coordinates are in world space.
    // External Read:SharedReadWrite lock required.
    Amulet::NBT::NamedTag get_value(std::int64_t cx, std::int64_t cz) ASTD_EXCLUDES(mutex, file_closer_mutex);

    // AMULET_ANVIL_EXPORT std::vector<std::optional<Amulet::NBT::NamedTag>> get_batch(std::vector<std::pair<std::int64_t, std::int64_t>>& coords);

    // Set the value for this coordinate.
    // Coordinates are in world space.
    // External ReadWrite:SharedReadWrite lock required.
    void set_value(std::int64_t cx, std::int64_t cz, const Amulet::NBT::NamedTag& tag) ASTD_EXCLUDES(mutex, file_closer_mutex);

    // AMULET_ANVIL_EXPORT void set_batch(std::vector<std::tuple<std::int64_t, std::int64_t, Amulet::NBT::NamedTag>>& batch);

    // Delete the chunk data.
    // Coordinates are in world space.
    // External ReadWrite:SharedReadWrite lock required.
    void delete_value(std::int64_t cx, std::int64_t cz) ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Delete multiple chunk's data.
    // Coordinates are in world space.
    // External ReadWrite:SharedReadWrite lock required.
    void delete_batch(std::vector<std::pair<std::int64_t, std::int64_t>>& coords) ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Compact the region file.
    // Defragments the file and deletes unused space.
    // If there are no chunks remaining in the region file it will be deleted.
    // External ReadWrite:SharedReadWrite lock required.
    void compact() ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Close the file object if open.
    // This is automatically called when the instance is destroyed but may be called earlier.
    // Thread safe.
    void close() ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Destroy the instance.
    // Calls made after this will fail.
    // This may only be called by the owner of the instance.
    // External ReadWrite:Unique lock required.
    void destroy() ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Has the instance been destroyed.
    // If this is false, other calls will fail.
    // External Read:SharedReadWrite lock required.
    bool is_destroyed() ASTD_EXCLUDES(mutex, file_closer_mutex);

    // Get the object responsible for closing the region file.
    // When this object is deleted it will close the region file
    // This means that holding a reference to this will delay when the region file is closed.
    // The region file may still be closed manually before this object is deleted.
    // Thread safe.
    std::shared_ptr<FileCloser> get_file_closer() ASTD_EXCLUDES(mutex, file_closer_mutex);
};

AnvilRegion::Impl::Impl(
    std::filesystem::path dir,
    std::filesystem::path path,
    std::int64_t rx,
    std::int64_t rz,
    const bool mcc)
    : dir(std::move(dir))
    , path(std::move(path))
    , rx(rx)
    , rz(rz)
    , mcc(mcc)
{
}

void AnvilRegion::Impl::create_region_file()
{
    regionf.open(path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    if (!regionf) {
        throw std::runtime_error("Could not open file " + path.string());
    }
    regionf.write(EmptyHeader.data(), EmptyHeader.size());
    if (!regionf) {
        regionf.close();
        throw std::runtime_error("Failed writing to region file " + path.string());
    }
}

void AnvilRegion::Impl::open_region_file()
{
    regionf.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!regionf) {
        throw std::runtime_error("Could not open file " + path.string());
    }
    regionf.seekp(0, std::ios::end);
    size_t file_size = regionf.tellp();
    if (file_size < SectorSize * 2) {
        // if the length of the region file is less than 8KiB extend it to 8KiB
        regionf.write(EmptyHeader.data(), EmptyHeader.size() - file_size);
        if (!regionf) {
            regionf.close();
            throw std::runtime_error("Failed writing to region file " + path.string());
        }
    } else if (file_size & 0xFFF) {
        // ensure the file is a multiple of 4096 bytes
        regionf.write(EmptyHeader.data(), (file_size | 0xFFF) + 1 - file_size);
        if (!regionf) {
            regionf.close();
            throw std::runtime_error("Failed writing to region file " + path.string());
        }
    }
}

void AnvilRegion::Impl::create_open_region_file_if_closed()
{
    if (!regionf.is_open()) {
        if (std::filesystem::is_regular_file(path)) {
            open_region_file();
        } else {
            create_region_file();
        }
    }
}

void AnvilRegion::Impl::read_file_header()
{
    if (sector_manager) {
        // Already loaded.
        return;
    }

    if (destroyed) {
        throw std::runtime_error("This AnvilRegion instance has been destroyed.");
    }

    // Load the region data
    sector_manager = SectorManager(0, SectorSize * 2);
    sector_manager->reserve(Sector(0, SectorSize * 2));

    if (std::filesystem::is_regular_file(path)) {
        if (!regionf.is_open()) {
            open_region_file();
        }
        // Read the location table header.
        regionf.seekg(0);
        std::vector<std::uint32_t> location_table(1024);
        regionf.read(reinterpret_cast<char*>(location_table.data()), 4096);
        // Convert from big endian to native endianness
        for (auto& v : location_table) {
            big_endian_swap(v);
        }
        for (size_t cx = 0; cx < 32; cx++) {
            for (size_t cz = 0; cz < 32; cz++) {
                const auto& sector_data = location_table[cx + cz * 32];
                if (sector_data) {
                    size_t sector_offset = (sector_data >> 8) * SectorSize;
                    size_t sector_size = (sector_data & 0xFF) * SectorSize;
                    Sector sector(sector_offset, sector_offset + sector_size);
                    sector_manager->reserve(sector);
                    chunk_locations.emplace(std::make_pair(cx + rx * 32, cz + rz * 32), sector);
                }
            }
        }
    }
}

void AnvilRegion::Impl::_close()
{
    regionf.close();
    region_file_cache.remove(weak_from_this());
}

void AnvilRegion::Impl::_close_if_open()
{
    if (regionf.is_open()) {
        regionf.close();
    }
    region_file_cache.remove(weak_from_this());
}

void AnvilRegion::Impl::close()
{
    std::lock_guard lock(mutex);
    _close_if_open();
}

void AnvilRegion::Impl::destroy()
{
    std::lock_guard lock(mutex);
    destroyed = true;
    _close_if_open();
    sector_manager = std::nullopt;
    chunk_locations.clear();
}

bool AnvilRegion::Impl::is_destroyed()
{
    std::lock_guard lock(mutex);
    return destroyed;
}

std::vector<std::pair<std::int64_t, std::int64_t>> AnvilRegion::Impl::get_coords()
{
    std::lock_guard lock(mutex);
    auto closer = get_file_closer();
    read_file_header();
    std::vector<std::pair<std::int64_t, std::int64_t>> coords;
    coords.reserve(chunk_locations.size());
    for (const auto& it : chunk_locations) {
        coords.push_back(it.first);
    }
    return coords;
}

bool AnvilRegion::Impl::contains(std::int64_t cx, std::int64_t cz) const
{
    return rx * 32 <= cx && cx < (rx + 1) * 32 && rz * 32 <= cz && cz < (rz + 1) * 32;
}

void AnvilRegion::Impl::validate_coord(std::int64_t cx, std::int64_t cz) const
{
    if (!contains(cx, cz)) {
        throw std::invalid_argument(
            "Chunk coordinate " + std::to_string(cx) + ", " + std::to_string(cz) + " is not in region " + std::to_string(rx) + ", " + std::to_string(rz));
    }
}

bool AnvilRegion::Impl::has_value(std::int64_t cx, std::int64_t cz)
{
    validate_coord(cx, cz);
    std::lock_guard lock(mutex);
    auto closer = get_file_closer();
    read_file_header();
    return chunk_locations.contains(std::make_pair(cx, cz));
}

static const std::string LZ4_MAGIC = "LZ4Block";
static const char COMPRESSION_METHOD_RAW = 0x10;
static const char COMPRESSION_METHOD_LZ4 = 0x20;

// Decompress lz4 compressed data from src into dst.
static void decompress_lz4(const std::string_view src, std::string& dst)
{
    // https://github.com/lz4/lz4-java/blob/7c931bef32d179ec3d3286ee71638b23ebde3459/src/java/net/jpountz/lz4/LZ4BlockInputStream.java#L200
    size_t index = 0;
    while (index < src.size()) {
        if (src.size() < index + 21) {
            throw std::invalid_argument("Corrupt lz4 data. Needed 21 bytes for the header.");
        }
        const std::string_view magic = src.substr(index, 8);
        if (magic != LZ4_MAGIC) {
            throw std::invalid_argument("LZ4 compressed block does not start with LZ4Block.");
        }
        char compression_method = src[index + 8] & 0xF0;
        std::int32_t compressed_length = *reinterpret_cast<const std::int32_t*>(&src[index + 9]);
        little_endian_swap(compressed_length);
        std::int32_t original_length = *reinterpret_cast<const std::int32_t*>(&src[index + 13]);
        little_endian_swap(original_length);
        index += 21;
        if (
            original_length < 0
            || compressed_length < 0
            || (original_length == 0 and compressed_length != 0)
            || (original_length != 0 and compressed_length == 0)) {
            throw std::invalid_argument("LZ4 compressed block is corrupted.");
        }
        switch (compression_method) {
        case COMPRESSION_METHOD_RAW: {
            if (original_length != compressed_length) {
                throw std::invalid_argument("LZ4 compressed block is corrupted.");
            }
            dst.append(src.substr(index, original_length));
            index += original_length;
            break;
        }
        case COMPRESSION_METHOD_LZ4: {
            size_t buf_index = dst.size();
            dst.resize(dst.size() + original_length);
            LZ4_decompress_safe(&src[index], &dst[buf_index], compressed_length, original_length);
            index += compressed_length;
            break;
        }
        default:
            throw std::invalid_argument("LZ4 compressed block is corrupted.");
        }
    }
}

// Decompress the data according to the compression type
static NamedTag decompress(char compression_type, const std::string_view& data)
{
    switch (compression_type) {
    case 1: // GZIP
    case 2: // Deflate
    {
        std::string dst;
        zlib::decompress_zlib_gzip(data, dst);
        return decode_nbt(dst, std::endian::big, mutf8_to_utf8);
    }
    case 3: // None
        return decode_nbt(data, std::endian::big, mutf8_to_utf8);
    case 4: // LZ4
    {
        std::string dst;
        decompress_lz4(data, dst);
        return decode_nbt(dst, std::endian::big, mutf8_to_utf8);
    }
    default:
        throw std::runtime_error("Unknown chunk compression format " + std::to_string(static_cast<std::int16_t>(compression_type)));
    }
}

NamedTag AnvilRegion::Impl::get_value(std::int64_t cx, std::int64_t cz)
{
    validate_coord(cx, cz);
    std::lock_guard lock(mutex);
    auto closer = get_file_closer();
    read_file_header();
    auto it = chunk_locations.find(std::make_pair(cx, cz));
    if (it == chunk_locations.end()) {
        throw RegionEntryDoesNotExist("Chunk " + std::to_string(cx) + ", " + std::to_string(cz) + " does not exist.");
    }
    create_open_region_file_if_closed();
    if (!regionf.seekg(it->second.start)) {
        throw std::runtime_error("Failed seeking.");
    }

    // Read the size of the buffer.
    std::uint32_t buffer_size;
    if (!regionf.read(reinterpret_cast<char*>(&buffer_size), sizeof(std::uint32_t))) {
        throw std::runtime_error("Failed reading size.");
    }
    big_endian_swap(buffer_size);

    if (buffer_size < 1 || buffer_size > MaxRegionSize) {
        throw std::runtime_error("Invalid buffer size " + std::to_string(buffer_size) + ".");
    }

    // Read the buffer.
    std::string buffer(buffer_size, 0);
    if (!regionf.read(buffer.data(), buffer_size)) {
        throw std::runtime_error("Failed reading buffer.");
    }

    if (mcc && buffer[0] & 128) {
        // mcc files are supported and external bit is set.
        std::filesystem::path mcc_path = dir / ("c." + std::to_string(cx) + "." + std::to_string(cz) + ".mcc");
        std::ifstream mccf(mcc_path, std::ios::in | std::ios::binary);
        if (!mccf) {
            throw std::runtime_error("Could not open file " + mcc_path.string());
        }
        std::stringstream mccbuffer;
        mccbuffer << mccf.rdbuf();
        return decompress(buffer[0] & 127, mccbuffer.view());
    } else {
        return decompress(buffer[0], std::string_view(buffer).substr(1));
    }
}

template <typename T>
void AnvilRegion::Impl::set_data(std::int64_t cx, std::int64_t cz, T data)
{
    // Find the old sector
    std::optional<Sector> old_sector;
    auto old_sector_it = chunk_locations.find(std::make_pair(cx, cz));
    if (old_sector_it != chunk_locations.end()) {
        old_sector = old_sector_it->second;
        chunk_locations.erase(old_sector_it);
    }

    bool mcc_overwritten = false;

    std::uint32_t location = 0;
    if constexpr (std::is_same_v<T, std::string_view>) {
        // Write the new chunk data
        char format_byte = 0;
        if (data.size() + 4 > MaxRegionSize) {
            // save externally (if mcc files are not supported the check at the top will filter large files out)
            mcc_overwritten = true;
            std::filesystem::path mcc_path = dir / ("c." + std::to_string(cx) + "." + std::to_string(cz) + ".mcc");
            std::ofstream mccf(mcc_path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!mccf) {
                throw std::runtime_error("Could not open file " + mcc_path.string());
            }
            mccf.write(&data[1], data.size() - 1);
            format_byte = data[0] | 128;
            data = std::string_view(reinterpret_cast<char*>(&format_byte), 1);
        }

        // Find how big the sector needs to be.
        size_t data_size = data.size() + 4;
        size_t sector_length = data_size;
        if (sector_length & 0xFFF) {
            sector_length = (sector_length | 0xFFF) + 1;
        }
        // Reserve a sector large enough to fit the data.
        auto sector = sector_manager->reserve_space(sector_length);
        if (sector.start & 0xFFF) {
            throw std::runtime_error("Sector size is not a multiple of 0x1000.");
        }
        chunk_locations.emplace(std::make_pair(cx, cz), sector);
        // Seek to the sector to write to
        regionf.seekp(sector.start);
        // Write the size value
        std::uint32_t data_size_buffer = static_cast<std::uint32_t>(data_size);
        big_endian_swap(data_size_buffer);
        regionf.write(reinterpret_cast<char*>(&data_size_buffer), 4);
        // Write the data
        regionf.write(data.data(), data.size());
        // Pad to sector_length
        size_t pad_size = sector_length - data_size;
        if (pad_size) {
            std::string padding(pad_size, 0);
            regionf.write(padding.data(), pad_size);
        }
        // Create the location value
        location = static_cast<std::uint32_t>((sector.start >> 4) + (sector_length >> 12));
        big_endian_swap(location);
    }

    // Write header data
    regionf.seekp(4 * (cx - rx * 32 + (cz - rz * 32) * 32));
    regionf.write(reinterpret_cast<char*>(&location), 4);
    regionf.seekg(SectorSize - 4, std::ios::cur);
    std::uint32_t t = static_cast<std::uint32_t>(std::time(NULL));
    big_endian_swap(t);
    regionf.write(reinterpret_cast<char*>(&t), 4);

    // Only do this after updating the header so that the file is always in a valid state.
    if (old_sector) {
        if (mcc && !mcc_overwritten) {
            // Delete the old external mcc file
            std::filesystem::path mcc_path = dir / ("c." + std::to_string(cx) + "." + std::to_string(cz) + ".mcc");
            if (std::filesystem::is_regular_file(mcc_path)) {
                std::filesystem::remove(mcc_path);
            }
        }
        // Free the old sector
        sector_manager->free(*old_sector);
    }
}

void AnvilRegion::Impl::set_value(std::int64_t cx, std::int64_t cz, const NamedTag& tag)
{
    validate_coord(cx, cz);
    // Encode the tag
    BinaryWriter writer(
        std::endian::big,
        &utf8_to_mutf8);
    encode_nbt(writer, tag);
    const std::string& bnbt = writer.get_buffer();

    // Create the output string
    std::string data;
    // Reserve space to avoid resizing buffer
    data.reserve(bnbt.size());
    // zlib compression
    data.push_back(2);
    // Compress
    zlib::compress_zlib(bnbt, data);

    if (!mcc && data.size() + 4 > MaxRegionSize) {
        // Skip saving large chunks if mcc files are not enabled.
        Amulet::warning(
            "Could not save data to chunk "
            + std::to_string(cx)
            + ", "
            + std::to_string(cz)
            + " in region file "
            + path.string()
            + " because it was too large.");
        return;
    }

    std::lock_guard lock(mutex);
    auto closer = get_file_closer();
    read_file_header();
    create_open_region_file_if_closed();
    set_data<std::string_view>(cx, cz, data);
}

void AnvilRegion::Impl::delete_value(std::int64_t cx, std::int64_t cz)
{
    validate_coord(cx, cz);
    std::lock_guard lock(mutex);
    if (!std::filesystem::is_regular_file(path)) {
        // Do nothing if there is no file.
        return;
    }
    auto closer = get_file_closer();
    read_file_header();
    create_open_region_file_if_closed();
    set_data<std::nullopt_t>(cx, cz, std::nullopt);
}

void AnvilRegion::Impl::delete_batch(std::vector<std::pair<std::int64_t, std::int64_t>>& coords)
{
    std::lock_guard lock(mutex);
    if (!std::filesystem::is_regular_file(path)) {
        // Do nothing if there is no file.
        return;
    }
    auto closer = get_file_closer();
    read_file_header();
    create_open_region_file_if_closed();

    for (const auto& [cx, cz] : coords) {
        if (contains(cx, cz)) {
            set_data<std::nullopt_t>(cx, cz, std::nullopt);
        }
    }
}

void AnvilRegion::Impl::compact()
{
    std::lock_guard lock(mutex);
    if (!std::filesystem::is_regular_file(path)) {
        // Do nothing if there is no file.
        return;
    }

    auto closer = get_file_closer();
    read_file_header();
    if (chunk_locations.empty()) {
        // No chunks in the region file. Delete it.
        _close_if_open();
        std::filesystem::remove(path);
        return;
    }

    // Sort by start position.
    struct SectorStartSort {
        bool operator()(
            const std::tuple<size_t, std::pair<std::int64_t, std::int64_t>, Sector>& a,
            const std::tuple<size_t, std::pair<std::int64_t, std::int64_t>, Sector>& b) const
        {
            return std::get<2>(a).start < std::get<2>(b).start;
        }
    };

    // Generate a list of sectors in sequential order
    // location header index, chunk coordinate, sector
    std::set<
        std::tuple<size_t, std::pair<std::int64_t, std::int64_t>, Sector>,
        SectorStartSort>
        chunk_sectors;
    for (const auto& [coord, sector] : chunk_locations) {
        chunk_sectors.emplace(
            4 * (coord.first - rx * 32 + (coord.second - rz * 32) * 32),
            std::make_pair(coord.first, coord.second),
            sector);
    }

    // Set the position to the end of the header
    size_t file_position = 2 * SectorSize;
    size_t file_end = std::get<2>(*chunk_sectors.rbegin()).stop;

    create_open_region_file_if_closed();

    while (!chunk_sectors.empty()) {
        // While there are remaining sectors, get the first sector.
        const auto [header_index, chunk_coordinate, sector] = *chunk_sectors.begin();
        chunk_sectors.erase(chunk_sectors.begin());

        if (file_position == sector.start) {
            // There isn't any space before the sector. Do nothing.
            file_position = sector.stop;
        } else {
            // There is space before the sector
            Sector new_sector;
            if (file_position + sector.length() <= sector.start) {
                // There is enough space before the sector to fit the whole sector.
                // Copy it to the new location
                new_sector = Sector(file_position, file_position + sector.length());
                file_position = new_sector.stop;
            } else {
                // There is space before the sector but not enough to fit the sector.
                // Move it to the end for processing later.
                new_sector = Sector(file_end, file_end + sector.length());
                file_end = new_sector.stop;
                chunk_sectors.emplace(header_index, chunk_coordinate, new_sector);
            }

            // Read in the data
            std::string data(sector.length(), 0);
            regionf.seekg(sector.start);
            regionf.read(data.data(), data.size());

            // Reserve and write the data to the new sector
            sector_manager->reserve(new_sector);
            regionf.seekp(new_sector.start);
            regionf.write(data.data(), data.size());

            // Update the index
            std::uint32_t location = static_cast<std::uint32_t>((new_sector.start >> 4) + (new_sector.length() >> 12));
            big_endian_swap(location);
            regionf.seekp(header_index);
            regionf.write(reinterpret_cast<char*>(&location), 4);

            // Update internal state
            chunk_locations[chunk_coordinate] = new_sector;
            sector_manager->free(sector);
        }
    }
    _close();
    // Delete any unused data at the end.
    std::filesystem::resize_file(path, file_position);
}

std::shared_ptr<AnvilRegion::FileCloser> AnvilRegion::Impl::get_file_closer()
{
    std::lock_guard closer_lock(file_closer_mutex);
    std::shared_ptr<AnvilRegion::FileCloser> file_closer = file_closer_ref.lock();
    if (!file_closer) {
        file_closer = std::make_shared<AnvilRegion::FileCloser>(shared_from_this());
        file_closer_ref = file_closer;
    }
    region_file_cache.add(weak_from_this(), file_closer);
    return file_closer;
}

// Constructors.
AnvilRegion::AnvilRegion(
    const std::filesystem::path& directory,
    const std::string& file_name,
    std::int64_t rx,
    std::int64_t rz,
    bool mcc)
    : _impl(std::make_shared<AnvilRegion::Impl>(
          directory,
          directory / file_name,
          rx,
          rz,
          mcc))
{
}

AnvilRegion::AnvilRegion(
    const std::filesystem::path& directory,
    const std::string& file_name,
    const std::pair<std::int64_t, std::int64_t>& region_coordinate,
    bool mcc)
    : AnvilRegion(
          directory,
          file_name,
          region_coordinate.first,
          region_coordinate.second,
          mcc)
{
}

AnvilRegion::AnvilRegion(
    const std::filesystem::path& directory,
    std::int64_t rx,
    std::int64_t rz,
    bool mcc)
    : AnvilRegion(
          directory,
          "r." + std::to_string(rx) + "." + std::to_string(rz) + ".mca",
          rx, rz, mcc)
{
}

AnvilRegion::AnvilRegion(std::filesystem::path path, bool mcc)
    : AnvilRegion(
          path.parent_path(),
          path.filename().string(),
          parse_region_filename(path.filename().string()),
          mcc)
{
}

AnvilRegion::~AnvilRegion()
{
    destroy();
}

Amulet::OrderedMutex& AnvilRegion::get_mutex() { return _public_mutex; }

std::filesystem::path AnvilRegion::path() const { return _impl->path; }

std::int64_t AnvilRegion::rx() const { return _impl->rx; }

std::int64_t AnvilRegion::rz() const { return _impl->rz; }

void AnvilRegion::close()
{
    _impl->close();
}

void AnvilRegion::destroy()
{
    _impl->destroy();
}

bool AnvilRegion::is_destroyed()
{
    return _impl->is_destroyed();
}

std::vector<std::pair<std::int64_t, std::int64_t>> AnvilRegion::get_coords()
{
    return _impl->get_coords();
}

bool AnvilRegion::contains(std::int64_t cx, std::int64_t cz) const
{
    return _impl->contains(cx, cz);
}

bool AnvilRegion::has_value(std::int64_t cx, std::int64_t cz)
{
    return _impl->has_value(cx, cz);
}

NamedTag AnvilRegion::get_value(std::int64_t cx, std::int64_t cz)
{
    return _impl->get_value(cx, cz);
}

void AnvilRegion::set_value(std::int64_t cx, std::int64_t cz, const NamedTag& tag)
{
    _impl->set_value(cx, cz, tag);
}

void AnvilRegion::delete_value(std::int64_t cx, std::int64_t cz)
{
    _impl->delete_value(cx, cz);
}

void AnvilRegion::delete_batch(std::vector<std::pair<std::int64_t, std::int64_t>>& coords)
{
    _impl->delete_batch(coords);
}

void AnvilRegion::compact()
{
    _impl->compact();
}

std::shared_ptr<AnvilRegion::FileCloser> AnvilRegion::get_file_closer()
{
    return _impl->get_file_closer();
}

AnvilRegion::FileCloser::FileCloser(std::shared_ptr<Impl> impl)
    : _impl(std::move(impl))
{
}

AnvilRegion::FileCloser::~FileCloser()
{
    auto& impl = *_impl;
    std::lock_guard lock(impl.mutex);
    if (impl.regionf.is_open()) {
        impl.regionf.close();
    }
    // This will not be called if the FileCloser is in the cache so we don't need to remove it.
}

RegionDoesNotExist::~RegionDoesNotExist() noexcept { }
RegionEntryDoesNotExist::~RegionEntryDoesNotExist() noexcept { }

} // namespace Amulet
