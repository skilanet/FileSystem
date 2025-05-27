#ifndef VOLUME_MANAGER_H
#define VOLUME_MANAGER_H

#include "file_system_config.h"
#include <fstream>
#include <string>

class VolumeManager {
public:
    VolumeManager();
    ~VolumeManager();

    // создание и форматирования нового тома
    bool create_and_format(const std::string& volume_path, uint64_t volume_size_bytes, FileSystem::Header& out_header);

    // загрузка существующего тома
    bool load_volume(const std::string& volume_path);

    // читает кластер в указанный буфер
    // размер buffer должен быть >= FileSystem::CLUSTER_SIZE_BYTES
    bool read_cluster(uint32_t cluster_idx, char* buffer) const;

    // записывает данные из буфера в определённый кластер
    // размер буфера == FileSystem::CLUSTER_SIZE_BYTES
    bool write_cluster(uint32_t cluster_idx, const char* buffer) const;

    const FileSystem::Header& get_header() const; // получить суперблок; константный доступ

    bool is_open() const; // проверка открыт ли том

    void close_volume(); // закрыть том

    // получить смещение кластера
    std::optional<uint64_t> get_cluster_offset(uint32_t cluster_idx) const;
    uint32_t get_cluster_size() const;

private:

    mutable std::fstream volume_stream_;
    FileSystem::Header header_cache_{}; // кэш заголовка
    std::string current_volume_path_; // текущий путь к файлу-тому
    bool is_volume_loaded_ = false; // загружен ли том

    static bool initialize_header(uint64_t volume_size_bytes, FileSystem::Header& header_to_fill); // инициализация заголовка, необходима при форматировании
    bool write_header_to_disk(const FileSystem::Header& header_to_write) const; // записать заголовок на диск
    bool read_header_from_disk(FileSystem::Header& header_to_fill) const; // прочитать заголовок с диска
};

#endif //VOLUME_MANAGER_H
