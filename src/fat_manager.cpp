//
// Created by Sergei Filoniuk on 23/05/25.
//

#include "../include/fat_manager.h"

#include <unordered_set>

#include "../include/output.h"

FATManager::FATManager(VolumeManager &vol_manager)
    : vol_manager_(vol_manager), total_clusters_managed_(0),
      fat_disk_start_cluster_(0), fat_dist_clusters_count_(0) {
}

bool FATManager::initialize_and_flush(const FileSystem::Header &header) {
    total_clusters_managed_ = header.total_clusters;
    fat_disk_start_cluster_ = header.fat_start_cluster;
    fat_dist_clusters_count_ = header.fat_size_clusters;

    if (total_clusters_managed_ == 0) {
        output::err(output::prefix::FAT_MANAGER_ERROR) <<
                "FATManager Error: total_clusters_managed_ is 0. Cannot initialize"
                << std::endl;
        return false;
    }

    fat_table_.assign(total_clusters_managed_, FileSystem::MARKER_FAT_ENTRY_FREE);

    if (header.root_dir_size_clusters > 0 && header.root_dir_start_cluster < total_clusters_managed_) {
        fat_table_[header.root_dir_start_cluster] = FileSystem::MARKER_FAT_ENTRY_EOF;
    }

    if (!write_fat_to_disk()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "FATManager Error: Failed to write initialized FAT to disk" <<
                std::endl;
        return false;
    }
    output::succ(output::prefix::FAT_MANAGER) << "Initialized and flushed successfully" << std::endl;
    return true;
}

bool FATManager::load(const FileSystem::Header &header) {
    total_clusters_managed_ = header.total_clusters;
    fat_disk_start_cluster_ = header.fat_start_cluster;
    fat_dist_clusters_count_ = header.fat_size_clusters;

    if (total_clusters_managed_ == 0) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "FATManager Error: total_clusters_managed_ is 0. Cannot load"
                << std::endl;
        return false;
    }

    fat_table_.resize(total_clusters_managed_);

    if (!read_fat_from_disk()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "FATManager Error: Failed to load FAT from disk" << std::endl;
        return false;
    }

    output::succ(output::prefix::FAT_MANAGER) << "Loaded successfully" << std::endl;
    return true;
}

std::optional<uint32_t> FATManager::get_entry(const uint32_t cluster_idx) const {
    if (cluster_idx >= total_clusters_managed_) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "FATManager Error: Cluster index " << cluster_idx <<
                " out of bounds" << std::endl;
        return std::nullopt;
    }
    if (cluster_idx >= fat_table_.size()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "FATManager Error: Cluster index " << cluster_idx <<
                " out of bounds for fat_table_ (size: " <<
                fat_table_.size() << ")" << std::endl;
        return std::nullopt;
    }
    return fat_table_[cluster_idx];
}

bool FATManager::set_entry(const uint32_t cluster_idx, const uint32_t value) {
    if (!vol_manager_.is_open()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "FATManager Error: Volume not open" << std::endl;
        return false;
    }

    if (cluster_idx >= total_clusters_managed_) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "FATManager Error: Cluster index " << cluster_idx <<
                " out of bounds" << std::endl;
        return false;
    }

    if (cluster_idx >= fat_table_.size()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "FATManager Error: Cluster index " << cluster_idx <<
                " out of bounds for fat_table_ (size: " <<
                fat_table_.size() << ")" << std::endl;
        return false;
    }

    bool is_value_received = false;
    auto old_entry = get_entry(cluster_idx);
    if (!old_entry) {
        output::err(output::prefix::FAT_MANAGER_ERROR) <<
                "FATManager Error: Cannot get old value for this cluster index: " << cluster_idx << std::endl;
        is_value_received = false;
    } else is_value_received = true;
    fat_table_[cluster_idx] = value;
    if (!write_fat_to_disk()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) <<
                "FATManager Error: Failed to write FAT to disk after set_entry for cluster " << cluster_idx <<
                std::endl;
        if (is_value_received) {
            fat_table_[cluster_idx] = *old_entry;
        }
        return false;
    }
    return true;
}

