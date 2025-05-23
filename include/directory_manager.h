//
// Created by Sergei Filoniuk on 23/05/25.
//

#ifndef DIRECTORY_MANAGER_H
#define DIRECTORY_MANAGER_H
#include "bitmap_manager.h"
#include "fat_manager.h"
#include "volume_manager.h"

class DirectoryManager {
public:
    DirectoryManager(VolumeManager& vol_manager, FATManager& fat_manager, BitmapManager& bitmap_manager);

    //
    bool initialize_root_directory();

    std::vector<FileSystem::DirectoryEntry> get_directories_list(uint32_t directory_start_cluster);

    std::optional<FileSystem::DirectoryEntry> find_entry(uint32_t dir_start_cluster, std::string& name);

    st
}

#endif //DIRECTORY_MANAGER_H
