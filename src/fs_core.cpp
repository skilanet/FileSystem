#include "../include/fs_core.h"

FileSystemCore::FileSystemCore(): mounted_(false), next_handle_id(1) {
}

FileSystemCore::~FileSystemCore() {
    unmount();
}

bool FileSystemCore::isMounted() const {
    return mounted_;
}

void FileSystemCore::unmount() {
    if (mounted_) {
        std::vector<uint32_t> handle_ids(0, opened_files_table_.size());
        for (const auto &[fst, snd]: opened_files_table_) {
            handle_ids.push_back(fst);
        }
        for (const uint32_t id: handle_ids) {
            close_file(id);
        }
        opened_files_table_.clear();

        vol_manager_.close_volume();

        bitmap_manager_.reset();
        fat_manager_.reset();
        directory_manager_.reset();
        mounted_ = false;

        output::succ(output::prefix::FILE_SYSTEM_CORE) << "Volume unmounted" << std::endl;
    }
}

bool FileSystemCore::format(const std::string &volume_path, uint64_t volume_size_mb) {
    if (mounted_) {
        unmount();
    }
    const uint64_t volume_size_bytes = volume_size_mb * 1024 * 1024;
    if (volume_size_bytes == 0) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Volume size cannot be a 0" << std::endl;
        return false;
    }
    FileSystem::Header _header_tmp{};
    if (!vol_manager_.create_and_format(volume_path, volume_size_bytes, _header_tmp)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "VolumeManager failed to create and format" << std::endl;
        return false;
    }

    header_ = _header_tmp;

    bitmap_manager_ = std::make_unique<BitmapManager>(vol_manager_);
    if (!bitmap_manager_->initialize_and_flush(header_)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "BitmapManager failed to initialize" << std::endl;
        return false;
    }

    fat_manager_ = std::make_unique<FATManager>(vol_manager_);
    if (!fat_manager_->initialize_and_flush(header_)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "FATManager failed to initialize" << std::endl;
        return false;
    }

    directory_manager_ = std::make_unique<DirectoryManager>(vol_manager_, *fat_manager_, *bitmap_manager_);
    if (!directory_manager_->initialize_root_directory(header_)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "DirectoryManager failed to initialize" << std::endl;
        return false;
    }
    if (header_.root_dir_size_clusters > 0) {
        if (fat_manager_->set_entry(header_.root_dir_start_cluster, FileSystem::MARKER_FAT_ENTRY_EOF)) {
            output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) <<
                    "Failed to set root directory first cluster as EOF in FAT" << std::endl;
            return false;
        }
    }
    output::succ(output::prefix::FILE_SYSTEM_CORE) << "Filesystem formatted successfully" << std::endl;
    vol_manager_.close_volume();
    return true;
}

bool FileSystemCore::mount(const std::string &volume_path) {
    if (mounted_) {
        unmount();
    }
    if (!vol_manager_.load_volume(volume_path)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "VolumeManager filed to load volume" << std::endl;
        return false;
    }

    header_ = vol_manager_.get_header();

    bitmap_manager_ = std::make_unique<BitmapManager>(vol_manager_);
    if (!bitmap_manager_->load(header_)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "BitmapManager failed to load" << std::endl;
        vol_manager_.close_volume();
        return false;
    }

    fat_manager_ = std::make_unique<FATManager>(vol_manager_);
    if (!fat_manager_->load(header_)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "FATManager failed to load" << std::endl;
        vol_manager_.close_volume();
        return false;
    }

    directory_manager_ = std::make_unique<DirectoryManager>(vol_manager_, *fat_manager_, *bitmap_manager_);

    mounted_ = true;
    output::succ(output::prefix::FILE_SYSTEM_CORE) << "Volume mounted successfully from " << volume_path << std::endl;
    return true;
}

std::optional<FileSystemCore::OpenMode> FileSystemCore::parse_mode(const std::string &mode) {
    OpenMode open_mode;
    switch (mode) {
        case "r": open_mode.read = true;
            break;
        case "w": open_mode.write = true;
            open_mode.truncate = true;
            open_mode.create_if_not_exists = true;
            break;
        case "a": open_mode.write = true;
            open_mode.append = true;
            open_mode.create_if_not_exists = true;
            break;
        case "r+": open_mode.read = true;
            open_mode.write = true;
            break;
        case "w+": open_mode.read = true;
            open_mode.write = true;
            open_mode.truncate = true;
            open_mode.create_if_not_exists = true;
            break;
        case "a+": open_mode.read = true;
            open_mode.write = true;
            open_mode.append = true;
            open_mode.create_if_not_exists = true;
            break;
        default: output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Invalid open mode '" << mode << "'" <<
                 std::endl;
            return std::nullopt;
    }
    return open_mode;
}

