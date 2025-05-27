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
        std::vector<uint32_t> handle_ids;
        handle_ids.reserve(opened_files_table_.size());
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
        if (!fat_manager_->set_entry(header_.root_dir_start_cluster, FileSystem::MARKER_FAT_ENTRY_EOF)) {
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
    if (mode == "r") {
        open_mode.read = true;
    } else if (mode == "w") {
        open_mode.write = true;
        open_mode.truncate = true;
        open_mode.create_if_not_exists = true;
    } else if (mode == "a") {
        open_mode.write = true;
        open_mode.append = true;
        open_mode.create_if_not_exists = true;
    } else if (mode == "r+") {
        open_mode.read = true;
        open_mode.write = true;
    } else if (mode == "w+") {
        open_mode.read = true;
        open_mode.write = true;
        open_mode.truncate = true;
        open_mode.create_if_not_exists = true;
    } else if (mode == "a+") {
        open_mode.read = true;
        open_mode.write = true;
        open_mode.append = true;
        open_mode.create_if_not_exists = true;
    } else {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Invalid open mode '" << mode << "'" << std::endl;
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

    if (entry_location_opt.has_value()) {
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
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Attempt to load invalid cluster index " <<
                cluster_to_load << " into buffer" << std::endl;
        return false;
    }
    if (handle.buffered_cluster_idx == cluster_to_load) return true;
    if (!flush_cluster(handle)) return false;

    if (!vol_manager_.read_cluster(cluster_to_load, handle.buffer.data())) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to read cluster " << cluster_to_load <<
                " into handle buffer" << std::endl;
        return false;
    }
    handle.buffered_cluster_idx = cluster_to_load;
    handle.buffer_dirty = false;
    return true;
}

std::optional<uint32_t> FileSystemCore::allocate_and_link_cluster(FileSystem::FileHandle &handle) const {
    if (!handle.is_open_to_write) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) <<
                "Cannot allocate cluster for file not opened in write mode" << std::endl;
        return std::nullopt;
    }

    std::optional<uint32_t> new_cluster_opt = bitmap_manager_->find_and_allocate_free_cluster();
    if (!new_cluster_opt) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "No free clusters available to extend file '" << handle.
                path << "'" << std::endl;
        return std::nullopt;
    }
    uint32_t new_cluster_idx = *new_cluster_opt;
    if (handle.dir_entry.first_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || handle.dir_entry.first_cluster ==
        FileSystem::MARKER_FAT_ENTRY_EOF) {
        if (!fat_manager_->append_to_chain(FileSystem::MARKER_FAT_ENTRY_EOF, new_cluster_idx)) {
            bitmap_manager_->free_cluster(new_cluster_idx);
            return std::nullopt;
        }
        handle.dir_entry.first_cluster = new_cluster_idx;
    } else {
        uint32_t last_known_cluster_in_chain = handle.current_cluster_in_chain;
        if (last_known_cluster_in_chain == FileSystem::MARKER_FAT_ENTRY_FREE || last_known_cluster_in_chain ==
            FileSystem::MARKER_FAT_ENTRY_EOF) {
            std::list<uint32_t> chain = fat_manager_->get_cluster_chain(handle.dir_entry.first_cluster);
            if (chain.empty()) {
                output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "File has first cluster but chain is empty" <<
                        std::endl;
                bitmap_manager_->free_cluster(new_cluster_idx);
                return std::nullopt;
            }
            last_known_cluster_in_chain = chain.back();
        }

        if (!fat_manager_->append_to_chain(last_known_cluster_in_chain, new_cluster_idx)) {
            bitmap_manager_->free_cluster(new_cluster_idx);
            return std::nullopt;
        }
    }

    handle.modified = true;
    return new_cluster_idx;
}

