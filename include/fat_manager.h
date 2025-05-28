#ifndef FAT_MANAGER_H
#define FAT_MANAGER_H

#include <list>
#include <cstring>

#include "volume_manager.h"


class FATManager {
public:
    explicit FATManager(VolumeManager& vol_manager);
    // инициализирует FAT
    bool initialize_and_flush(const FileSystem::Header& header);

    // загружает FAT с диска
    bool load(const FileSystem::Header& header);

    // получение значения записи FAT
    [[nodiscard]] std::optional<uint32_t> get_entry(uint32_t cluster_idx) const;

    // устанавливает значение записи FAT и записывает на диск
    bool set_entry(uint32_t cluster_idx, uint32_t value);

    // для указанного кластера возвращает всю цепочку кластеров
    [[nodiscard]] std::list<uint32_t> get_cluster_chain(uint32_t start_cluster) const;

    // освобождает цепочку кластеров начиная со start_cluster
    bool free_chain(uint32_t start_cluster);

    // добавляет кластер в цепочку кластеров
    bool append_to_chain(uint32_t last_cluster_in_chain, uint32_t new_cluster_idx);
private:
    VolumeManager& vol_manager_; // ссылка на менеджер томов
    std::vector<uint32_t> fat_table_; // копия fat в памяти
    uint32_t total_clusters_managed_; // общее количество управляемых кластеров
    uint32_t fat_disk_start_cluster_; // начальный кластер fat на диске
    uint32_t fat_dist_clusters_count_; // количество кластеров отведённых под fat

    // чтение fat с диска
    bool read_fat_from_disk();
    // запись fat на диск
    [[nodiscard]] bool write_fat_to_disk() const;
};



#endif //FAT_MANAGER_H
