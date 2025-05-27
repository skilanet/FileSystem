#include "../include/bitmap_manager.h"

#include <iostream>
#include <cstring>

#include "../include/output.h"

BitmapManager::BitmapManager(VolumeManager &volume_manager)
    : volume_mgr_(volume_manager), total_clusters_managed_(0),
      bitmap_disk_start_cluster_(0), bitmap_disk_cluster_count_(0) {
}

bool BitmapManager::initialize_and_flush(const FileSystem::Header &header) {
    total_clusters_managed_ = header.total_clusters;
    bitmap_disk_start_cluster_ = header.bitmap_start_cluster;
    bitmap_disk_cluster_count_ = header.bitmap_size_cluster;

    const uint32_t bitmap_size_in_bytes = (total_clusters_managed_ + 7) / 8;
    bitmap_data_.assign(bitmap_size_in_bytes, 0);

    for (uint32_t i = 0; i < header.header_cluster_count; ++i) {
        if (i < total_clusters_managed_) set_bit(i);
    }

    for (uint32_t i = 0; i < header.bitmap_size_cluster; ++i) {
        if (const uint32_t cluster_idx = header.bitmap_start_cluster + i; cluster_idx < total_clusters_managed_)
            set_bit(cluster_idx);
    }

    for (uint32_t i = 0; i < header.fat_size_clusters; ++i) {
        if (const uint32_t cluster_idx = header.fat_start_cluster + i; cluster_idx < total_clusters_managed_)
            set_bit(cluster_idx);
    }

    for (uint32_t i = 0; i < header.root_dir_size_clusters; ++i) {
        if (const uint32_t cluster_idx = header.root_dir_start_cluster + i; cluster_idx < total_clusters_managed_)
            set_bit(cluster_idx);
    }

    if (!write_bitmap_to_disk()) {
        output::err(output::prefix::BITMAP_MANAGER) << "Failed to write initialized bitmap to disk" << std::endl;
        return false;
    }

    output::succ(output::prefix::BITMAP_MANAGER) << "Initialized and flushed successfully." << std::endl;
    return true;
}

bool BitmapManager::load(const FileSystem::Header &header) {
    total_clusters_managed_ = header.total_clusters;
    bitmap_disk_start_cluster_ = header.bitmap_start_cluster;
    bitmap_disk_cluster_count_ = header.bitmap_size_cluster;

    const uint32_t bitmap_size_in_bytes = (total_clusters_managed_ + 7) / 8;
    bitmap_data_.resize(bitmap_size_in_bytes);

    if (!read_bitmap_from_disk()) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Failed to load bitmap from disk" << std::endl;
        return false;
    }
    output::succ(output::prefix::BITMAP_MANAGER) << "Loaded successfully" << std::endl;
    return true;
}

std::optional<uint32_t> BitmapManager::find_and_allocate_free_cluster() {
    if (!volume_mgr_.is_open()) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Volume not open" << std::endl;
        return std::nullopt;
    }

    const auto &header = volume_mgr_.get_header();

    for (uint32_t i = header.data_start_cluster; i < total_clusters_managed_; ++i) {
        const auto received_bit = get_bit(i);
        if (!received_bit) {
            output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Cannot get bit " << i;
            return std::nullopt;
        }
        if (!*received_bit) {
            set_bit(i);
            if (!write_bitmap_to_disk()) {
                clear_bit(i);
                output::err(output::prefix::BITMAP_MANAGER_ERROR) <<
                        "Failed to write bitmap to disk after allocating cluster " << i <<
                        std::endl;
                return std::nullopt;
            }
            return i;
        }
    }
    output::warn(output::prefix::BITMAP_MANAGER_WARNING) << "No free clusters found" << std::endl;
    return std::nullopt;
}

bool BitmapManager::free_cluster(uint32_t cluster_idx) {
    if (!volume_mgr_.is_open()) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Volume not open" << std::endl;
        return false;
    }
    if (cluster_idx >= total_clusters_managed_) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Cluster index " << cluster_idx << " out of bounds" <<
                std::endl;
        return false;
    }

    if (const auto &header = volume_mgr_.get_header(); cluster_idx < header.data_start_cluster) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Attempting to free a metadate cluster " << cluster_idx <<
                std::endl;
        return false;
    }
    const auto received_bit = get_bit(cluster_idx);
    if (!received_bit) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Cannot get bit " << cluster_idx;
        return false;
    }
    if (!*received_bit) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Cluster " << cluster_idx << " is already free" <<
                std::endl;
    }

    clear_bit(cluster_idx);
    if (!write_bitmap_to_disk()) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Failed to write bitmap to disk after freeing cluster " <<
                cluster_idx <<
                std::endl;
        return false;
    }
    return true;
}