bool FileSystemCore::update_directory_entry_for_file(const FileSystem::FileHandle &handle) const {
    FileSystem::DirectoryEntry updated_de = handle.dir_entry;

    std::string filename = get_filename_from_path(handle.path);
    uint32_t dir_cluster = get_containing_directory_cluster(handle.path);

    if (!directory_manager_->update_entry(dir_cluster, filename, updated_de)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to update directory entry for file '" << handle.
                path << "'" << std::endl;
        return false;
    }
    return true;
}

int64_t FileSystemCore::read_file(uint32_t handle_id, char *buffer, uint64_t bytes_to_read) {
    auto it = opened_files_table_.find(handle_id);
    if (it == opened_files_table_.end()) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Invalid file handle '" << handle_id << "'" << std::endl;
        return -1;
    }
    FileSystem::FileHandle &handle = it->second;

    if (bytes_to_read == 0) return 0;
    if (handle.current_pos_bytes >= handle.dir_entry.file_size_bytes) {
        return 0;
    }

    uint64_t total_bytes_read = 0;
    uint64_t remaining_file_size = handle.dir_entry.file_size_bytes - handle.current_pos_bytes;
    uint64_t effective_bytes_to_read = std::min(bytes_to_read, remaining_file_size);

    while (total_bytes_read < effective_bytes_to_read) {
        // проверка правильности кластера в буфер
        if (handle.buffered_cluster_idx != handle.current_cluster_in_chain) {
            if (handle.current_cluster_in_chain == FileSystem::MARKER_FAT_ENTRY_FREE || handle.current_cluster_in_chain
                == FileSystem::MARKER_FAT_ENTRY_EOF) {
                output::warn(output::prefix::FILE_SYSTEM_CORE_WARNING) << "Unexpected end of cluster chain for file '"
                        << handle.path << "'" << std::endl;
                break;
            }
            if (!load_cluster_info_buffer(handle, handle.current_cluster_in_chain)) {
                output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to load cluster " << handle.
                        current_cluster_in_chain << " for reading" << std::endl;
                return -1;
            }
        }

        // расчёт количества байт, которые можно считать из текущего буфера
        uint32_t bytes_in_current_cluster_buffer = FileSystem::CLUSTER_SIZE_BYTES - handle.offset_in_buffered_cluster;
        uint64_t bytes_to_read_this_iteration = std::min(static_cast<uint64_t>(bytes_in_current_cluster_buffer),
                                                         effective_bytes_to_read - total_bytes_read);

        std::memcpy(buffer + total_bytes_read, handle.buffer.data() + handle.offset_in_buffered_cluster,
                    bytes_to_read_this_iteration);

        handle.current_pos_bytes += bytes_to_read_this_iteration;
        handle.offset_in_buffered_cluster += static_cast<uint32_t>(bytes_to_read_this_iteration);
        total_bytes_read += bytes_to_read_this_iteration;

        if (handle.offset_in_buffered_cluster >= FileSystem::CLUSTER_SIZE_BYTES) {
            if (handle.current_cluster_in_chain == FileSystem::MARKER_FAT_ENTRY_FREE || handle.current_cluster_in_chain
                == FileSystem::MARKER_FAT_ENTRY_EOF)
                break;
            auto next_cluster_opt = fat_manager_->get_entry(handle.current_cluster_in_chain);
            if (!next_cluster_opt || *next_cluster_opt == FileSystem::MARKER_FAT_ENTRY_FREE || *next_cluster_opt ==
                FileSystem::MARKER_FAT_ENTRY_EOF) {
                handle.current_cluster_in_chain = FileSystem::MARKER_FAT_ENTRY_EOF;
                if (total_bytes_read < effective_bytes_to_read) {
                    output::warn(output::prefix::FILE_SYSTEM_CORE_WARNING) <<
                            "File size mismatch. EOF in FAT chain reached early for '" << handle.path << "'" <<
                            std::endl;
                }
                break;
            }
            handle.current_cluster_in_chain = *next_cluster_opt;
            handle.offset_in_buffered_cluster = 0;
        }
    }
    return static_cast<int64_t>(total_bytes_read);
}