std::optional<uint32_t> FileSystemCore::open_file(const std::string &path, const std::string &mode) {
    if (!mounted_) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Filesystem not mounted. Cannot open file" << std::endl;
        return std::nullopt;
    }

    const std::optional<OpenMode> open_mode = parse_mode(mode);
    if (!open_mode) return std::nullopt;
    OpenMode _mode = *open_mode;

    std::string filename = get_filename_from_path(path);
    uint32_t dir_cluster = get_containing_directory_cluster(path);

    std::optional<DirectoryManager::EntryLocation> entry_location_opt = directory_manager_->get_entry_location(
        dir_cluster, filename);
    FileSystem::DirectoryEntry entry_data;
    bool entry_exists = entry_location_opt.has_value();

    if (entry_exists) {
        entry_data = entry_location_opt->entry_data;
        if (entry_data.type == FileSystem::EntityType::DIRECTORY) {
            output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Path '" << path <<
                    "' is a directory, cannot open as file" << std::endl;
            return std::nullopt;
        }
        if (_mode.truncate) {
            if (entry_data.first_cluster != FileSystem::MARKER_FAT_ENTRY_EOF && entry_data.first_cluster !=
                FileSystem::MARKER_FAT_ENTRY_FREE) {
                std::list<uint32_t> chain = fat_manager_->get_cluster_chain(entry_data.first_cluster);
                fat_manager_->free_chain(entry_data.first_cluster);
                for (uint32_t cluster_idx: chain) {
                    bitmap_manager_->free_cluster(cluster_idx);
                }
            }
            entry_data.first_cluster = FileSystem::MARKER_FAT_ENTRY_FREE;
            entry_data.file_size_bytes = 0;
            if (!directory_manager_->update_entry(dir_cluster, filename, entry_data)) {
                output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) <<
                        "Failed to update directory entry after truncate for '" << path << "'" << std::endl;
                return std::nullopt;
            }
        }
    } else {
        if (_mode.create_if_not_exists) {
            std::strncpy(entry_data.name.data(), filename.c_str(), FileSystem::MAX_FILE_NAME - 1);
            entry_data.name[FileSystem::MAX_FILE_NAME - 1] = '\0';
            entry_data.type = FileSystem::EntityType::FILE;
            entry_data.first_cluster = FileSystem::MARKER_FAT_ENTRY_FREE;
            entry_data.file_size_bytes = 0;
            if (!directory_manager_->add_entry(dir_cluster, entry_data)) {
                output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to create new file entry for '" << path
                        << "'" << std::endl;
                return std::nullopt;
            }
            entry_exists = true;
        } else {
            output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "File '" << path <<
                    "' not found and mode does not allow creation" << std::endl;
            return std::nullopt;
        }
    }
    FileSystem::FileHandle handle;
    handle.handle_id = next_handle_id++;
    handle.path = path;
    handle.dir_entry = entry_data;
    handle.is_open_to_write = _mode.write || _mode.append;

    handle.buffered_cluster_idx = FileSystem::MARKER_FAT_ENTRY_EOF;
    handle.buffer_dirty = false;

    uint64_t position_to_seek = 0;
    if (_mode.append) {
        position_to_seek = entry_data.file_size_bytes;
    }

    opened_files_table_[handle.handle_id] = handle;

    if (!seek(handle.handle_id, position_to_seek, FS_SEEK_SET)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_WARNING) << "Initial seek failed for handle " << handle.handle_id
                << " for path '" << path << "' to position" << position_to_seek << std::endl;
        opened_files_table_.erase(handle.handle_id);
        return std::nullopt;
    }
    return handle.handle_id;
}

