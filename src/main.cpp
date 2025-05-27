#include "fs_core.h"
#include "output.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>

// Вспомогательная функция для разделения строки на вектор строк
std::vector<std::string> parseInput(const std::string& input) {
    std::vector<std::string> tokens;
    std::stringstream ss(input);
    std::string token;
    while (ss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// Вспомогательная функция для вывода справки по командам оболочки
void printShellHelp() {
    std::cout << "\nSimple File System Shell Commands:\n";
    std::cout << "  format <volume_file> <size_MB>        - Formats a new volume.\n";
    std::cout << "  mount <volume_file>                   - Mounts an existing volume.\n";
    std::cout << "  unmount                               - Unmounts the current volume.\n";
    std::cout << "  info                                  - Shows current volume superblock info (requires mount).\n";
    std::cout << "  ls [fs_path]                          - Lists directory contents (default: root '/'). Requires mount.\n";
    std::cout << "  mkdir <fs_dir_path>                   - Creates a directory. Requires mount.\n";
    std::cout << "  rmdir <fs_dir_path>                   - Removes an empty directory. Requires mount.\n";
    std::cout << "  create <fs_file_path>                 - Creates an empty file (or truncates). Requires mount.\n";
    std::cout << "  rm <fs_file_path>                     - Removes a file. Requires mount.\n";
    std::cout << "  write <fs_file_path> \"text ...\"       - Writes text to a file (overwrites). Requires mount.\n";
    std::cout << "  append <fs_file_path> \"text ...\"      - Appends text to a file. Requires mount.\n";
    std::cout << "  cat <fs_file_path>                    - Prints file content to console. Requires mount.\n";
    std::cout << "  rename <old_fs_path> <new_fs_path>    - Renames a file or directory. Requires mount.\n";
    std::cout << "  cp_to_fs <host_src_file> <fs_dest_path> - Copies file from host to FS. Requires mount.\n";
    std::cout << "  cp_from_fs <fs_src_path> <host_dest_file> - Copies file from FS to host. Requires mount.\n";
    std::cout << "  help                                  - Shows this help message.\n";
    std::cout << "  exit / quit                           - Exits the shell.\n";
    std::cout << std::endl;
}

// Функция копирования с хоста в ФС
bool copyHostToFsShell(FileSystemCore& fs, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Usage: cp_to_fs <host_src_file> <fs_dest_path>\n";
        return false;
    }

    const std::string& host_src_path = args[1];
    const std::string& fs_dest_path = args[2];

    std::ifstream host_file(host_src_path, std::ios::binary | std::ios::ate);
    if (!host_file.is_open()) {
        std::cerr << "Error: Cannot open host source file: " << host_src_path << std::endl;
        return false;
    }

    std::streamsize size = host_file.tellg();
    host_file.seekg(0, std::ios::beg);
    std::vector<char> buffer(static_cast<size_t>(size));

    if (!host_file.read(buffer.data(), size)) {
        std::cerr << "Error: Failed to read host source file: " << host_src_path << std::endl;
        host_file.close();
        return false;
    }
    host_file.close();

    auto handle_opt = fs.open_file(fs_dest_path, "w+");
    if (!handle_opt) {
        std::cerr << "Error: Cannot open/create destination file in FS: " << fs_dest_path << std::endl;
        return false;
    }

    uint32_t handle = *handle_opt;
    if (fs.write_file(handle, buffer.data(), buffer.size()) != static_cast<int64_t>(buffer.size())) {
        std::cerr << "Error: Failed to write all data to FS file: " << fs_dest_path << std::endl;
        fs.close_file(handle);
        return false;
    }

    fs.close_file(handle);
    std::cout << "Copied " << host_src_path << " to FS:" << fs_dest_path << std::endl;
    return true;
}

// Функция копирования из ФС на хост
bool copyFsToHostShell(FileSystemCore& fs, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Usage: cp_from_fs <fs_src_path> <host_dest_file>\n";
        return false;
    }

    const std::string& fs_src_path = args[1];
    const std::string& host_dest_path = args[2];

    auto handle_opt = fs.open_file(fs_src_path, "r");
    if (!handle_opt) {
        std::cerr << "Error: Cannot open source file in FS: " << fs_src_path << std::endl;
        return false;
    }

    uint32_t handle = *handle_opt;

    std::vector<char> file_content;
    std::vector<char> read_buffer(FileSystem::CLUSTER_SIZE_BYTES);
    int64_t bytes_read;

    while ((bytes_read = fs.read_file(handle, read_buffer.data(), read_buffer.size())) > 0) {
        file_content.insert(file_content.end(), read_buffer.data(), read_buffer.data() + bytes_read);
    }
    fs.close_file(handle);

    if (bytes_read < 0) {
        std::cerr << "Error: Failed to read FS file: " << fs_src_path << std::endl;
        return false;
    }

    std::ofstream host_file(host_dest_path, std::ios::binary | std::ios::trunc);
    if (!host_file.is_open()) {
        std::cerr << "Error: Cannot open host destination file: " << host_dest_path << std::endl;
        return false;
    }

    if (!file_content.empty()) {
        if (!host_file.write(file_content.data(), file_content.size())) {
            std::cerr << "Error: Failed to write to host destination file: " << host_dest_path << std::endl;
            host_file.close();
            return false;
        }
    }

    host_file.close();
    std::cout << "Copied FS:" << fs_src_path << " to " << host_dest_path << std::endl;
    return true;
}