int64_t FileSystemCore::write_file(uint32_t handle_id, const char *user_buffer, uint64_t bytes_to_write) {
    auto it = opened_files_table_.find(handle_id);
    if (it == opened_files_table_.end()) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Invalid file handle " << handle_id << " for write." <<
                std::endl;
        return -1;
    }
    FileSystem::FileHandle &handle = it->second;

    if (!handle.is_open_to_write) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Error: File with handle " << handle_id <<
                " not opened in write mode." <<
                std::endl;
        return -1;
    }
    if (bytes_to_write == 0) return 0;

    uint64_t total_bytes_written = 0;

    while (total_bytes_written < bytes_to_write) {
        // если текущий кластер невалиден (начало файла или конец цепочки), выделяем новый
        if (handle.current_cluster_in_chain == FileSystem::MARKER_FAT_ENTRY_FREE || handle.current_cluster_in_chain ==
            FileSystem::MARKER_FAT_ENTRY_EOF) {
            std::optional<uint32_t> new_cluster_idx_opt = allocate_and_link_cluster(handle);
            if (!new_cluster_idx_opt) {
                output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to allocate new cluster for file '" <<
                        handle.path <<
                        "' during write." << std::endl;
                break;
            }
            handle.current_cluster_in_chain = *new_cluster_idx_opt;
            handle.offset_in_buffered_cluster = 0;
            if (handle.buffered_cluster_idx != handle.current_cluster_in_chain) {
                if (!flush_cluster(handle)) { break; }
                handle.buffered_cluster_idx = FileSystem::MARKER_FAT_ENTRY_EOF;
            }
        }

        // убедиться, что правильный (текущий) кластер в буфере
        if (handle.buffered_cluster_idx != handle.current_cluster_in_chain) {
            if (!load_cluster_info_buffer(handle, handle.current_cluster_in_chain)) {
                output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to load cluster " << handle.
                        current_cluster_in_chain <<
                        " for writing." << std::endl;
                break;
            }
        }

        // вычисляем сколько байт можно записать в текущий буфер
        const uint32_t bytes_to_fill_in_cluster = FileSystem::CLUSTER_SIZE_BYTES - handle.offset_in_buffered_cluster;
        const uint64_t bytes_to_write_this_iteration = std::min(static_cast<uint64_t>(bytes_to_fill_in_cluster),
                                                                bytes_to_write - total_bytes_written);

        std::memcpy(handle.buffer.data() + handle.offset_in_buffered_cluster,
                    user_buffer + total_bytes_written,
                    bytes_to_write_this_iteration);
        handle.buffer_dirty = true;

        handle.current_pos_bytes += bytes_to_write_this_iteration;
        handle.offset_in_buffered_cluster += static_cast<uint32_t>(bytes_to_write_this_iteration);
        total_bytes_written += bytes_to_write_this_iteration;

        if (handle.current_pos_bytes > handle.dir_entry.file_size_bytes) {
            handle.dir_entry.file_size_bytes = handle.current_pos_bytes;
            handle.modified = true;
        }

        if (handle.offset_in_buffered_cluster >= FileSystem::CLUSTER_SIZE_BYTES) {
            if (!flush_cluster(handle)) {
                output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) <<
                        "Failed to flush buffer before switching to next cluster." << std::endl;
                break;
            }
            auto next_cluster_opt = fat_manager_->get_entry(handle.current_cluster_in_chain);
            if (!next_cluster_opt || *next_cluster_opt == FileSystem::MARKER_FAT_ENTRY_EOF || *next_cluster_opt ==
                FileSystem::MARKER_FAT_ENTRY_FREE) {
                handle.current_cluster_in_chain = FileSystem::MARKER_FAT_ENTRY_EOF;
            } else {
                handle.current_cluster_in_chain = *next_cluster_opt;
            }
            handle.offset_in_buffered_cluster = 0;
            handle.buffered_cluster_idx = FileSystem::MARKER_FAT_ENTRY_EOF;
        }
    }
    return static_cast<int64_t>(total_bytes_written);
}