bool FileSystemCore::close_file(const uint32_t handle_id) {
    const auto _it = opened_files_table_.find(handle_id);
    if (_it == opened_files_table_.end()) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Invalid file handle " << handle_id << std::endl;
        return false;
    }
    FileSystem::FileHandle handle = _it->second;
    if (!flush_cluster(handle)) {
        output::warn(output::prefix::FAT_MANAGER_WARNING) << "Failed to flush buffer for handle " << handle_id <<
                std::endl;
    }

    if (handle.modified) {
        if (!update_directory_entry_for_file(handle)) {
            output::warn(output::prefix::FILE_SYSTEM_CORE_WARNING) << "Failed to update directory entry for handle " <<
                    handle_id << std::endl;
        }
    }
    opened_files_table_.erase(handle_id);
    return true;
}

bool FileSystemCore::flush_cluster(FileSystem::FileHandle &handle) const {
    if (handle.buffer_dirty && handle.buffered_cluster_idx != FileSystem::MARKER_FAT_ENTRY_EOF && handle.
        buffered_cluster_idx != FileSystem::MARKER_FAT_ENTRY_FREE) {
        if (!vol_manager_.write_cluster(handle.buffered_cluster_idx, handle.buffer.data())) {
            output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to write buffered cluster " << handle.
                    buffered_cluster_idx << " to disk" << std::endl;
            return false;
        }
        handle.buffer_dirty = false;
    }
    return true;
}

bool FileSystemCore::load_cluster_info_buffer(FileSystem::FileHandle &handle, uint32_t cluster_to_load) const {
    if (cluster_to_load == FileSystem::MARKER_FAT_ENTRY_EOF || cluster_to_load == FileSystem::MARKER_FAT_ENTRY_FREE) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Attempt to load invalid cluster index " << cluster_to_load << " into buffer" << std::endl;
        return false;
    }
    if (handle.buffered_cluster_idx == cluster_to_load) return true;
    if (!flush_cluster(handle)) return false;

    if (!vol_manager_.read_cluster(cluster_to_load, handle.buffer.data())) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to read cluster " << cluster_to_load << " into handle buffer" << std::endl;
        return false;
    }
    handle.buffered_cluster_idx = cluster_to_load;
    handle.buffer_dirty = false;
    return true;
}

std::optional<uint32_t> FileSystemCore::allocate_and_link_cluster(FileSystem::FileHandle &handle) {
    if (!handle.is_open_to_write) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Cannot allocate cluster for file not opened in write mode" << std::endl;
        return std::nullopt;
    }

    std::optional<uint32_t> new_cluster_opt = bitmap_manager_->find_and_allocate_free_cluster();
    if (!new_cluster_opt) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "No free clusters available to extend file '" << handle.path << "'" << std::endl;
        return std::nullopt;
    }
    uint32_t new_cluster_idx = *new_cluster_opt;
    if (handle.dir_entry.first_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || handle.dir_entry.first_cluster == FileSystem::MARKER_FAT_ENTRY_EOF) {
        if (!fat_manager_->append_to_chain(FileSystem::MARKER_FAT_ENTRY_EOF, new_cluster_idx)) {
            bitmap_manager_->free_cluster(new_cluster_idx);
            return std::nullopt;
        }
        handle.dir_entry.first_cluster = new_cluster_idx;
    } else {
        uint32_t last_known_cluster_in_chain = handle.current_cluster_in_chain;
        if (last_known_cluster_in_chain == FileSystem::MARKER_FAT_ENTRY_FREE || last_known_cluster_in_chain == FileSystem::MARKER_FAT_ENTRY_EOF) {
            std::list<uint32_t> chain = fat_manager_->get_cluster_chain(handle.dir_entry.first_cluster);
            if (chain.empty()) {
                output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "File has first cluster but chain is empty" << std::endl;
                bitmap_manager_->free_cluster(new_cluster_idx);
                return std::nullopt;
            }
            last_known_cluster_in_chain = chain.back();
        }

        if (!fat_manager_->append_to_chain(last_known_cluster_in_chain, new_cluster_idx) ) {
            bitmap_manager_->free_cluster(new_cluster_idx);
            return std::nullopt;
        }
    }

    handle.modified = true;
    return new_cluster_idx;
}

bool FileSystemCore::update_directory_entry_for_file(const FileSystem::FileHandle &handle) {
    FileSystem::DirectoryEntry updated_de = handle.dir_entry;

    std::string filename = get_filename_from_path(handle.path);
    uint32_t dir_cluster = get_containing_directory_cluster(handle.path);

    if (!directory_manager_->update_entry(dir_cluster, filename, updated_de)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to update directory entry for file '" << handle.path << "'" << std::endl;
        return false;
    }
    return true;
}



