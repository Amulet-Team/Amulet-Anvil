#pragma once

#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <utility>

#include <amulet/utils/logging.hpp>
#include <amulet/utils/threading/mutex.hpp>
#include <amulet/utils/threading/ordered_mutex.hpp>
#include <amulet/utils/threading/shared_mutex.hpp>
#include <amulet/utils/threading/thread_safety.hpp>

#include <amulet/nbt/tag/named_tag.hpp>

#include <amulet/anvil/dll.hpp>

#include "region.hpp"

namespace Amulet {

// An input iterator over region coordinates in a directory.
class AMULET_ANVIL_EXPORT AnvilRegionCoordIterator {
    // Not thread safe.
private:
    std::filesystem::directory_iterator it;
    std::pair<std::int64_t, std::int64_t> coord;

    void seek_to_valid();
    void seek_to_next_valid();

public:
    using difference_type = std::ptrdiff_t;
    using value_type = std::pair<std::int64_t, std::int64_t>;

    AnvilRegionCoordIterator();
    AnvilRegionCoordIterator(const std::filesystem::path&);
    const std::pair<std::int64_t, std::int64_t>& operator*() const;
    AnvilRegionCoordIterator& operator++();
    void operator++(int);
    friend AMULET_ANVIL_EXPORT bool operator==(const AnvilRegionCoordIterator&, const AnvilRegionCoordIterator&);
};

AMULET_ANVIL_EXPORT bool operator==(const AnvilRegionCoordIterator&, const AnvilRegionCoordIterator&);

static_assert(std::input_iterator<AnvilRegionCoordIterator>);

// An input iterator over chunk coordinates in a dimension layer.
// Layer's Read::SharedReadWrite lock required.
// Layer's Read::SharedReadOnly lock optional.
class AMULET_ANVIL_EXPORT AnvilChunkCoordIterator {
    // Not thread safe.
private:
    std::weak_ptr<class AnvilDimensionLayer> _layer;
    AnvilRegionCoordIterator _region_it;
    using coordsT = std::vector<std::pair<std::int64_t, std::int64_t>>;
    coordsT _coords;
    coordsT::iterator _coord_it;

    void seek_to_valid();
    void seek_to_next_valid();

public:
    using difference_type = std::ptrdiff_t;
    using value_type = std::pair<std::int64_t, std::int64_t>;

    AnvilChunkCoordIterator();
    AnvilChunkCoordIterator(std::shared_ptr<class AnvilDimensionLayer>);
    std::pair<std::int64_t, std::int64_t> operator*() const;
    AnvilChunkCoordIterator& operator++();
    void operator++(int);
    friend AMULET_ANVIL_EXPORT bool operator==(const AnvilChunkCoordIterator&, const AnvilChunkCoordIterator&);
};

AMULET_ANVIL_EXPORT bool operator==(const AnvilChunkCoordIterator&, const AnvilChunkCoordIterator&);

static_assert(std::input_iterator<AnvilChunkCoordIterator>);

// In the Anvil format chunk data is split into layers.
// Historically there was only one layer but entity data was split into its own layer.

// A class to manage a directory of region files.
class AMULET_ANVIL_EXPORT AnvilDimensionLayer {
private:
    Amulet::OrderedMutex _public_mutex;
    const std::filesystem::path _directory;
    const bool _mcc;
    astd::shared_mutex _mutex;
    std::map<std::pair<std::int64_t, std::int64_t>, std::shared_ptr<Amulet::AnvilRegion>> _regions ASTD_GUARDED_BY(_mutex);
    bool destroyed ASTD_GUARDED_BY(_mutex) = false;
    // TODO: This never removes region objects.
    // Perhaps _regions should store weak_ptr and a daque of shared_ptr to keep some alive.
    // _regions can be periodically cleaned to remove dead weak_ptrs.

public:
    // Constructors
    AnvilDimensionLayer() = delete;
    AnvilDimensionLayer(const AnvilDimensionLayer&) = delete;
    AnvilDimensionLayer(AnvilDimensionLayer&&) = delete;
    AnvilDimensionLayer(std::filesystem::path directory, bool mcc = false);