bool FileSystemCore::seek(uint32_t handle_id, uint64_t offset, int whence) {
    const auto it = opened_files_table_.find(handle_id);
    if (it == opened_files_table_.end()) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Invalid file handle " << handle_id << " for seek." <<
                std::endl;
        return false;
    }
    FileSystem::FileHandle &handle = it->second;

    uint64_t new_pos_bytes;
    uint64_t file_size = handle.dir_entry.file_size_bytes;

    switch (whence) {
        case FS_SEEK_SET:
            new_pos_bytes = offset;
            break;
        case FS_SEEK_CUR:
            if (static_cast<int64_t>(handle.current_pos_bytes) + offset < 0) {
                output::err("") << "Seek Error: Seek before beginning of file with SEEK_CUR." << std::endl;
                return false;
            }
            new_pos_bytes = handle.current_pos_bytes + offset;
            break;
        case FS_SEEK_END:
            if (static_cast<int64_t>(file_size) + offset < 0) {
                output::err("") << "Seek Error: Seek before beginning of file with SEEK_END." << std::endl;
                return false;
            }
            new_pos_bytes = file_size + offset;
            break;
        default:
            output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Invalid 'whence' parameter for seek." << std::endl;
            return false;
    }

    if (!handle.is_open_to_write && new_pos_bytes > file_size) {
        output::warn(output::prefix::FILE_SYSTEM_CORE_WARNING) << "Seek beyond EOF in read-only mode. Clamping to EOF."
                << std::endl;
        new_pos_bytes = file_size;
    }

    if (new_pos_bytes == handle.current_pos_bytes) {
        uint32_t target_cluster_for_new_pos = handle.dir_entry.first_cluster;
        uint32_t cluster_offset_for_new_pos = 0;

        if (new_pos_bytes == 0 && handle.dir_entry.first_cluster == FileSystem::MARKER_FAT_ENTRY_FREE) {
            handle.current_cluster_in_chain = FileSystem::MARKER_FAT_ENTRY_FREE;
            handle.offset_in_buffered_cluster = 0;
            if (handle.buffered_cluster_idx != FileSystem::MARKER_FAT_ENTRY_EOF) {
                if (!flush_cluster(handle)) {
                }
                handle.buffered_cluster_idx = FileSystem::MARKER_FAT_ENTRY_EOF;
            }
            return true;
        }


        for (uint64_t c = 0; c < new_pos_bytes / FileSystem::CLUSTER_SIZE_BYTES; ++c) {
            if (target_cluster_for_new_pos == FileSystem::MARKER_FAT_ENTRY_EOF || target_cluster_for_new_pos ==
                FileSystem::MARKER_FAT_ENTRY_FREE) {
                return false;
            }
            auto next_opt = fat_manager_->get_entry(target_cluster_for_new_pos);
            if (!next_opt) return false;
            target_cluster_for_new_pos = *next_opt;
        }
        cluster_offset_for_new_pos = new_pos_bytes % FileSystem::CLUSTER_SIZE_BYTES;

        if (handle.current_cluster_in_chain == target_cluster_for_new_pos &&
            handle.offset_in_buffered_cluster == cluster_offset_for_new_pos &&
            handle.buffered_cluster_idx == target_cluster_for_new_pos) {
            return true;
        }
    }
    if (!flush_cluster(handle)) {
        return false;
    }
    handle.buffered_cluster_idx = FileSystem::MARKER_FAT_ENTRY_EOF;

    handle.current_pos_bytes = new_pos_bytes;

    if (handle.dir_entry.first_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || handle.dir_entry.first_cluster ==
        FileSystem::MARKER_FAT_ENTRY_EOF) {
        handle.current_cluster_in_chain = handle.dir_entry.first_cluster;
        handle.offset_in_buffered_cluster = 0;
        return true;
    }

    uint32_t target_cluster = handle.dir_entry.first_cluster;
    uint64_t bytes_to_skip = new_pos_bytes;

    while (bytes_to_skip >= FileSystem::CLUSTER_SIZE_BYTES) {
        if (target_cluster == FileSystem::MARKER_FAT_ENTRY_EOF || target_cluster == FileSystem::MARKER_FAT_ENTRY_FREE) {
            handle.current_cluster_in_chain = target_cluster;
            handle.offset_in_buffered_cluster = 0;

            if (bytes_to_skip > 0 && target_cluster == FileSystem::MARKER_FAT_ENTRY_EOF) {
            }
            return true;
        }
        auto next_cluster_opt = fat_manager_->get_entry(target_cluster);
        if (!next_cluster_opt) {
            output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "FAT entry missing during seek for cluster " <<
                    target_cluster << std::endl;
            return false;
        }
        target_cluster = *next_cluster_opt;
        bytes_to_skip -= FileSystem::CLUSTER_SIZE_BYTES;
    }

    handle.current_cluster_in_chain = target_cluster;
    handle.offset_in_buffered_cluster = static_cast<uint32_t>(bytes_to_skip);

    return true;
}