bool BitmapManager::is_cluster_free(uint32_t cluster_idx) const {
    if (cluster_idx >= total_clusters_managed_) {
        return false;
    }
    const auto received_bit = get_bit(cluster_idx);
    if (!received_bit) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Cannot get bit " << cluster_idx;
        return false;
    }
    return !*received_bit;
}

void BitmapManager::set_bit(const uint32_t cluster_idx) {
    if (cluster_idx >= total_clusters_managed_) return;
    const uint32_t byte_idx = cluster_idx / 8;
    const uint8_t bit_offset = cluster_idx % 8;
    if (byte_idx < bitmap_data_.size()) {
        bitmap_data_[byte_idx] |= 1 << bit_offset;
    }
}

void BitmapManager::clear_bit(uint32_t cluster_idx) {
    if (cluster_idx >= total_clusters_managed_) return;
    uint32_t byte_idx = cluster_idx / 8;
    uint8_t bit_offset = cluster_idx % 8;
    if (byte_idx < bitmap_data_.size()) {
        bitmap_data_[byte_idx] &= ~(1 << bit_offset);
    }
}

std::optional<bool> BitmapManager::get_bit(uint32_t cluster_idx) const {
    if (cluster_idx >= total_clusters_managed_) return std::nullopt;
    const uint32_t byte_idx = cluster_idx / 8;
    const uint8_t bit_offset = cluster_idx % 8;
    if (byte_idx < bitmap_data_.size()) {
        return (bitmap_data_[byte_idx] >> bit_offset) & 1;
    }
    return std::nullopt;
}

bool BitmapManager::read_bitmap_from_disk() {
    if (bitmap_disk_cluster_count_ == 0) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "total_clusters is 0, cannot read" << std::endl;
        return bitmap_data_.empty();
    }
    std::vector<char> raw_bitmap_buffer(bitmap_disk_cluster_count_ * volume_mgr_.get_cluster_size());

    for (uint32_t i = 0; i < bitmap_disk_cluster_count_; ++i) {
        if (!volume_mgr_.read_cluster(bitmap_disk_start_cluster_ + i,
                                      raw_bitmap_buffer.data() + i * volume_mgr_.get_cluster_size())) {
            output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Failed to read cluster " << (
                        bitmap_disk_cluster_count_ + i)
                    << " for bitmap" << std::endl;
            return false;
        }
    }
    if (bitmap_data_.size() > raw_bitmap_buffer.size()) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "bitmap_data_ vector is larger than buffer read from disk"
                << std::endl;
        return false;
    }
    std::memcpy(bitmap_data_.data(), raw_bitmap_buffer.data(), bitmap_data_.size());
    return true;
}

bool BitmapManager::write_bitmap_to_disk() const {
    if (bitmap_disk_cluster_count_ == 0 && bitmap_data_.empty()) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "No bitmap data to write" << std::endl;
        return false;
    }
    if (bitmap_disk_cluster_count_ == 0 && !bitmap_data_.empty()) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) << "total_clusters_ is 0, but bitmap_data_ is not empty" <<
                std::endl;
        return false;
    }
    std::vector<char> raw_bitmap_buffer(bitmap_disk_cluster_count_ * volume_mgr_.get_cluster_size(), 0);

    if (bitmap_data_.size() > raw_bitmap_buffer.size()) {
        output::err(output::prefix::BITMAP_MANAGER_ERROR) <<
                "bitmap_data_ vector is larger then disk space allocated for bitmap" <<
                std::endl;
    }
    std::memcpy(raw_bitmap_buffer.data(), bitmap_data_.data(),
                std::min(bitmap_data_.size(), raw_bitmap_buffer.size()));

    for (uint32_t i = 0; i < bitmap_disk_cluster_count_; ++i) {
        if (!volume_mgr_.write_cluster(bitmap_disk_start_cluster_ + i,
                                       raw_bitmap_buffer.data() + i * volume_mgr_.get_cluster_size())) {
            output::err(output::prefix::BITMAP_MANAGER_ERROR) << "Failed to write cluster " << (
                        bitmap_disk_start_cluster_ + i) <<
                    " for bitmap" << std::endl;
            return false;
        }
    }
    return true;
}
