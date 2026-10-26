#include <algorithm>
#include <list>
#include <map>
#include <mutex>
#include <vector>

#include <amulet/utils/threading/mutex.hpp>
#include <amulet/utils/threading/shared_mutex.hpp>
#include <amulet/utils/threading/thread_safety.hpp>

#include "dimension.hpp"
#include "region.hpp"

namespace Amulet {

// AnvilRegionCoordIterator
void AnvilRegionCoordIterator::seek_to_valid()
{
    for (; it != std::filesystem::directory_iterator(); it++) {
        try {
            if (!it->is_regular_file()) {
                continue;
            }
            coord = parse_region_filename(it->path().filename().string());
            return;
        } catch (const std::invalid_argument&) {
            continue;
        }
    }
}

void AnvilRegionCoordIterator::seek_to_next_valid()
{
    if (it != std::filesystem::directory_iterator()) {
        it++;
        seek_to_valid();
    }
}

AnvilRegionCoordIterator::AnvilRegionCoordIterator() { }

AnvilRegionCoordIterator::AnvilRegionCoordIterator(const std::filesystem::path& path)
    : it(path)
{
    // Seek to first valid region
    seek_to_valid();
}

const std::pair<std::int64_t, std::int64_t>& AnvilRegionCoordIterator::operator*() const
{
    return coord;
}

AnvilRegionCoordIterator& AnvilRegionCoordIterator::operator++()
{
    seek_to_next_valid();
    return *this;
}

void AnvilRegionCoordIterator::operator++(int)
{
    seek_to_next_valid();
}

bool operator==(const AnvilRegionCoordIterator& lhs, const AnvilRegionCoordIterator& rhs)
{
    return lhs.it == rhs.it;
}

// AnvilChunkCoordIterator

void AnvilChunkCoordIterator::seek_to_valid()
{
    for (; _region_it != AnvilRegionCoordIterator(); _region_it++) {
        const auto& [rx, rz] = *_region_it;
        std::shared_ptr<AnvilRegion> region;
        try {
            region = _layer->get_region(rx, rz);
        } catch (const RegionDoesNotExist&) {
            continue;
        }
        OrderedLockGuard<ThreadAccessMode::Read, ThreadShareMode::SharedReadWrite> region_lock(region->get_mutex());
        _coords = region->get_coords();
        _coord_index = 0;
        if (!_coords.empty()) {
            return;
        }
    }
}

void AnvilChunkCoordIterator::seek_to_next_valid()
{
    if (_coord_index < _coords.size()) {
        _coord_index++;
    }
    if (_coord_index >= _coords.size()) {
        if (_region_it == AnvilRegionCoordIterator()) {
            return;
        }
        _region_it++;
        seek_to_valid();
    }
}

AnvilChunkCoordIterator::AnvilChunkCoordIterator() { }

AnvilChunkCoordIterator::AnvilChunkCoordIterator(std::shared_ptr<AnvilDimensionLayer> layer)
    : _layer(layer ? std::move(layer) : throw std::invalid_argument("layer must not be a nullptr"))
    , _region_it(_layer->all_region_coords())
    , _coord_index(0)
{
    seek_to_valid();
}

std::pair<std::int64_t, std::int64_t> AnvilChunkCoordIterator::operator*() const
{
    return _coords[_coord_index];
}

AnvilChunkCoordIterator& AnvilChunkCoordIterator::operator++()
{
    seek_to_next_valid();
    return *this;
}

void AnvilChunkCoordIterator::operator++(int)
{
    seek_to_next_valid();
}

bool operator==(const AnvilChunkCoordIterator& lhs, const AnvilChunkCoordIterator& rhs)
{
    if (lhs._region_it == AnvilRegionCoordIterator()) {
        return rhs._region_it == AnvilRegionCoordIterator();
    } else {
        return (
            lhs._region_it == rhs._region_it
            && lhs._coords[lhs._coord_index] == rhs._coords[rhs._coord_index]);
    }
}

class AnvilRegionStorage {
public:
    astd::mutex mutex;

    bool destroyed ASTD_GUARDED_BY(mutex) = false;

private:
    struct Element;
    struct StrongElement;

    using LRUCache = std::list<StrongElement>;
    using CoordToElement = std::map<std::pair<std::int64_t, std::int64_t>, Element>;