    // Destructor
    ~AnvilDimensionLayer();

    // Accessors

    // External mutex.
    // Thread safe.
    Amulet::OrderedMutex& get_mutex();

    // The directory this instance manages.
    // Thread safe.
    const std::filesystem::path& directory() const;

    // Is mcc file support enabled for this instance.
    // Thread safe.
    bool mcc() const;

    // Region

    // Get the path to the region file
    // Thread safe.
    std::filesystem::path region_path(std::int64_t rx, std::int64_t rz) const;

    // An iterator of all region coordinates in this layer.
    // External Read::SharedReadWrite lock required.
    // External Read::SharedReadOnly lock optional.
    AnvilRegionCoordIterator all_region_coords();

    // Check if a region file exists in this layer at given the coordinates.
    // External Read::SharedReadWrite lock required.
    // External Read::SharedReadOnly lock optional.
    bool has_region(std::int64_t rx, std::int64_t rz) const;

    // Check if a region file exists in this layer that contains the given chunk.
    // External Read::SharedReadWrite lock required.
    // External Read::SharedReadOnly lock optional.
    bool has_region_at_chunk(std::int64_t cx, std::int64_t cz) const;

    // Get an AnvilRegion instance from its coordinates. This must not be stored long-term.
    // Will throw RegionDoesNotExist if create is false and the region does not exist.
    // External Read::SharedReadWrite lock required if only calling Read methods on AnvilRegion.
    // External ReadWrite::SharedReadWrite lock required if calling ReadWrite methods on AnvilRegion.
    std::shared_ptr<AnvilRegion> get_region(std::int64_t rx, std::int64_t rz, bool create = false) ASTD_EXCLUDES(_mutex);

    // Get an AnvilRegion instance from chunk coordinates it contains. This must not be stored long-term.
    // Will throw RegionDoesNotExist if create is false and the region does not exist.
    // External Read::SharedReadWrite lock required if only calling Read methods on AnvilRegion.
    // External ReadWrite::SharedReadWrite lock required if calling ReadWrite methods on AnvilRegion.
    std::shared_ptr<AnvilRegion> get_region_at_chunk(std::int64_t cx, std::int64_t cz, bool create = false) ASTD_EXCLUDES(_mutex);

    // Chunk

    // Check if the chunk has data in this layer.
    // External Read::SharedReadWrite lock required.
    // External Read::SharedReadOnly lock optional.
    bool has_chunk(std::int64_t cx, std::int64_t cz) ASTD_EXCLUDES(_mutex);

    // Get the chunk data for this layer.
    // Will throw RegionEntryDoesNotExist if the chunk does not exist.
    // External Read::SharedReadWrite lock required.
    Amulet::NBT::NamedTag get_chunk_data(std::int64_t cx, std::int64_t cz) ASTD_EXCLUDES(_mutex);

    // Set the chunk data for this layer.
    // External ReadWrite::SharedReadWrite lock required.
    void set_chunk_data(std::int64_t cx, std::int64_t cz, const Amulet::NBT::NamedTag&) ASTD_EXCLUDES(_mutex);

    // Delete the chunk data from this layer.
    // External ReadWrite::SharedReadWrite lock required.
    void delete_chunk(std::int64_t cx, std::int64_t cz) ASTD_EXCLUDES(_mutex);

    // Defragment the region files and remove unused region files.
    // External ReadWrite::SharedReadOnly lock required.
    void compact() ASTD_EXCLUDES(_mutex);

    // Destroy the instance.
    // Calls made after this will fail.
    // This may only be called by the owner of the instance.
    // External ReadWrite:Unique lock required.
    void destroy() ASTD_EXCLUDES(_mutex);

