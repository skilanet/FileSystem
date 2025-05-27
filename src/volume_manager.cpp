#include "../include/volume_manager.h"

#include <iostream>

#include "output.h"

VolumeManager::VolumeManager() = default;

VolumeManager::~VolumeManager() {
    close_volume();
}

void VolumeManager::close_volume() {
    if (volume_stream_.is_open()) {
        volume_stream_.close();
    }
    is_volume_loaded_ = false;
    current_volume_path_.clear();
}

bool VolumeManager::is_open() const {
    return is_volume_loaded_ && volume_stream_.is_open();
}

uint32_t VolumeManager::get_cluster_size() const {
    if (!is_volume_loaded_) {
        return FileSystem::CLUSTER_SIZE_BYTES;
    }
    return header_cache_.cluster_size_bytes;
}

bool VolumeManager::create_and_format(const std::string &volume_path, const uint64_t volume_size_bytes,
                                      FileSystem::Header &out_header) {
    if (is_open()) {
        close_volume();
    }
    current_volume_path_ = volume_path;
    volume_stream_.open(current_volume_path_, std::ios::out | std::ios::binary | std::ios::trunc);

    if (!volume_stream_.is_open()) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Could not open file for format" << std::endl;
        return false;
    }
    if (volume_size_bytes == 0) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Volume size cannot be zero" << std::endl;
        close_volume();
        return false;
    }
    const auto offset = FileSystem::try_to_streamoff(volume_size_bytes - 1);
    if (!offset) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "File is too large for this system" << std::endl;
        return false;
    }
    volume_stream_.seekp(*offset);
    volume_stream_.write("\0", 1);
    if (!volume_stream_) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Could not set file size for: " << current_volume_path_ <<
                std::endl;
        close_volume();
        return false;
    }
    volume_stream_.flush();

    if (!initialize_header(volume_size_bytes, header_cache_)) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Could not initialize header structure" << std::endl;
        close_volume();
        return false;
    }

    out_header = header_cache_;

    if (!write_header_to_disk(header_cache_)) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Could not write header to disk" << std::endl;
        close_volume();
        return false;
    }
    is_volume_loaded_ = true;
    output::succ(output::prefix::VOLUME_MANAGER) << "Volume initialised and formatted successfully" << std::endl;

    return true;
}

bool VolumeManager::load_volume(const std::string &volume_path) {
    if (is_open()) {
        close_volume();
    }
    current_volume_path_ = volume_path;
    volume_stream_.open(current_volume_path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!volume_stream_.is_open()) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Could not open volume file: " << current_volume_path_ <<
                std::endl;
        return false;
    }

    if (!read_header_from_disk(header_cache_)) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Failed to read or validate header from: " <<
                current_volume_path_ <<
                std::endl;
        close_volume();
        return false;
    }

    is_volume_loaded_ = true;
    output::succ(output::prefix::VOLUME_MANAGER) << "Volume loaded successfully" << std::endl;
    return true;
}

bool VolumeManager::read_cluster(uint32_t cluster_idx, char *buffer) const {
    if (!is_open()) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Volume not open for reading cluster" << std::endl;
        return false;
    }

    if (cluster_idx >= header_cache_.total_clusters) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Cluster index " << cluster_idx << " out of bounds" <<
                std::endl;
        return false;
    }

    const auto cluster_offset = get_cluster_offset(cluster_idx);
    if (!cluster_offset) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Cluster offset is invalid" << std::endl;
        return false;
    }
    const auto offset = FileSystem::try_to_streamoff(*cluster_offset);
    if (!offset) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Offset is too large for this filesystem" << std::endl;
        return false;
    }
    volume_stream_.seekg(*offset);
    if (!volume_stream_) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Seekg failed for cluster" << cluster_idx << std::endl;
        return false;
    }
    volume_stream_.read(buffer, header_cache_.cluster_size_bytes);
    if (volume_stream_.gcount() != static_cast<std::streamsize>(header_cache_.cluster_size_bytes)) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Read failed for cluster " << cluster_idx <<
                ". Expected " << header_cache_.cluster_size_bytes << " got " << volume_stream_.gcount() << std::endl;
        if (!volume_stream_.eof()) volume_stream_.clear();
        return false;
    }
    return true;
}

bool VolumeManager::write_cluster(uint32_t cluster_idx, const char *buffer) const {
    if (!is_open()) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Volume not open for writing cluster" << std::endl;
        return false;
    }
    if (cluster_idx >= header_cache_.total_clusters) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Cluster index " << cluster_idx << " out of bounds" <<
                std::endl;
        return false;
    }
    const auto offset_opt = get_cluster_offset(cluster_idx);
    if (!offset_opt) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Offset is too large for this filesystem" << std::endl;
        return false;
    }
    const auto offset = FileSystem::try_to_streamoff(*offset_opt);
    volume_stream_.seekp(*offset);
    if (!volume_stream_) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Seekp failed for cluster" << cluster_idx << std::endl;
        return false;
    }
    volume_stream_.write(buffer, header_cache_.cluster_size_bytes);
    if (!volume_stream_) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Write failed for cluster" << cluster_idx << std::endl;
        return false;
    }
    volume_stream_.flush();
    return true;
}

const FileSystem::Header &VolumeManager::get_header() const {
    return header_cache_;
}

