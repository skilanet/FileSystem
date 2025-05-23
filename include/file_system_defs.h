#ifndef FILE_SYSTEM_DEFS_H
#define FILE_SYSTEM_DEFS_H

#include <cstdint>

#pragma once
namespace FileSystem {
    constexpr uint32_t CLUSTER_SIZE_BYTES = 4096; // размер одного кластера 4096 байт -> 4 Кб
    constexpr uint8_t MAX_FILE_NAME = 255; // максимальная длинна имени файла
    constexpr uint16_t ROOT_DIRECTORY_CLUSTER_COUNT = 1; // изначальный размер корневого каталога

    // системные маркеры для FAT
    constexpr uint32_t MARKER_FAT_ENRY_FREE = 0x00000000; // кластер свободен
    constexpr uint32_t MARKER_FAT_ENTRY_EOF = 0xFFFFFFFF; // маркер конца файла
    // любое другое значение - указатель на следующий кластер

    struct Header {
        char signature[16]; // идентификатор для заголовка
        uint64_t volume_size_bytes; // общий размер тома в байтах
        uint32_t cluster_size_bytes; // размер кластера
        uint32_t total_clusters; // количество кластеров в ФС

        uint32_t bitmap_start_cluster; // номер первого кластера битовой карты
        uint32_t bitmap_size_cluster; // количество кластеров занимаемых битовой картой

        uint32_t fat_start_cluster; // номер первого кластера fat
        uint32_t fat_size_clusters; // количество занимаемых fat кластеров

        uint32_t root_dir_start_cluster; // номер первого кластера корневого каталога
        uint32_t root_dir_size_clusters; // размер корневого каталога

        uint32_t data_start_cluster; // номер первого доступного для записи кластера
    };

    // проверка возможности поместить заголовок в один кластер
    static_assert(sizeof(Header) <= CLUSTER_SIZE_BYTES, "Header is too large for one cluster");

    enum EntityType: uint8_t {
        // тип сущности файл/директория
        FILE = 0, // файл
        DIRECTORY = 1, // директория
    };

    struct DirectoryEntry {
        //запись каталога
        std::array<char, MAX_FILE_NAME> name{}; // сд для хранения имени файла
        EntityType type; // тип
        uint8_t reserved[3]{}; // резерв + выравнивание памяти

        uint32_t first_cluster;
        uint32_t file_size_bytes;

        DirectoryEntry(): type(FILE), first_cluster(MARKER_FAT_ENRY_FREE), file_size_bytes(0) {
            name.fill('\0');
            reserved_fill('\0');
        }

        void reserved_fill(const char val_) {
            for (unsigned char &i: reserved)
                i = val_;
        }
    };

    constexpr uint32_t DIR_ENTRIES_PER_CLUSTER = CLUSTER_SIZE_BYTES / sizeof(DirectoryEntry);
}

#endif //FILE_SYSTEM_DEFS_H