    // Has the instance been destroyed.
    // If this is false, other calls will fail.
    // External Read:SharedReadWrite lock required.
    bool is_destroyed() ASTD_EXCLUDES(_mutex);
};

template <typename Range, typename T>
concept TypedInputRange = std::ranges::input_range<Range> && std::convertible_to<std::ranges::range_value_t<Range>, T>;

using JavaRawChunk = std::map<std::string, Amulet::NBT::NamedTag>;

class AMULET_ANVIL_EXPORT AnvilDimension {
private:
    Amulet::OrderedMutex _public_mutex;
    const std::filesystem::path _directory;
    const bool _mcc;
    astd::shared_mutex _mutex;
    std::map<std::string, std::shared_ptr<AnvilDimensionLayer>> _layers ASTD_GUARDED_BY(_mutex);
    const std::shared_ptr<AnvilDimensionLayer> _default_layer;
    bool destroyed ASTD_GUARDED_BY(_mutex) = false;

public:
    template <TypedInputRange<std::string> layersT>
    AnvilDimension(std::filesystem::path directory, layersT layer_names, bool mcc = false)
        : _directory(std::move(directory))
        , _mcc(mcc)
        , _layers([&]() {
            if (layer_names.begin() == layer_names.end()) {
                throw std::invalid_argument("layer_names must contain at least one name.");
            }
            std::map<std::string, std::shared_ptr<AnvilDimensionLayer>> layers;
            for (const auto& layer_name : layer_names) {
                layers.emplace(layer_name, std::make_shared<AnvilDimensionLayer>(_directory / layer_name, _mcc));
            };
            return layers;
        }())
        , _default_layer(_layers[*layer_names.begin()])
    {
    }

    // Destructor
    ~AnvilDimension();

    // External mutex.
    // Thread safe.
    Amulet::OrderedMutex& get_mutex();

    // The directory this dimension is in.
    // Thread safe.
    const std::filesystem::path& directory() const;

    // Are mcc files enabled for this dimension.
    // Thread safe.
    bool mcc() const;

    // Get the names of all layers in this dimension.
    // External Read::SharedReadWrite lock required.
    // External Read::SharedReadOnly lock optional.
    std::vector<std::string> layer_names() ASTD_EXCLUDES(_mutex);

    // Check if this dimension has the requested layer.
    // External Read::SharedReadWrite lock required.
    // External Read::SharedReadOnly lock optional.
    bool has_layer(const std::string& layer_name) ASTD_EXCLUDES(_mutex);

    // Get the AnvilDimensionLayer for a specific layer. The returned value must not be stored long-term.
    // If create=true the layer will be created if it doesn't exist.
    // External Read::SharedReadWrite lock required if only calling Read methods on AnvilDimensionLayer.
    // External ReadWrite::SharedReadWrite lock required if create=true or calling ReadWrite methods on AnvilDimensionLayer.
    std::shared_ptr<AnvilDimensionLayer> get_layer(const std::string& layer_name, bool create = false) ASTD_EXCLUDES(_mutex);

    // Get an iterator for all the chunks that exist in this dimension.
    // External Read::SharedReadWrite lock required.
    // External Read::SharedReadOnly lock optional.
    AnvilChunkCoordIterator all_chunk_coords() const ASTD_EXCLUDES(_mutex);

    // Check if a chunk exists.
    // External Read::SharedReadWrite lock required.
    // External Read::SharedReadOnly lock optional.
    bool has_chunk(std::int64_t cx, std::int64_t cz) const ASTD_EXCLUDES(_mutex);

    // Get the data for a chunk
    // External Read::SharedReadWrite lock required.
    JavaRawChunk get_chunk_data(std::int64_t cx, std::int64_t cz) ASTD_EXCLUDES(_mutex);

