//
// Created by Sergei Filoniuk on 23/05/25.
//

#ifndef DIRECTORY_MANAGER_H
#define DIRECTORY_MANAGER_H
#include "bitmap_manager.h"
#include "fat_manager.h"
#include "volume_manager.h"
#include "output.h"
#include <iostream>
#include <cstring>

class DirectoryManager {
public:
    DirectoryManager(VolumeManager &vol_manager, FATManager &fat_manager, BitmapManager &bitmap_manager);

    // инициализирует корневой каталог
    [[nodiscard]] bool initialize_root_directory(const FileSystem::Header& header) const;

    // выводит список всех записей в каталоге
    [[nodiscard]] std::vector<FileSystem::DirectoryEntry> get_directories_list(uint32_t directory_start_cluster) const;

    // находит и возвращает запись в каталоге
    std::optional<FileSystem::DirectoryEntry> find_entry(uint32_t dir_start_cluster, const std::string &name);

    // структура местоположения и информации о записи в каталоге
    struct EntryLocation {
        uint32_t dir_cluster_idx{}; // индекс кластера
        uint32_t entry_offset{}; // смещение
        FileSystem::DirectoryEntry entry_data; // данные о записи
    };
    // находит запись в каталоге
    [[nodiscard]] std::optional<EntryLocation> get_entry_location(uint32_t dir_start_cluster, const std::string &name) const;
    // добавляет запись в каталог
    bool add_entry(uint32_t dir_start_cluster, const FileSystem::DirectoryEntry &new_entry);

    // удаляет запись из каталога
    bool remove_entry(uint32_t dir_start_cluster, const std::string &name);

    // обновляет запись в каталоге
    bool update_entry(uint32_t dir_start_cluster, const std::string &old_name,
                      const FileSystem::DirectoryEntry &updated_entry);

    // функция для перезаписи всех записей каталога
    [[nodiscard]] bool write_directory_cluster(uint32_t cluster_idx, const std::vector<FileSystem::DirectoryEntry>& entries_for_this_cluster) const;

private:
    VolumeManager &vol_manager_; // ссылка на менеджер тома
    FATManager& fat_manager_; // ссылка на менеджер FAT
    BitmapManager& bitmap_manager_; // ссылка на менеджер битовой карты

    // чтение всех записей каталога из его цепочки кластеров
    [[nodiscard]] std::vector<FileSystem::DirectoryEntry> read_all_entries(uint32_t dir_start_cluster) const;

    // функция для расширения каталога на один кластер
    [[nodiscard]] std::optional<uint32_t> extend_directory(uint32_t dir_last_cluster_idx) const;
};

#endif //DIRECTORY_MANAGER_H
