#ifndef VOLUME_MANAGER_H
#define VOLUME_MANAGER_H

#include "file_system_defs.h"
#include <fstream>
#include <vector>
#include <string>

class VolumeManager {
public:
    VolumeManager();
    ~VolumeManager();

    // создание и форматирования нового тома
    bool create_and_format(const std::string& volume_path, uint64_t volume_size_bytes);

    // загрузка существующего тома
    bool load_volume(const std::string& volume_path);

    // читает кластер в указанный буфер
    // размер buffer должен быть >= FileSystem::CLUSTER_SIZE_BYTES
    bool read_cluster(uint32_t cluster_idx, char* buffer);

    // записывает данные из буфера в определённый кластер
    // размер буфера == FileSystem::CLUSTER_SIZE_BYTES
    bool write_cluster(uint32_t cluster_idx, const char* buffer);

    const FileSystem::Header& get_header(); // получить суперблок; константный доступ

    FileSystem::Header& get_header_dangerously(); // получить суперблок; для изменения

    bool is_open(); // проверка открыт ли том

    void close_volume(); // закрыть том

    // получить смещение кластера
    uint64_t get_cluster_offset(uint32_t cluster_idx) const;

private:

    mutable std::fstream volume_stream_;
    FileSystem::Header header_cache_; // кэш заголовка
    std::string current_volume_path_; // текущий путь к файлу-тому
    bool is_volume_loaded_ = false; // загружен ли том

    bool initialize_header(uint64_t volume_size_bytes); // инициализация заголовка, необходима при форматировании
    bool write_header(); // записать заголовок на диск
    bool read_superblock(); // прочитать заголовок с диска
};

#endif //VOLUME_MANAGER_H
