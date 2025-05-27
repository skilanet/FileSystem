# Simple File System

Простая файловая система с поддержкой базовых операций работы с файлами и каталогами. Реализована на C++ с использованием FAT-подобной архитектуры.

## Структура проекта

- [Менеджер томов (VolumeManager)](documentation/VolumeReadme.md) — управление файлом-контейнером
- [Менеджер битовой карты (BitmapManager)](documentation/BitmapReadme.md) — отслеживание свободных кластеров
- [Менеджер FAT (FATManager)](documentation/FATReadme.md) — управление цепочками кластеров
- [Менеджер каталогов (DirectoryManager)](documentation/DirectoryReadme.md) — работа с каталогами
- [Конфигурация (FileSystemConfig)](documentation/ConfigReadme.md) — основные структуры и константы

## Сборка проекта

```bash
mkdir build
cd build
cmake ..
make
```

## Использование

После сборки запустите исполняемый файл:

```bash
./FileSystem
```

### Основные команды

**Управление томом:**

- `format <volume_file> <size_MB>` - создать и отформатировать новый том
- `mount <volume_file>` - примонтировать существующий том
- `unmount` - размонтировать текущий том
- `info` - показать информацию о примонтированном томе

**Работа с файлами:**

- `create <fs_file_path>` - создать пустой файл
- `rm <fs_file_path>` - удалить файл
- `write <fs_file_path> "text"` - записать текст в файл (перезапись)
- `append <fs_file_path> "text"` - добавить текст в конец файла
- `cat <fs_file_path>` - вывести содержимое файла
- `rename <old_path> <new_path>` - переименовать файл

**Работа с каталогами:**

- `ls [fs_path]` - список файлов в каталоге (по умолчанию корневой)
- `mkdir <fs_dir_path>` - создать каталог
- `rmdir <fs_dir_path>` - удалить пустой каталог

**Копирование файлов:**

- `cp_to_fs <host_file> <fs_path>` - скопировать файл с хоста в ФС
- `cp_from_fs <fs_path> <host_file>` - скопировать файл из ФС на хост

**Прочее:**

- `help` - показать справку по командам
- `exit` или `quit` - выйти из программы

### Примеры использования

```bash
# Создание нового тома размером 100 МБ
FS_Shell > format myvolume.fs 100

# Монтирование тома
FS_Shell > mount myvolume.fs

# Создание файла и запись в него
[myvolume.fs] > create test.txt
[myvolume.fs] > write test.txt "Hello, World!"

# Просмотр содержимого
[myvolume.fs] > cat test.txt
Hello, World!

# Создание каталога (в текущей версии поддерживается только плоская структура)
[myvolume.fs] > mkdir documents

# Копирование файла с хоста
[myvolume.fs] > cp_to_fs /home/user/file.txt file.txt

# Просмотр списка файлов
[myvolume.fs] > ls
F test.txt           13 B  (Cl: 5)
D documents           0 B  (Cl: 6)
F file.txt         1024 B  (Cl: 7)
```

## Особенности реализации

- Размер кластера: 4096 байт
- Максимальная длина имени файла: 255 символов
- Поддерживается только плоская структура каталогов (все файлы в корне)
- Файловая система использует FAT для управления цепочками кластеров
- Битовая карта для отслеживания свободного пространства