    struct Element {
        std::weak_ptr<Amulet::AnvilRegion> weak_region;
        LRUCache::iterator lru_it;
    };

    struct StrongElement {
        std::shared_ptr<Amulet::AnvilRegion> shared_region;
        Element& element;
    };

    // Map from region coordinate to region weak ptr.
    CoordToElement _coord_to_element ASTD_GUARDED_BY(mutex);

    // LRU cache of regions
    LRUCache _lru_cache ASTD_GUARDED_BY(mutex);

public:
    // Get the region from its coordinates. Also returns the region evicted from the cache. Either may be nullptr.
    std::pair<std::shared_ptr<Amulet::AnvilRegion>, std::shared_ptr<Amulet::AnvilRegion>> get_region(std::int64_t rx, std::int64_t rz) ASTD_REQUIRES_UNIQUE(mutex);

    // Add a new region. get_region must return a nullptr.
    // Returns the AnvilRegion rejected from the cache which may be nullptr.
    std::shared_ptr<Amulet::AnvilRegion> add_region(std::shared_ptr<Amulet::AnvilRegion>) ASTD_REQUIRES_UNIQUE(mutex);

    // Remove the region.
    void on_region_deleted(std::int64_t rx, std::int64_t rz) ASTD_REQUIRES_UNIQUE(mutex);