bool FileSystemCore::remove_file(const std::string &path) const {
    if (!mounted_) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Filesystem not mounted. Cannot remove file." <<
                std::endl;
        return false;
    }

    const std::string filename = get_filename_from_path(path);
    const uint32_t dir_cluster = get_containing_directory_cluster(path);

    const auto entry_loc_opt = directory_manager_->get_entry_location(dir_cluster, filename);
    if (!entry_loc_opt) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "File '" << path << "' not found for removal." <<
                std::endl;
        return false;
    }
    FileSystem::DirectoryEntry entry_to_remove = entry_loc_opt->entry_data;

    if (entry_to_remove.type == FileSystem::EntityType::DIRECTORY) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "'" << path << "' is a directory. Use removeDirectory."
                << std::endl;
        return false;
    }

    // освободить кластеры файла в FAT и Bitmap
    if (entry_to_remove.first_cluster != FileSystem::MARKER_FAT_ENTRY_FREE && entry_to_remove.first_cluster !=
        FileSystem::MARKER_FAT_ENTRY_EOF) {
        const std::list<uint32_t> cluster_chain = fat_manager_->get_cluster_chain(entry_to_remove.first_cluster);
        if (!fat_manager_->free_chain(entry_to_remove.first_cluster)) {
            output::warn(output::prefix::FILE_SYSTEM_CORE_WARNING) << "Failed to fully free FAT chain for '" << path <<
                    "'." << std::endl;
        }
        for (uint32_t cluster_idx: cluster_chain) {
            if (!bitmap_manager_->free_cluster(cluster_idx)) {
                output::warn(output::prefix::FILE_SYSTEM_CORE_WARNING) << "Failed to free cluster " << cluster_idx <<
                        " in bitmap for '" << path << "'." << std::endl;
            }
        }
    }

    // удалить запись из каталога
    if (!directory_manager_->remove_entry(dir_cluster, filename)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to remove directory entry for '" << path << "'."
                << std::endl;
        return false;
    }

    return true;
}