std::optional<uint64_t> VolumeManager::get_cluster_offset(const uint32_t cluster_idx) const {
    if (!is_volume_loaded_ || header_cache_.cluster_size_bytes == 0) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(cluster_idx) * header_cache_.cluster_size_bytes;
}

bool VolumeManager::initialize_header(const uint64_t volume_size_bytes, FileSystem::Header &header_to_fill) {
    std::memset(&header_to_fill, 0, sizeof(FileSystem::Header));
    strncpy(header_to_fill.signature, "FileSystem v1.0.0", sizeof(header_to_fill.signature) - 1);
    header_to_fill.signature[sizeof(header_to_fill.signature) - 1] = '\0';
    header_to_fill.volume_size_bytes = volume_size_bytes;
    header_to_fill.cluster_size_bytes = FileSystem::CLUSTER_SIZE_BYTES;

    if (header_to_fill.cluster_size_bytes == 0) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "CLUSTER_SIZE_BYTES is invalid" << std::endl;
        return false;
    }
    header_to_fill.total_clusters = volume_size_bytes / header_to_fill.cluster_size_bytes;

    if (header_to_fill.total_clusters < 10) {
        output::warn(output::prefix::VOLUME_MANAGER_WARNING) <<
                "Volume size is too small for minimum FS structures. Min 10 clusters need" <<
                std::endl;
        return false;
    }

    header_to_fill.header_cluster_count = 1;

    header_to_fill.bitmap_start_cluster = header_to_fill.header_cluster_count;
    const uint32_t bitmap_size_bits = header_to_fill.total_clusters;
    const uint32_t bitmap_size_bytes = (bitmap_size_bits + 7) / 8;
    header_to_fill.bitmap_size_cluster = (bitmap_size_bytes + header_to_fill.cluster_size_bytes - 1) / header_to_fill.
                                         cluster_size_bytes;

    header_to_fill.fat_start_cluster = header_to_fill.bitmap_start_cluster + header_to_fill.bitmap_size_cluster;
    constexpr uint32_t fat_entry_size = sizeof(uint32_t);
    const uint64_t total_fat_size_bytes = static_cast<uint64_t>(header_to_fill.total_clusters) * fat_entry_size;
    header_to_fill.fat_size_clusters = (total_fat_size_bytes + header_to_fill.cluster_size_bytes - 1) / header_to_fill.
                                       cluster_size_bytes;
    header_to_fill.root_dir_start_cluster = header_to_fill.fat_start_cluster + header_to_fill.fat_size_clusters;
    header_to_fill.root_dir_size_clusters = FileSystem::ROOT_DIRECTORY_CLUSTER_COUNT;

    header_to_fill.data_start_cluster = header_to_fill.root_dir_start_cluster + header_to_fill.root_dir_size_clusters;

    if (header_to_fill.data_start_cluster >= header_to_fill.total_clusters) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Not enough space for data clusters after metadata" <<
                std::endl;
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "" << header_to_fill.data_start_cluster <<
                ", total_clusters: " <<
                header_to_fill.total_clusters << std::endl;
        return false;
    }
    return true;
}

bool VolumeManager::write_header_to_disk(const FileSystem::Header &header_to_write) const {
    if (!volume_stream_.is_open()) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Stream not open for writing header" << std::endl;
        return false;
    }
    std::vector<char> cluster_buffer(header_to_write.cluster_size_bytes, 0);
    std::memcpy(cluster_buffer.data(), &header_to_write, sizeof(FileSystem::Header));

    volume_stream_.seekp(0);
    if (!volume_stream_) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Seekp to 0 for writing Header" << std::endl;
        return false;
    }
    volume_stream_.write(cluster_buffer.data(), header_to_write.cluster_size_bytes);
    if (!volume_stream_) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Writing header failed" << std::endl;
        return false;
    }
    volume_stream_.flush();
    return true;
}

bool VolumeManager::read_header_from_disk(FileSystem::Header &header_to_fill) const {
    if (!volume_stream_.is_open()) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Stream not open for reading header" << std::endl;
        return false;
    }
    std::vector<char> cluster_buffer(FileSystem::CLUSTER_SIZE_BYTES);
    volume_stream_.seekg(0);
    if (!volume_stream_) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Seekg to 0 failed for reading Header";
        return false;
    }
    volume_stream_.read(cluster_buffer.data(), FileSystem::CLUSTER_SIZE_BYTES);
    if (volume_stream_.gcount() != static_cast<std::streamsize>(FileSystem::CLUSTER_SIZE_BYTES)) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Read header failed. Read " << volume_stream_.gcount() <<
                " bytes" <<
                std::endl;
        return false;
    }
    std::memcpy(&header_to_fill, cluster_buffer.data(), sizeof(FileSystem::Header));

    if (std::string(header_to_fill.signature) != "FileSystem v1.0") {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Invalid file system signature" << std::endl;
        return false;
    }

    if (header_to_fill.cluster_size_bytes != FileSystem::CLUSTER_SIZE_BYTES) {
        output::err(output::prefix::VOLUME_MANAGER_ERROR) << "Mismatched cluster size. Expected "
                << FileSystem::CLUSTER_SIZE_BYTES << ", got " << header_to_fill.cluster_size_bytes << std::endl;
        return false;
    }
    return true;
}
