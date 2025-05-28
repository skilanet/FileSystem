#ifndef BITMAP_MANAGER_H
#define BITMAP_MANAGER_H

#include "file_system_config.h"
#include <vector>
#include <fstream>

#include "volume_manager.h"

class BitmapManager {
public:
    explicit BitmapManager(VolumeManager& volume_manager);
    // инициализация битовой карты при форматировании
    bool initialize_and_flush(const FileSystem::Header& header);
    // загрузка битовой карты с диска в память
    bool load(const FileSystem::Header& header);
    // находит свободный кластер и помечает его как занятый
    std::optional<uint32_t> find_and_allocate_free_cluster();
    // помечает кластер как свободный
    bool free_cluster(uint32_t cluster_idx);
    // проверят свободен ли кластер
    [[nodiscard]] bool is_cluster_free(uint32_t cluster_idx) const;
private:
    VolumeManager& volume_mgr_; // ссылка на менеджер тома
    std::vector<uint32_t> bitmap_data_; // копия битовой карты в памяти

    uint32_t total_clusters_managed_; // количество кластеров фс == FileSystem::Header->total_clusters
    uint32_t bitmap_disk_start_cluster_; // начальный кластер битовой карты
    uint32_t bitmap_disk_cluster_count_; // количество кластеров, занимаемых битовой картой

    // установить бит
    void set_bit(uint32_t cluster_idx);
    // снять бит
    void clear_bit(uint32_t cluster_idx);
    // получить бит
    [[nodiscard]] std::optional<bool> get_bit(uint32_t cluster_idx) const;

    // чтение битовой карты с диска
    bool read_bitmap_from_disk();
    // записать битовую карту на диск
    [[nodiscard]] bool write_bitmap_to_disk() const;
};

#endif //BITMAP_MANAGER_H
