#include "../include/directory_manager.h"

DirectoryManager::DirectoryManager(VolumeManager &vol_manager, FATManager &fat_manager, BitmapManager &bitmap_manager)
    : vol_manager_(vol_manager), fat_manager_(fat_manager), bitmap_manager_(bitmap_manager) {
}

bool DirectoryManager::initialize_root_directory(const FileSystem::Header &header) const {
    if (header.root_dir_size_clusters == FileSystem::MARKER_FAT_ENTRY_EOF || header.root_dir_start_cluster ==
        FileSystem::MARKER_FAT_ENTRY_FREE) {
        output::err(output::prefix::DIRECTORY_MANAGER) << "Invalid root directory start cluster" << std::endl;
    }

    if (const std::vector<FileSystem::DirectoryEntry> empty_entries(FileSystem::DIR_ENTRIES_PER_CLUSTER); !write_directory_cluster(header.root_dir_start_cluster, empty_entries)) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) <<
                "Failed to write initial empty entries to root directory cluster " << header.root_dir_start_cluster <<
                std::endl;
        return false;
    }
    output::succ(output::prefix::DIRECTORY_MANAGER) << "Root directory initialized in cluster " << header.
            root_dir_start_cluster << std::endl;
    return true;
}

std::vector<FileSystem::DirectoryEntry> DirectoryManager::read_all_entries(const uint32_t dir_start_cluster) const {
    std::vector<FileSystem::DirectoryEntry> entries;
    if (dir_start_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || dir_start_cluster ==
        FileSystem::MARKER_FAT_ENTRY_EOF) {
        output::warn(output::prefix::DIRECTORY_MANAGER_WARNING) << "Empty entries for cluster " << dir_start_cluster <<
                std::endl;
    }
    std::vector<char> buffer(vol_manager_.get_cluster_size());
    if (!vol_manager_.read_cluster(dir_start_cluster, buffer.data())) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Failed to read directory cluster " << dir_start_cluster
                << std::endl;
        return entries;
    }
    entries.resize(FileSystem::DIR_ENTRIES_PER_CLUSTER);
    std::memcpy(entries.data(), buffer.data(),
                FileSystem::DIR_ENTRIES_PER_CLUSTER * sizeof(FileSystem::DirectoryEntry));
    return entries;
}

bool DirectoryManager::write_directory_cluster(const uint32_t cluster_idx,
                                               const std::vector<FileSystem::DirectoryEntry> &
                                               entries_for_this_cluster) const {
    if (cluster_idx == FileSystem::MARKER_FAT_ENTRY_FREE || cluster_idx == FileSystem::MARKER_FAT_ENTRY_EOF) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Invalid cluster index " << cluster_idx << std::endl;
        return false;
    }
    if (entries_for_this_cluster.size() != FileSystem::DIR_ENTRIES_PER_CLUSTER) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) <<
                "Incorrect num of entries providing for write to cluster" << std::endl;
        return false;
    }
    std::vector<char> buffer(vol_manager_.get_cluster_size());
    std::memcpy(buffer.data(), entries_for_this_cluster.data(),
                FileSystem::DIR_ENTRIES_PER_CLUSTER * sizeof(FileSystem::DirectoryEntry));
    std::memset(buffer.data() + FileSystem::DIR_ENTRIES_PER_CLUSTER * sizeof(FileSystem::DirectoryEntry), 0,
                buffer.size() + FileSystem::DIR_ENTRIES_PER_CLUSTER * sizeof(FileSystem::DirectoryEntry));

    if (!vol_manager_.write_cluster(cluster_idx, buffer.data())) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Failed to write directory cluster " << cluster_idx <<
                std::endl;
        return false;
    }
    return true;
}