std::list<uint32_t> FATManager::get_cluster_chain(const uint32_t start_cluster) const {
    std::list<uint32_t> chain;
    if (start_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || start_cluster == FileSystem::MARKER_FAT_ENTRY_EOF ||
        start_cluster >= total_clusters_managed_) {
        output::warn(output::prefix::FAT_MANAGER_WARNING) << "Cluster chain is empty" << std::endl;
        return chain;
    }
    std::unordered_set<uint32_t> visited_;
    uint32_t current_cluster = start_cluster;
    while (current_cluster != FileSystem::MARKER_FAT_ENTRY_EOF &&
           current_cluster != FileSystem::MARKER_FAT_ENTRY_FREE &&
           current_cluster < total_clusters_managed_) {
        visited_.insert(current_cluster);
        chain.push_back(current_cluster);
        if (current_cluster >= fat_table_.size()) break;
        current_cluster = fat_table_[current_cluster];
        if (chain.size() > total_clusters_managed_) {
            output::warn(output::prefix::FAT_MANAGER_WARNING) << "Potential loop in FAT chain detected starting at " <<
                    start_cluster
                    << std::endl;
            chain.clear();
            break;
        }
    }
    return chain;
}

bool FATManager::free_chain(const uint32_t start_cluster) {
    if (start_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || start_cluster == FileSystem::MARKER_FAT_ENTRY_EOF ||
        start_cluster >= total_clusters_managed_) {
        output::warn(output::prefix::FAT_MANAGER_WARNING) << "Nothing to clear" << std::endl;
        return true;
    }

    bool success = true;

    std::list<uint32_t> clusters_to_free;
    uint32_t tmp_current = start_cluster;

    while (tmp_current != FileSystem::MARKER_FAT_ENTRY_FREE && tmp_current != FileSystem::MARKER_FAT_ENTRY_EOF &&
           tmp_current < total_clusters_managed_ && tmp_current < fat_table_.size()) {
        clusters_to_free.push_back(tmp_current);
        tmp_current = fat_table_[tmp_current];
        if (clusters_to_free.size() > total_clusters_managed_) {
            output::err(output::prefix::FAT_MANAGER_ERROR) << "Loop detected in free_chain for start_cluster " <<
                    start_cluster <<
                    std::endl;
            return false;
        }
    }

    for (auto cluster_idx: clusters_to_free) {
        if (cluster_idx < fat_table_.size()) {
            if (!set_entry(cluster_idx, FileSystem::MARKER_FAT_ENTRY_FREE)) {
                output::err(output::prefix::FAT_MANAGER_ERROR) << "Failed to set FAT entry to free for cluster " <<
                        cluster_idx <<
                        std::endl;
                success = false;
            }
        } else {
            output::warn(output::prefix::FAT_MANAGER_WARNING) << "Invalid cluster index " << cluster_idx << std::endl;
            success = false;
        }
    }
    return success;
}

bool FATManager::append_to_chain(const uint32_t last_cluster_in_chain, const uint32_t new_cluster_idx) {
    if (last_cluster_in_chain >= total_clusters_managed_) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "Invalid last_cluster_in_chain: " << last_cluster_in_chain <<
                std::endl;
        return false;
    }
    if (new_cluster_idx == FileSystem::MARKER_FAT_ENTRY_FREE || new_cluster_idx == FileSystem::MARKER_FAT_ENTRY_EOF ||
        new_cluster_idx >= total_clusters_managed_) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "Invalid new_cluster_idx: " << new_cluster_idx << std::endl;
        return false;
    }

    if (!set_entry(new_cluster_idx, FileSystem::MARKER_FAT_ENTRY_EOF)) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "Failed to set new cluster " << new_cluster_idx << " as EOF"
                << std::endl;
        return false;
    }

    if (last_cluster_in_chain != FileSystem::MARKER_FAT_ENTRY_FREE && last_cluster_in_chain !=
        FileSystem::MARKER_FAT_ENTRY_EOF && last_cluster_in_chain < total_clusters_managed_) {
        if (!set_entry(last_cluster_in_chain, new_cluster_idx)) {
            output::err(output::prefix::FAT_MANAGER_ERROR) << "Failed to link cluster " << last_cluster_in_chain <<
                    " with " <<
                    new_cluster_idx << std::endl;
            set_entry(new_cluster_idx, FileSystem::MARKER_FAT_ENTRY_FREE);
            return false;
        }
    }

    return true;
}