bool FileSystemCore::rename_file(const std::string &old_path, const std::string &new_path) {
    if (!mounted_) return false;

    const std::string old_filename = get_filename_from_path(old_path);
    const std::string new_filename = get_filename_from_path(new_path);
    const uint32_t dir_cluster = get_containing_directory_cluster(old_path);

    if (new_filename.empty() || new_filename.length() >= FileSystem::MAX_FILE_NAME) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "New filename '" << new_filename << "' is invalid." <<
                std::endl;
        return false;
    }

    // проверить, не существует ли уже файл/каталог с новым именем
    if (directory_manager_->find_entry(dir_cluster, new_filename)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Target filename '" << new_filename <<
                "' already exists." << std::endl;
        return false;
    }

    auto entry_loc_opt = directory_manager_->get_entry_location(dir_cluster, old_filename);
    if (!entry_loc_opt) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Source file/directory '" << old_filename <<
                "' not found." << std::endl;
        return false;
    }

    FileSystem::DirectoryEntry entry_to_rename = entry_loc_opt->entry_data;
    // обновляем имя в копии записи
    strncpy(entry_to_rename.name.data(), new_filename.c_str(), FileSystem::MAX_FILE_NAME - 1);
    entry_to_rename.name[FileSystem::MAX_FILE_NAME - 1] = '\0';


    if (!directory_manager_->update_entry(dir_cluster, old_filename, entry_to_rename)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to update directory entry during rename from '"
                << old_filename << "' to '" << new_filename << "'." << std::endl;
        return false;
    }

    // если переименовывается открытый файл, нужно обновить его путь в open_files_table_
    for (auto &[fst, snd]: opened_files_table_) {
        if (snd.path == old_path) {
            snd.path = new_path;
            // также обновить dir_entry_snapshot.filename, если это важно для логики close()
            strncpy(snd.dir_entry.name.data(), new_filename.c_str(), FileSystem::MAX_FILE_NAME - 1);
            snd.dir_entry.name[FileSystem::MAX_FILE_NAME - 1] = '\0';
        }
    }

    return true;
}


bool FileSystemCore::create_directory(const std::string &path) const {
    if (!mounted_) return false;

    const std::string dirname = get_filename_from_path(path);
    const uint32_t parent_dir_cluster = get_containing_directory_cluster(path);

    if (dirname.empty() || dirname.length() >= FileSystem::MAX_FILE_NAME) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Directory name '" << dirname << "' is invalid." <<
                std::endl;
        return false;
    }
    if (directory_manager_->find_entry(parent_dir_cluster, dirname)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Directory or file '" << dirname << "' already exists."
                << std::endl;
        return false;
    }

    // выделяем кластер для данных нового каталога
    const std::optional<uint32_t> new_dir_data_cluster_opt = bitmap_manager_->find_and_allocate_free_cluster();
    if (!new_dir_data_cluster_opt) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "No free cluster for new directory data." << std::endl;
        return false;
    }
    const uint32_t new_dir_data_cluster = *new_dir_data_cluster_opt;

    // помечаем этот кластер как конец цепочки в FAT (каталог пока пуст и занимает 1 кластер)
    if (!fat_manager_->set_entry(new_dir_data_cluster, FileSystem::MARKER_FAT_ENTRY_EOF)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to set FAT entry for new directory cluster " <<
                new_dir_data_cluster << std::endl;
        bitmap_manager_->free_cluster(new_dir_data_cluster);
        return false;
    }

    // инициализируем сам кластер данных каталога (заполняем пустыми DirectoryEntry)
    std::vector<FileSystem::DirectoryEntry> empty_entries(FileSystem::DIR_ENTRIES_PER_CLUSTER);
    if (!directory_manager_->write_directory_cluster(new_dir_data_cluster, empty_entries)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to initialize new directory data cluster " <<
                new_dir_data_cluster << std::endl;
        fat_manager_->set_entry(new_dir_data_cluster, FileSystem::MARKER_FAT_ENTRY_FREE);
        bitmap_manager_->free_cluster(new_dir_data_cluster);
        return false;
    }


    // создаем запись для нового каталога в родительском каталоге
    FileSystem::DirectoryEntry new_dir_entry;
    strncpy(new_dir_entry.name.data(), dirname.c_str(), FileSystem::MAX_FILE_NAME - 1);
    new_dir_entry.name[FileSystem::MAX_FILE_NAME - 1] = '\0';
    new_dir_entry.type = FileSystem::EntityType::DIRECTORY;
    new_dir_entry.first_cluster = new_dir_data_cluster;
    new_dir_entry.file_size_bytes = 0;

    if (!directory_manager_->add_entry(parent_dir_cluster, new_dir_entry)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to add directory entry for '" << dirname << "'."
                << std::endl;
        fat_manager_->set_entry(new_dir_data_cluster, FileSystem::MARKER_FAT_ENTRY_FREE);
        bitmap_manager_->free_cluster(new_dir_data_cluster);
        return false;
    }
    return true;
}