std::vector<FileSystem::DirectoryEntry> DirectoryManager::get_directories_list(uint32_t directory_start_cluster) const {
    std::vector<FileSystem::DirectoryEntry> all_entries;
    if (directory_start_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || directory_start_cluster ==
        FileSystem::MARKER_FAT_ENTRY_EOF) {
        output::warn(output::prefix::DIRECTORY_MANAGER_WARNING) << "List of entries is empty" << std::endl;
        return all_entries;
    }
    const std::list<uint32_t> cluster_chain = fat_manager_.get_cluster_chain(directory_start_cluster);

    for (const uint32_t cluster_idx: cluster_chain) {
        std::vector<FileSystem::DirectoryEntry> entries_for_this_cluster = read_all_entries(cluster_idx);
        for (const auto &entry: entries_for_this_cluster) {
            if (entry.name[0] != FileSystem::ENTRY_NEVER_USED && entry.name[0] != FileSystem::ENTRY_DELETED) {
                all_entries.push_back(entry);
            }
        }
    }
    return all_entries;
}

std::optional<FileSystem::DirectoryEntry> DirectoryManager::find_entry(const uint32_t dir_start_cluster,
                                                                       const std::string &name) {
    if (auto location_opt = get_entry_location(dir_start_cluster, name)) {
        return location_opt->entry_data;
    }
    return std::nullopt;
}

std::optional<DirectoryManager::EntryLocation> DirectoryManager::get_entry_location(
    const uint32_t dir_start_cluster, const std::string &name) const {
    if (name.length() >= FileSystem::MAX_FILE_NAME) {
        output::warn(output::prefix::DIRECTORY_MANAGER_WARNING) << "Name is too long for this filesystem" << std::endl;
        return std::nullopt;
    }
    if (dir_start_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || dir_start_cluster ==
        FileSystem::MARKER_FAT_ENTRY_EOF) {
        output::warn(output::prefix::DIRECTORY_MANAGER_WARNING) << "Cluster is free or eof" << std::endl;
        return std::nullopt;
    }
    const std::list<uint32_t> cluster_chain = fat_manager_.get_cluster_chain(dir_start_cluster);
    char search_name_arr[FileSystem::MAX_FILE_NAME] = {0};
    strncpy(search_name_arr, name.c_str(), FileSystem::MAX_FILE_NAME - 1);

    for (const uint32_t cluster_idx: cluster_chain) {
        std::vector<FileSystem::DirectoryEntry> entries_for_this_cluster = read_all_entries(cluster_idx);
        for (uint32_t i = 0; i < entries_for_this_cluster.size(); ++i) {
            if (const auto &entry = entries_for_this_cluster[i];
                entry.name[0] != FileSystem::ENTRY_NEVER_USED && entry.name[0] != FileSystem::ENTRY_DELETED) {
                if (strncmp(entry.name.data(), search_name_arr, FileSystem::MAX_FILE_NAME) == 0) {
                    return EntryLocation{cluster_idx, i, entry};
                }
            }
        }
    }
    return std::nullopt;
}

bool DirectoryManager::add_entry(const uint32_t dir_start_cluster, const FileSystem::DirectoryEntry &new_entry) {
    if (new_entry.name[0] == '\0') {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Cannot add entry with empty name" << std::endl;
        return false;
    }

    if (dir_start_cluster == FileSystem::MARKER_FAT_ENTRY_FREE || dir_start_cluster ==
        FileSystem::MARKER_FAT_ENTRY_EOF) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Invalid directory start cluster " << dir_start_cluster
                << std::endl;
        return false;
    }

    if (find_entry(dir_start_cluster,
                   std::string(new_entry.name.data(), strnlen(new_entry.name.data(), FileSystem::MAX_FILE_NAME)))) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Entry with name '" << new_entry.name.data() <<
                "' already exists" << std::endl;
        return false;
    }

    const std::list<uint32_t> clusters_chain = fat_manager_.get_cluster_chain(dir_start_cluster);
    uint32_t last_cluster_in_chain = dir_start_cluster;
    if (!clusters_chain.empty()) {
        last_cluster_in_chain = clusters_chain.back();
    }

    for (const uint32_t cluster_idx: clusters_chain) {
        std::vector<FileSystem::DirectoryEntry> entries_in_cluster = read_all_entries(cluster_idx);
        for (uint32_t i = 0; i < entries_in_cluster.size(); ++i) {
            if (entries_in_cluster[i].name[0] != FileSystem::ENTRY_NEVER_USED && entries_in_cluster[i].name[0] !=
                FileSystem::ENTRY_DELETED) {
                entries_in_cluster[i] = new_entry;
                return write_directory_cluster(cluster_idx, entries_in_cluster);
            }
        }
    }

    const std::optional<uint32_t> new_cluster_opt = extend_directory(last_cluster_in_chain);
    if (!new_cluster_opt) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Failed to extend directory file" << std::endl;
        return false;
    }

    uint32_t new_cluster_idx = *new_cluster_opt;
    std::vector<FileSystem::DirectoryEntry> new_cluster_entries(FileSystem::DIR_ENTRIES_PER_CLUSTER);
    new_cluster_entries[0] = new_entry;
    return write_directory_cluster(new_cluster_idx, new_cluster_entries);
}