    // Destroy all regions and clears data. Returns pointers to the regions so they can be deleted without the lock.
    std::vector<std::shared_ptr<Amulet::AnvilRegion>> destroy() ASTD_REQUIRES_UNIQUE(mutex);
};

std::pair<std::shared_ptr<Amulet::AnvilRegion>, std::shared_ptr<Amulet::AnvilRegion>> AnvilRegionStorage::get_region(std::int64_t rx, std::int64_t rz)
{
    // Find the region Element.
    const auto it = _coord_to_element.find(std::make_pair(rx, rz));
    if (it != _coord_to_element.end()) {
        // Element exists. Get region (may be nullptr)
        auto& element = it->second;
        auto region = element.weak_region.lock();
        if (region) {
            std::shared_ptr<Amulet::AnvilRegion> evicted_region;
            // If region exists, update LRU.
            if (element.lru_it == _lru_cache.end()) {
                // Not in LRU. Append.
                _lru_cache.emplace_front(region, element);
                element.lru_it = _lru_cache.begin();
                if (_lru_cache.size() > 64) {
                    // Limit LRU cache size.
                    auto& evicted_element = _lru_cache.back();
                    evicted_element.element.lru_it = _lru_cache.end();
                    evicted_region = std::move(evicted_element.shared_region);
                    _lru_cache.pop_back();
                }
            } else if (element.lru_it != _lru_cache.begin()) {
                // Not the first element. Move to the front.
                _lru_cache.splice(_lru_cache.begin(), _lru_cache, element.lru_it);
            }
            return { std::move(region), std::move(evicted_region) };
        }
    }
    // Element does not exist.
    return { nullptr, nullptr };
}

std::shared_ptr<Amulet::AnvilRegion> AnvilRegionStorage::add_region(std::shared_ptr<Amulet::AnvilRegion> region)
{
    auto coord = std::make_pair(region->rx(), region->rz());
    auto& element = _coord_to_element[coord];
    element.weak_region = region;
    _lru_cache.emplace_front(region, element);
    element.lru_it = _lru_cache.begin();

    std::shared_ptr<Amulet::AnvilRegion> evicted_region;
    if (_lru_cache.size() > 64) {
        // Limit LRU cache size.
        auto& evicted_element = _lru_cache.back();
        evicted_element.element.lru_it = _lru_cache.end();
        evicted_region = std::move(evicted_element.shared_region);
        _lru_cache.pop_back();
    }
    return evicted_region;
}

void AnvilRegionStorage::on_region_deleted(std::int64_t rx, std::int64_t rz)
{
    auto element_it = _coord_to_element.find({ rx, rz });
    if (element_it != _coord_to_element.end() && element_it->second.weak_region.expired()) {
        _coord_to_element.erase(element_it);
    }
}

std::vector<std::shared_ptr<Amulet::AnvilRegion>> AnvilRegionStorage::destroy()
{
    // Find all regions that still exist.
    std::vector<std::shared_ptr<Amulet::AnvilRegion>> regions;
    for (auto& it : _coord_to_element) {
        auto region_ptr = it.second.weak_region.lock();
        if (region_ptr) {
            regions.emplace_back(std::move(region_ptr));
        }
    }

    destroyed = true;

    // Clear memory
    _coord_to_element.clear();
    _lru_cache.clear();

    // Destroy regions
    for (auto& region_ptr : regions) {
        auto& region = *region_ptr;
        astd::lock_guard lock(region.get_mutex());
        region.destroy();
    }

    // Return shared pointers so they can be deleted without the lock.
    return regions;
}

// Subclass of AnvilRegion that unlinks itself upon deletion.
class AnvilRegion2 : public AnvilRegion {
    std::shared_ptr<AnvilRegionStorage> _shared;

public:
    AnvilRegion2(
        std::shared_ptr<AnvilRegionStorage> shared,
        const std::filesystem::path& directory,
        std::int64_t rx, std::int64_t rz,
        bool mcc);
    ~AnvilRegion2() override;
};

AnvilRegion2::AnvilRegion2(
    std::shared_ptr<AnvilRegionStorage> shared,
    const std::filesystem::path& directory,
    std::int64_t rx, std::int64_t rz,
    bool mcc)
    : AnvilRegion(directory, rx, rz, mcc)
    , _shared(std::move(shared))
{
}

AnvilRegion2::~AnvilRegion2()
{
    destroy();
    auto& shared = *_shared;
    astd::lock_guard lock(shared.mutex);
    shared.on_region_deleted(rx(), rz());
}

// AnvilDimensionLayer

AnvilDimensionLayer::AnvilDimensionLayer(
    std::filesystem::path directory, bool mcc)
    : _directory(directory)
    , _mcc(mcc)
    , _shared(std::make_shared<AnvilRegionStorage>())
{
    if (!std::filesystem::exists(_directory)) {
        std::filesystem::create_directories(_directory);
    } else if (!std::filesystem::is_directory(_directory)) {
        throw std::invalid_argument("AnvilDimensionLayer path is not a directory: " + _directory.string());
    }
}

AnvilDimensionLayer::~AnvilDimensionLayer()
{
    destroy();
}

bool AnvilDimensionLayer::is_valid_layer_name(const std::string& layer_name)
{
    if (layer_name.empty()) {
        // Must contain at least one character
        return false;
    }
    for (auto c : layer_name) {
        if (!(('a' <= c && c <= 'z') || c == '_')) {
            // All characters must be a-z or _
            return false;
        }
    }
    return true;
}

// Accessors

Amulet::OrderedMutex& AnvilDimensionLayer::get_mutex() { return _public_mutex; }
const std::filesystem::path& AnvilDimensionLayer::directory() const { return _directory; }
bool AnvilDimensionLayer::mcc() const { return _mcc; }

std::filesystem::path AnvilDimensionLayer::region_path(
    std::int64_t rx, std::int64_t rz) const
{
    return _directory / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".mca");
}

AnvilRegionCoordIterator AnvilDimensionLayer::all_region_coords()
{
    return AnvilRegionCoordIterator(_directory);
}

bool AnvilDimensionLayer::has_region(
    std::int64_t rx, std::int64_t rz) const
{
    return std::filesystem::is_regular_file(region_path(rx, rz));
}

bool AnvilDimensionLayer::has_region_at_chunk(
    std::int64_t cx, std::int64_t cz) const
{
    return has_region(cx >> 5, cz >> 5);
}

std::shared_ptr<AnvilRegion> AnvilDimensionLayer::get_region(
    std::int64_t rx, std::int64_t rz, bool create)
{
    // Get the region key
    auto key = std::make_pair(rx, rz);
    auto& shared = *_shared;

    // Defined before lock so it is destroyed after.
    std::shared_ptr<Amulet::AnvilRegion> evicted_region;

    astd::lock_guard lock(shared.mutex);
    if (shared.destroyed) {
        throw std::runtime_error("This AnvilDimensionLayer instance has been destroyed.");
    }

    // Find the region
    auto regions = shared.get_region(rx, rz);
    std::shared_ptr<Amulet::AnvilRegion> region = std::move(std::get<0>(regions));
    evicted_region = std::move(std::get<1>(regions));

    if (region) {
        // Return if it already exists.
        return region;
    } else if (create or has_region(rx, rz)) {
        region = std::make_shared<AnvilRegion2>(_shared, _directory, rx, rz, _mcc);
        evicted_region = shared.add_region(region);
        return region;
    } else {
        throw RegionDoesNotExist();
    }
}

