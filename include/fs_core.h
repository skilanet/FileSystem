#ifndef FS_CORE_H
#define FS_CORE_H
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <optional>

#include "bitmap_manager.h"
#include "directory_manager.h"
#include "fat_manager.h"
#include "file_system_config.h"
#include "volume_manager.h"

#define FS_SEEK_SET 0
#define FS_SEEK_CUR 1
#define FS_SEEK_END 2

class FileSystemCore {
public:
    // Ядро файловой системы
    FileSystemCore();
    ~FileSystemCore();

    bool mount(const std::string &volume_path); // монтирование существующего тома
    bool format(const std::string &volume_path, uint64_t volume_size_mb); // форматирование тома
    void unmount(); // размонтирование тома
    bool isMounted() const;

    // --- Операции с файлами --- //
    std::optional<uint32_t> open_file(const std::string &path, const std::string &mode);
    bool close_file(uint32_t handle_id);
    int64_t read_file(uint32_t handle_id, char *buffer, uint64_t bytes_to_read);
    int64_t write_file(uint32_t handle_id, const char *buffer, uint64_t bytes_to_write);
    bool seek(uint32_t handle_id, uint64_t offset, int whence);
    bool remove_file(const std::string &path) const;
    bool rename_file(const std::string &old_path, const std::string &new_path);

    // --- Операции с каталогами --- //
    bool create_directory(const std::string &path) const;
    bool remove_directory(const std::string &path) const;
    std::vector<FileSystem::DirectoryEntry> list_directory(const std::string &path) const;

    static std::string get_filename_from_path(const std::string &path); // разбор пути

    FileSystem::Header get_header() const { return header_; }

private:
    VolumeManager vol_manager_;
    std::unique_ptr<BitmapManager> bitmap_manager_;
    std::unique_ptr<FATManager> fat_manager_;
    std::unique_ptr<DirectoryManager> directory_manager_;

    bool mounted_ = false;
    FileSystem::Header header_{};

    std::map<uint32_t, FileSystem::FileHandle> opened_files_table_; // таблица открытых файлов
    uint32_t next_handle_id = 1; // ID следующего дескриптора

    // Вспомогательные методы для работы с файлами
    bool load_cluster_info_buffer(FileSystem::FileHandle &handle, uint32_t cluster_to_load) const;
    bool flush_cluster(FileSystem::FileHandle &handle) const;
    std::optional<uint32_t> allocate_and_link_cluster(FileSystem::FileHandle &handle) const;
    bool update_directory_entry_for_file(const FileSystem::FileHandle &handle) const;

    // Получить начальный кластер каталога (для плоской ФС всегда корневой)
    uint32_t get_containing_directory_cluster(const std::string &path_ignored_for_flat_fs) const;

    // Валидация кластеров
    bool is_valid_cluster(uint32_t cluster_idx) const;

    struct OpenMode {
        bool read = false;
        bool write = false;
        bool append = false;
        bool truncate = false;
        bool create_if_not_exists = false;
    };

    static std::optional<OpenMode> parse_mode(const std::string &mode);
};

#endif //FS_CORE_H