std::optional<uint32_t> DirectoryManager::extend_directory(const uint32_t dir_last_cluster_idx) const {
    // 1. выделяем новый кластер в битовой карте
    const std::optional<uint32_t> new_cluster_idx_opt = bitmap_manager_.find_and_allocate_free_cluster();
    if (!new_cluster_idx_opt) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "No free clusters for extend directory" << std::endl;
        return std::nullopt;
    }
    uint32_t new_cluster_idx = *new_cluster_idx_opt;

    // 2. связываем его в FAT
    if (!fat_manager_.append_to_chain(dir_last_cluster_idx, new_cluster_idx)) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Failed to link new cluster " << new_cluster_idx <<
                " in FAT to directory extension (previous last: " << dir_last_cluster_idx << ")" << std::endl;
        bitmap_manager_.free_cluster(new_cluster_idx);
        return std::nullopt;
    }
    // 3. очищаем новый кластер для каталога
    if (const std::vector<FileSystem::DirectoryEntry> empty_entries(FileSystem::DIR_ENTRIES_PER_CLUSTER); !
        write_directory_cluster(new_cluster_idx, empty_entries)) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Failed to initialize new directory cluster " <<
                new_cluster_idx << std::endl;
        // если не удалось записать кластер возвращаем всё как было
        fat_manager_.set_entry(dir_last_cluster_idx, FileSystem::MARKER_FAT_ENTRY_EOF);
        fat_manager_.set_entry(new_cluster_idx, FileSystem::MARKER_FAT_ENTRY_FREE);
        bitmap_manager_.free_cluster(new_cluster_idx);
        return std::nullopt;
    }
    return new_cluster_idx;
}

bool DirectoryManager::remove_entry(const uint32_t dir_start_cluster, const std::string &name) {
    auto location_opt = get_entry_location(dir_start_cluster, name);
    if (!location_opt) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Entry: '" << name << "' not found for remove" <<
                std::endl;
        return false;
    }
    EntryLocation location = *location_opt;

    std::vector<FileSystem::DirectoryEntry> entries_in_cluster = read_all_entries(location.dir_cluster_idx);

    entries_in_cluster[location.entry_offset] = FileSystem::DirectoryEntry();

    return write_directory_cluster(location.dir_cluster_idx, entries_in_cluster);
}

bool DirectoryManager::update_entry(uint32_t dir_start_cluster, const std::string &old_name,
                                    const FileSystem::DirectoryEntry &updated_entry) {
    const auto location_opt = get_entry_location(dir_start_cluster, old_name);
    if (!location_opt) {
        output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "Entry '" << old_name << "' not found for update" <<
                std::endl;
        return true;
    }

    std::string new_name_str(updated_entry.name.data(), strnlen(updated_entry.name.data(), FileSystem::MAX_FILE_NAME));
    if (old_name != new_name_str) {
        if (find_entry(dir_start_cluster, new_name_str)) {
            output::err(output::prefix::DIRECTORY_MANAGER_ERROR) << "New name '" << new_name_str << "' already exists"
                    << std::endl;
            return false;
        }
    }

    EntryLocation location = *location_opt;
    std::vector<FileSystem::DirectoryEntry> entries_in_cluster = read_all_entries(location.dir_cluster_idx);

    entries_in_cluster[location.entry_offset] = updated_entry;

    return write_directory_cluster(location.dir_cluster_idx, entries_in_cluster);
}