bool FileSystemCore::remove_directory(const std::string &path) const {
    if (!mounted_) return false;

    const std::string dirname = get_filename_from_path(path);
    const uint32_t parent_dir_cluster = get_containing_directory_cluster(path);

    const auto entry_loc_opt = directory_manager_->get_entry_location(parent_dir_cluster, dirname);
    if (!entry_loc_opt) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Directory '" << path << "' not found for removal." <<
                std::endl;
        return false;
    }
    const FileSystem::DirectoryEntry dir_to_remove = entry_loc_opt->entry_data;

    if (dir_to_remove.type != FileSystem::EntityType::DIRECTORY) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "'" << path << "' is not a directory. Use remove." <<
                std::endl;
        return false;
    }

    // проверить, пуст ли каталог
    std::vector<FileSystem::DirectoryEntry> entries_in_dir = directory_manager_->get_directories_list(
        dir_to_remove.first_cluster);
    if (!entries_in_dir.empty()) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Directory '" << path << "' is not empty." << std::endl;
        return false;
    }

    // освободить кластеры данных каталога в FAT и Bitmap
    if (dir_to_remove.first_cluster != FileSystem::MARKER_FAT_ENTRY_FREE && dir_to_remove.first_cluster !=
        FileSystem::MARKER_FAT_ENTRY_EOF) {
        const std::list<uint32_t> cluster_chain = fat_manager_->get_cluster_chain(dir_to_remove.first_cluster);
        fat_manager_->free_chain(dir_to_remove.first_cluster);
        for (const uint32_t cluster_idx: cluster_chain) {
            bitmap_manager_->free_cluster(cluster_idx);
        }
    }

    // удалить запись каталога из родительского каталога
    if (!directory_manager_->remove_entry(parent_dir_cluster, dirname)) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Failed to remove directory entry for '" << path << "'."
                << std::endl;
        return false;
    }
    return true;
}

std::vector<FileSystem::DirectoryEntry> FileSystemCore::list_directory(const std::string &path) const {
    std::vector<FileSystem::DirectoryEntry> result;
    if (!mounted_) {
        output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Filesystem not mounted." << std::endl;
        return result;
    }

    if (path == "/") {
        return directory_manager_->get_directories_list(header_.root_dir_start_cluster);
    }
    const std::string dirname = get_filename_from_path(path);
    if (const auto entry_opt = directory_manager_->find_entry(header_.root_dir_start_cluster, dirname);
        entry_opt && entry_opt->type == FileSystem::EntityType::DIRECTORY) {
        return directory_manager_->get_directories_list(entry_opt->first_cluster);
    }
    output::err(output::prefix::FILE_SYSTEM_CORE_ERROR) << "Directory '" << path << "' not found or is not a directory."
            << std::endl;
    return result;
}

std::string FileSystemCore::get_filename_from_path(const std::string &path) {
    if (path.empty()) return "";
    if (path == "/") return "/";

    if (const size_t last_slash = path.find_last_of('/'); last_slash != std::string::npos) {
        return path.substr(last_slash + 1);
    }
    return path;
}

uint32_t FileSystemCore::get_containing_directory_cluster(const std::string &path_ignored_for_flat_fs) const {
    (void) path_ignored_for_flat_fs;
    return header_.root_dir_start_cluster;
}