std::shared_ptr<AnvilRegion> AnvilDimensionLayer::get_region_at_chunk(
    std::int64_t cx, std::int64_t cz, bool create)
{
    return get_region(cx >> 5, cz >> 5, create);
}

bool AnvilDimensionLayer::has_chunk(std::int64_t cx, std::int64_t cz)
{
    std::shared_ptr<AnvilRegion> region;
    try {
        region = get_region_at_chunk(cx, cz);
    } catch (const RegionDoesNotExist&) {
        return false;
    }
    OrderedLockGuard<ThreadAccessMode::Read, ThreadShareMode::SharedReadWrite> region_lock(region->get_mutex());
    return region->has_value(cx, cz);
}

Amulet::NBT::NamedTag AnvilDimensionLayer::get_chunk_data(std::int64_t cx, std::int64_t cz)
{
    std::shared_ptr<AnvilRegion> region;
    try {
        region = get_region_at_chunk(cx, cz);
    } catch (const RegionDoesNotExist&) {
        throw RegionEntryDoesNotExist("Chunk " + std::to_string(cx) + ", " + std::to_string(cz) + " does not exist.");
    }
    OrderedLockGuard<ThreadAccessMode::Read, ThreadShareMode::SharedReadWrite> region_lock(region->get_mutex());
    return region->get_value(cx, cz);
}

void AnvilDimensionLayer::set_chunk_data(std::int64_t cx, std::int64_t cz, const Amulet::NBT::NamedTag& tag)
{
    auto region = get_region_at_chunk(cx, cz, true);
    OrderedLockGuard<ThreadAccessMode::ReadWrite, ThreadShareMode::SharedReadWrite> region_lock(region->get_mutex());
    return region->set_value(cx, cz, tag);
}

void AnvilDimensionLayer::delete_chunk(std::int64_t cx, std::int64_t cz)
{
    std::shared_ptr<AnvilRegion> region;
    try {
        region = get_region_at_chunk(cx, cz);
    } catch (const RegionDoesNotExist&) {
        return;
    }
    OrderedLockGuard<ThreadAccessMode::ReadWrite, ThreadShareMode::SharedReadWrite> region_lock(region->get_mutex());
    region->delete_value(cx, cz);
}

void AnvilDimensionLayer::compact()
{
    // TODO: Threads
    // TODO: CancelManager
    for (auto it = all_region_coords(); it != AnvilRegionCoordIterator(); it++) {
        auto [rx, rz] = *it;
        std::shared_ptr<AnvilRegion> region_ptr;
        try {
            region_ptr = get_region(rx, rz);
        } catch (const RegionDoesNotExist&) {
            continue;
        }
        auto& region = *region_ptr;
        OrderedLockGuard<ThreadAccessMode::ReadWrite, ThreadShareMode::SharedReadWrite> region_lock(region.get_mutex());
        region.compact();
    }
}

void AnvilDimensionLayer::destroy()
{
    std::vector<std::shared_ptr<Amulet::AnvilRegion>> evicted_regions;

    auto& shared = *_shared;
    astd::lock_guard lock(shared.mutex);
    shared.destroyed = true;

    evicted_regions = shared.destroy();
}

bool AnvilDimensionLayer::is_destroyed()
{
    auto& shared = *_shared;
    astd::lock_guard lock(shared.mutex);
    return shared.destroyed;
}

Amulet::OrderedMutex& AnvilDimension::get_mutex() { return _public_mutex; }

const std::filesystem::path& AnvilDimension::directory() const { return _directory; }

bool AnvilDimension::mcc() const { return _mcc; }

std::vector<std::string> AnvilDimension::layer_names()
{
    astd::shared_lock lock(_mutex);
    std::vector<std::string> layers;
    layers.reserve(_layers.size());
    for (const auto& node : _layers) {
        layers.emplace_back(node.first);
    }
    return layers;
}