// Функция для сборки аргументов в одну строку (для команд write/append)
std::string collectTextFromArgs(const std::vector<std::string>& args, size_t start_index) {
    std::string text;
    if (args.size() > start_index) {
        // Проверяем, начинается ли текст с кавычки
        bool in_quotes = !args[start_index].empty() && args[start_index][0] == '"';
        if (in_quotes) {
            text = args[start_index].substr(1); // Убираем первую кавычку
            for (size_t i = start_index + 1; i < args.size(); ++i) {
                text += " " + args[i];
            }
            // Убираем последнюю кавычку, если она есть
            if (!text.empty() && text.back() == '"') {
                text.pop_back();
            }
        } else {
            // Если нет кавычек, берем только первый аргумент после пути
            text = args[start_index];
        }
    }
    return text;
}

int main(int argc, char* argv[]) {
    FileSystemCore fs_core;
    std::string current_volume_file;

    // Попытка автомонтирования, если файл тома передан как аргумент программе
    if (argc == 2) {
        std::string initial_volume = argv[1];
        if (fs_core.mount(initial_volume)) {
            current_volume_file = initial_volume;
            std::cout << "Volume '" << current_volume_file << "' auto-mounted.\n";
        } else {
            std::cout << "Failed to auto-mount volume '" << initial_volume << "'. Please use 'format' or 'mount' command.\n";
        }
    }

    std::string line;
    std::cout << "SimpleFS Shell. Type 'help' for commands.\n";

    while (true) {
        // Показываем приглашение с именем смонтированного тома
        if (fs_core.isMounted()) {
            std::cout << "[" << current_volume_file << "] > ";
        } else {
            std::cout << "FS_Shell > ";
        }

        if (!std::getline(std::cin, line)) {
            break; // EOF (например, Ctrl+D)
        }

        std::vector<std::string> tokens = parseInput(line);
        if (tokens.empty()) {
            continue;
        }

        std::string command = tokens[0];
        // Приводим команду к нижнему регистру для удобства
        std::transform(command.begin(), command.end(), command.begin(), ::tolower);

        if (command == "exit" || command == "quit") {
            break;
        }
        else if (command == "help") {
            printShellHelp();
        }
        else if (command == "format") {
            if (tokens.size() == 3) {
                if (fs_core.isMounted() && tokens[1] == current_volume_file) {
                    std::cout << "Cannot format currently mounted volume. Unmount first.\n";
                } else {
                    uint64_t size_mb = 0;
                    try {
                        size_mb = std::stoull(tokens[2]);
                        if (size_mb == 0) throw std::invalid_argument("Size cannot be zero.");
                        if (fs_core.format(tokens[1], size_mb)) {
                            std::cout << "Volume '" << tokens[1] << "' formatted (" << size_mb << "MB).\n";
                        } else {
                            std::cout << "Failed to format volume '" << tokens[1] << "'.\n";
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Error: Invalid size_MB value: " << tokens[2] << ". " << e.what() << std::endl;
                    }
                }
            } else {
                std::cout << "Usage: format <volume_file> <size_MB>\n";
            }
        }
        else if (command == "mount") {
            if (tokens.size() == 2) {
                if (fs_core.isMounted()) {
                    fs_core.unmount();
                    current_volume_file.clear();
                }
                if (fs_core.mount(tokens[1])) {
                    current_volume_file = tokens[1];
                    std::cout << "Volume '" << current_volume_file << "' mounted.\n";
                } else {
                    std::cout << "Failed to mount volume '" << tokens[1] << "'.\n";
                }
            } else {
                std::cout << "Usage: mount <volume_file>\n";
            }
        }
        else if (command == "unmount") {
            if (fs_core.isMounted()) {
                fs_core.unmount();
                current_volume_file.clear();
                std::cout << "Volume unmounted.\n";
            } else {
                std::cout << "No volume is currently mounted.\n";
            }
        }
        // Команды, требующие смонтированной ФС
        else if (!fs_core.isMounted()) {
            std::cout << "No volume mounted. Mount a volume first or format a new one.\n";
            std::cout << "Available commands: format, mount, help, exit.\n";
        }
        else if (command == "info") {
            const auto& sb = fs_core.get_header();
            std::cout << "--- Superblock Info for " << current_volume_file << " ---\n";
            std::cout << "Signature:         " << std::string(sb.signature, strnlen(sb.signature, 16)) << "\n";
            std::cout << "Volume Size (B):   " << sb.volume_size_bytes << "\n";
            std::cout << "Cluster Size (B):  " << sb.cluster_size_bytes << "\n";
            std::cout << "Total Clusters:    " << sb.total_clusters << "\n";
            std::cout << "Data Start Cl:     " << sb.data_start_cluster << "\n";
            std::cout << "Root Dir Start:    " << sb.root_dir_start_cluster << "\n";
            std::cout << "Root Dir Size:     " << sb.root_dir_size_clusters << "\n";
            std::cout << "FAT Start:         " << sb.fat_start_cluster << "\n";
            std::cout << "FAT Size:          " << sb.fat_size_clusters << "\n";
            std::cout << "Bitmap Start:      " << sb.bitmap_start_cluster << "\n";
            std::cout << "Bitmap Size:       " << sb.bitmap_size_cluster << "\n";
            std::cout << "-------------------------------\n";
        }
        else if (command == "ls") {
            std::string fs_path = (tokens.size() > 1) ? tokens[1] : "/";
            std::vector<FileSystem::DirectoryEntry> entries = fs_core.list_directory(fs_path);

            if (entries.empty() && fs_path != "/") {
                std::cout << "(Directory '" << fs_path << "' is empty or does not exist)\n";
            }

            for (const auto& entry : entries) {
                std::cout << (entry.type == FileSystem::EntityType::DIRECTORY ? "D" : "F") << " ";
                std::cout.width(FileSystem::MAX_FILE_NAME + 1);
                std::cout << std::left << std::string(entry.name.data(), strnlen(entry.name.data(), FileSystem::MAX_FILE_NAME));
                std::cout.width(10);
                std::cout << std::right << entry.file_size_bytes << " B";
                std::cout << "  (Cl: " << entry.first_cluster << ")" << std::endl;
            }
        }
        else if (command == "mkdir") {
            if (tokens.size() == 2) {
                if (fs_core.create_directory(tokens[1])) {
                    std::cout << "Directory '" << tokens[1] << "' created.\n";
                } else {
                    std::cout << "Failed to create directory '" << tokens[1] << "'.\n";
                }
            } else {
                std::cout << "Usage: mkdir <fs_dir_path>\n";
            }
        }
        else if (command == "rmdir") {
            if (tokens.size() == 2) {
                if (fs_core.remove_directory(tokens[1])) {
                    std::cout << "Directory '" << tokens[1] << "' removed.\n";
                } else {
                    std::cout << "Failed to remove directory '" << tokens[1] << "'.\n";
                }
            } else {
                std::cout << "Usage: rmdir <fs_dir_path>\n";
            }
        }
        else if (command == "create") {
            if (tokens.size() == 2) {
                auto handle = fs_core.open_file(tokens[1], "w");
                if (handle) {
                    fs_core.close_file(*handle);
                    std::cout << "File '" << tokens[1] << "' created/truncated.\n";
                } else {
                    std::cout << "Failed to create/truncate file '" << tokens[1] << "'.\n";
                }
            } else {
                std::cout << "Usage: create <fs_file_path>\n";
            }
        }
        else if (command == "rm") {
            // ИСПРАВЛЕНО: Используем remove_file вместо remove_directory
            if (tokens.size() == 2) {
                if (fs_core.remove_file(tokens[1])) {
                    std::cout << "File '" << tokens[1] << "' removed.\n";
                } else {
                    std::cout << "Failed to remove file '" << tokens[1] << "'.\n";
                }
            } else {
                std::cout << "Usage: rm <fs_file_path>\n";
            }
        }
        else if (command == "write" || command == "append") {
            if (tokens.size() >= 3) { // команда, путь, текст
                std::string mode = (command == "write") ? "w+" : "a+";
                std::string text_to_write = collectTextFromArgs(tokens, 2);

                auto handle_opt = fs_core.open_file(tokens[1], mode);
                if (!handle_opt) {
                    std::cout << "Failed to open file '" << tokens[1] << "' for " << command << ".\n";
                } else {
                    uint32_t handle = *handle_opt;
                    int64_t written = fs_core.write_file(handle, text_to_write.c_str(), text_to_write.length());
                    if (written == static_cast<int64_t>(text_to_write.length())) {
                        std::cout << text_to_write.length() << " bytes " << command << "ed to '" << tokens[1] << "'.\n";
                    } else {
                        std::cout << "Failed to write all text (wrote " << written << ").\n";
                    }
                    fs_core.close_file(handle);
                }
            } else {
                std::cout << "Usage: " << command << " <fs_file_path> \"text data\"\n";
            }
        }
        else if (command == "cat") {
            if (tokens.size() == 2) {
                auto handle_opt = fs_core.open_file(tokens[1], "r");
                if (!handle_opt) {
                    std::cout << "Failed to open file '" << tokens[1] << "' for reading.\n";
                } else {
                    uint32_t handle = *handle_opt;
                    char buffer[256];
                    int64_t bytes_read;
                    while ((bytes_read = fs_core.read_file(handle, buffer, sizeof(buffer) - 1)) > 0) {
                        buffer[bytes_read] = '\0';
                        std::cout << buffer << std::endl;
                    }
                    if (bytes_read < 0) {
                        std::cout << "\nError during read." << std::endl;
                    }
                    fs_core.close_file(handle);
                }
            } else {
                std::cout << "Usage: cat <fs_file_path>\n";
            }
        }
        else if (command == "rename") {
            if (tokens.size() == 3) {
                if (fs_core.rename_file(tokens[1], tokens[2])) {
                    std::cout << "Renamed '" << tokens[1] << "' to '" << tokens[2] << "'.\n";
                } else {
                    std::cout << "Rename failed.\n";
                }
            } else {
                std::cout << "Usage: rename <old_fs_path> <new_fs_path>\n";
            }
        }
        else if (command == "cp_to_fs") {
            copyHostToFsShell(fs_core, tokens);
        }
        else if (command == "cp_from_fs") {
            copyFsToHostShell(fs_core, tokens);
        }
        else {
            std::cout << "Unknown command: '" << command << "'. Type 'help' for commands.\n";
        }
    }

    if (fs_core.isMounted()) {
        fs_core.unmount(); // Убедимся, что все отмонтировано при выходе
    }

    std::cout << "Exiting SimpleFS Shell.\n";
    return 0;
}