bool FATManager::read_fat_from_disk() {
    if (fat_dist_clusters_count_ == 0 && fat_table_.empty()) return true;
    if (fat_dist_clusters_count_ == 0 && !fat_table_.empty()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) <<
                "total_clusters_managed_ is 0, but fat_table_ is not empty (size: " <<
                fat_table_.size() << ")" << std::endl;
        return false;
    }

    uint32_t cluster_size = vol_manager_.get_cluster_size();
    if (cluster_size == 0) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "Cluster size from VolumeManager is 0" << std::endl;
        return false;
    }

    std::vector<char> raw_fat_buffer(fat_dist_clusters_count_ * cluster_size);

    for (uint32_t i = 0; i < fat_dist_clusters_count_; ++i) {
        if (!vol_manager_.read_cluster(fat_disk_start_cluster_ + i, raw_fat_buffer.data() + i * cluster_size)) {
            output::err(output::prefix::FAT_MANAGER_ERROR) << "Failed to read cluster " << (fat_disk_start_cluster_ + i)
                    << " for FAT"
                    << std::endl;
            return false;
        }
    }
    uint64_t fat_table_size_bytes = static_cast<uint64_t>(fat_table_.size()) * sizeof(uint32_t);
    if (fat_table_size_bytes > raw_fat_buffer.size()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "fat_table_ expected size (" << fat_table_size_bytes <<
                " bytes) is larger then raw buffer read from disk ("
                << raw_fat_buffer.size() << " bytes)" << std::endl;
        return false;
    }
    std::memcpy(fat_table_.data(), raw_fat_buffer.data(), fat_table_size_bytes);
    return true;
}

bool FATManager::write_fat_to_disk() const {
    if (fat_dist_clusters_count_ == 0 && fat_table_.empty()) return true;
    if (fat_dist_clusters_count_ == 0 && !fat_table_.empty()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "total_clusters_managed_ is 0, but fat_table_ is not empty" <<
                std::endl;
        return false;
    }
    uint32_t cluster_size = vol_manager_.get_cluster_size();
    if (cluster_size == 0) {
        output::err(output::prefix::FAT_MANAGER_ERROR) <<
                "FATManager Error Cluster size from VolumeManager is 0 for writing" << std::endl;
        return false;
    }
    std::vector<char> raw_fat_buffer(fat_dist_clusters_count_ * cluster_size, 0);

    uint64_t fat_table_size_bytes = static_cast<uint64_t>(fat_table_.size()) * sizeof(uint32_t);

    if (fat_table_size_bytes > raw_fat_buffer.size()) {
        output::err(output::prefix::FAT_MANAGER_ERROR) << "fat_table_ expected size (" << fat_table_size_bytes <<
                " bytes) is larger then disk space allocated for FAT ("
                << raw_fat_buffer.size() << " bytes)" << std::endl;
        return false;
    }

    std::memcpy(raw_fat_buffer.data(), fat_table_.data(), fat_table_size_bytes);

    for (uint32_t i = 0; i < fat_dist_clusters_count_; ++i) {
        if (!vol_manager_.write_cluster(fat_disk_start_cluster_ + i, raw_fat_buffer.data() + i * cluster_size)) {
            output::err(output::prefix::FAT_MANAGER_ERROR) << "Failed to write cluster " << fat_disk_start_cluster_ + i
                    << " for FAT" << std::endl;
            return false;
        }
    }
    return true;
}