AnvilDimension::AnvilDimension(AnvilDimensionDataTuple data, bool mcc)
    : _directory(std::move(std::get<0>(data)))
    , _mcc(mcc)
    , _layers(std::move(std::get<1>(data)))
    , _default_layer(std::move(std::get<2>(data)))
{
}

AnvilDimension::~AnvilDimension()
{
    destroy();
}

bool AnvilDimension::has_layer(const std::string& layer_name)
{
    astd::shared_lock lock(_mutex);
    return _layers.contains(layer_name);
}

std::shared_ptr<AnvilDimensionLayer> AnvilDimension::get_layer(const std::string& layer_name, bool create)
{
    {
        astd::shared_lock lock(_mutex);
        if (destroyed) {
            throw std::runtime_error("This AnvilDimension instance has been destroyed.");
        }
        auto it = _layers.find(layer_name);
        if (it != _layers.end()) {
            return it->second;
        } else if (!create) {
            throw std::invalid_argument("No layer exists with name " + layer_name);
        }
    }
    {
        astd::lock_guard lock(_mutex);
        if (destroyed) {
            throw std::runtime_error("This AnvilDimension instance has been destroyed.");
        }
        // Find the layer
        auto it = _layers.find(layer_name);
        if (it == _layers.end()) {
            if (!AnvilDimensionLayer::is_valid_layer_name(layer_name)) {
                throw std::invalid_argument("Anvil layer " + layer_name + " contains characters not matching [_a-z]");
            }
            // Initialise it if it did not exist.
            it = _layers.emplace(layer_name, std::make_shared<AnvilDimensionLayer>(_directory / layer_name, _mcc)).first;
        }
        return it->second;
    }
}

AnvilChunkCoordIterator AnvilDimension::all_chunk_coords() const
{
    return AnvilChunkCoordIterator(_default_layer);
}

bool AnvilDimension::has_chunk(std::int64_t cx, std::int64_t cz) const
{
    OrderedLockGuard<ThreadAccessMode::Read, ThreadShareMode::SharedReadWrite> layer_lock(_default_layer->get_mutex());
    return _default_layer->has_chunk(cx, cz);
}

JavaRawChunk AnvilDimension::get_chunk_data(std::int64_t cx, std::int64_t cz)
{
    astd::shared_lock lock(_mutex);
    JavaRawChunk chunk_data;
    for (const auto& [layer_name, layer_ptr] : _layers) {
        auto& layer = *layer_ptr;
        OrderedLockGuard<ThreadAccessMode::Read, ThreadShareMode::SharedReadWrite> layer_lock(layer.get_mutex());
        try {
            chunk_data.emplace(layer_name, layer.get_chunk_data(cx, cz));
        } catch (const RegionEntryDoesNotExist&) {
        }
    }
    if (chunk_data.empty()) {
        if (destroyed) {
            throw std::runtime_error("This AnvilDimension instance has been destroyed.");
        }
        throw RegionEntryDoesNotExist();
    }
    return chunk_data;
}

void AnvilDimension::delete_chunk(std::int64_t cx, std::int64_t cz)
{
    astd::shared_lock lock(_mutex);
    for (const auto& it : _layers) {
        auto& layer = *it.second;
        OrderedLockGuard<ThreadAccessMode::ReadWrite, ThreadShareMode::SharedReadWrite> layer_lock(layer.get_mutex());
        layer.delete_chunk(cx, cz);
    }
}

void AnvilDimension::compact()
{
    astd::shared_lock lock(_mutex);
    for (const auto& it : _layers) {
        auto& layer = *it.second;
        OrderedLockGuard<ThreadAccessMode::Read, ThreadShareMode::SharedReadWrite> layer_lock(layer.get_mutex());
        layer.compact();
    }
}

void AnvilDimension::destroy()
{
    astd::lock_guard layers_lock(_mutex);
    destroyed = true;

    // Destroy all region instances.
    for (auto& it : _layers) {
        auto& layer = *it.second;
        astd::lock_guard layer_lock(layer.get_mutex());
        layer.destroy();
    }
    _layers.clear();
}

bool AnvilDimension::is_destroyed()
{
    astd::shared_lock lock(_mutex);
    return destroyed;
}

} // namespace Amulet