    // Set the data for a chunk.
    // data_layers can be any object supporting std::ranges::input_range of [std::string, Amulet::NBT::NamedTag || std::optional<Amulet::NBT::NamedTag>]
    // If the second value is a nullopt optional, the value will be deleted.
    // External ReadWrite::SharedReadWrite lock required.
    template <typename dataT>
    void set_chunk_data(std::int64_t cx, std::int64_t cz, const dataT& data_layers) ASTD_EXCLUDES(_mutex)
    {
        using ItT = std::ranges::range_value_t<dataT>;
        using NameT = std::remove_cv_t<std::tuple_element_t<0, ItT>>;
        using TagT = std::remove_cv_t<std::tuple_element_t<1, ItT>>;

        static_assert(std::is_same_v<NameT, std::string>);
        static_assert(std::is_same_v<TagT, Amulet::NBT::NamedTag> || std::is_same_v<TagT, std::optional<Amulet::NBT::NamedTag>>);

        bool missing = false;
        std::vector<std::pair<ItT, std::shared_ptr<AnvilDimensionLayer>>> layers;

        astd::shared_lock slock(_mutex);
        if (destroyed) {
            throw std::runtime_error("This AnvilDimension instance has been destroyed.");
        }

        // Iterate through each item and find the layer for that item.
        // If the layer does not exist, store a nullptr.
        for (const auto& data_layers_it : data_layers) {
            const auto& [layer_name, data] = data_layers_it;
            auto it = _layers.find(layer_name);
            if (it != _layers.end()) {
                layers.emplace_back(data_layers_it, it->second);
            } else {
                if constexpr (std::is_same_v<TagT, std::optional<Amulet::NBT::NamedTag>>) {
                    if (!data) {
                        // Do nothing because we were going to delte the data but the layer does not exist.
                        continue;
                    }
                }
                if (!std::all_of(layer_name.begin(), layer_name.end(), [](char c) { return 0x61 <= c && c <= 0x7A; })) {
                    error("Anvil layer " + layer_name + " contains characters not in the range a-z");
                    continue;
                }
                missing = true;
                layers.emplace_back(data_layers_it, nullptr);
            }
        }

        if (missing) {
            // The last stage found a layer that does not exist.
            // We need to switch to a unique lock to create the layer.
            slock.unlock();
            {
                astd::lock_guard ulock(_mutex);
                if (destroyed) {
                    throw std::runtime_error("This AnvilDimension instance has been destroyed.");
                }
                // Iterate through the data and create missing layers.
                for (auto& [data_layers_it, layer_ptr] : layers) {
                    if (!layer_ptr) {
                        const auto& [layer_name, data] = data_layers_it;
                        // The layer may have been created by another thread while we were waiting for the lock.
                        auto& layer_ptr_ref = _layers[layer_name];
                        if (!layer_ptr_ref) {
                            layer_ptr_ref = std::make_shared<AnvilDimensionLayer>(_directory / layer_name, _mcc);
                        }
                        layer_ptr = layer_ptr_ref;
                    }
                }
            }
            slock.lock();
            if (destroyed) {
                throw std::runtime_error("This AnvilDimension instance has been destroyed.");
            }
        }

        for (auto& [data_layers_it, layer_ptr] : layers) {
            const auto& [layer_name, data] = data_layers_it;
            auto& layer = *layer_ptr;
            OrderedLockGuard<ThreadAccessMode::ReadWrite, ThreadShareMode::SharedReadWrite> lock(layer.get_mutex());
            if constexpr (std::is_same_v<TagT, std::optional<Amulet::NBT::NamedTag>>) {
                if (data) {
                    layer.set_chunk_data(cx, cz, *data);
                } else {
                    layer.delete_chunk(cx, cz);
                }
            } else {
                static_assert(std::is_same_v<TagT, Amulet::NBT::NamedTag>);
                layer.set_chunk_data(cx, cz, data);
            }
        }
    }

    // Delete all data for the given chunk.
    // External ReadWrite::SharedReadWrite lock required.
    void delete_chunk(std::int64_t cx, std::int64_t cz) ASTD_EXCLUDES(_mutex);

    // Defragment the region files and remove unused region files.
    // External ReadWrite::SharedReadOnly lock required.
    void compact() ASTD_EXCLUDES(_mutex);

    // Destroy the instance.
    // Calls made after this will fail.
    // This may only be called by the owner of the instance.
    // External ReadWrite:Unique lock required.
    void destroy() ASTD_EXCLUDES(_mutex);

    // Has the instance been destroyed.
    // If this is false, other calls will fail.
    // External Read:SharedReadWrite lock required.
    bool is_destroyed() ASTD_EXCLUDES(_mutex);
};

} // namespace Amulet
