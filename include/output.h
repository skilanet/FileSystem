#ifndef OUTPUT_H
#define OUTPUT_H

#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace output {

    namespace prefix {
        constexpr auto DIRECTORY_MANAGER_ERROR = "DirectoryManager Error: ";
        constexpr auto BITMAP_MANAGER_ERROR = "BitmapManager Error: ";
        constexpr auto FAT_MANAGER_ERROR = "FATManager Error: ";
        constexpr auto VOLUME_MANAGER_ERROR = "VolumeManager Error: ";
        constexpr auto FILE_SYSTEM_CORE_ERROR = "FileSystemCore Error: ";

        constexpr auto DIRECTORY_MANAGER = "DirectoryManager: ";
        constexpr auto BITMAP_MANAGER = "BitmapManager: ";
        constexpr auto FAT_MANAGER = "FATManager: ";
        constexpr auto VOLUME_MANAGER = "VolumeManager: ";
        constexpr auto FILE_SYSTEM_CORE = "FileSystemCore: ";

        constexpr auto DIRECTORY_MANAGER_WARNING = "DirectoryManager Warning: ";
        constexpr auto BITMAP_MANAGER_WARNING = "BitmapManager Warning: ";
        constexpr auto FAT_MANAGER_WARNING = "FATManager Warning: ";
        constexpr auto VOLUME_MANAGER_WARNING = "VolumeManager Warning: ";
        constexpr auto FILE_SYSTEM_CORE_WARNING = "FileSystemCore Warning: ";
    }

    namespace colors {
        constexpr auto RESET = "\033[0m";
        constexpr auto RED = "\033[31m";
        constexpr auto GREEN = "\033[32m";
        constexpr auto YELLOW = "\033[33m";
        constexpr auto BLUE = "\033[34m";
        constexpr auto MAGENTA = "\033[35m";
        constexpr auto CYAN = "\033[36m";
        constexpr auto WHITE = "\033[37m";

        constexpr auto BOLD_RED = "\033[1;31m";
        constexpr auto BOLD_GREEN = "\033[1;32m";
        constexpr auto BOLD_YELLOW = "\033[1;33m";
    }

    class ColorStream {
        std::ostream &stream_;
        std::string prefix_;
        std::string color_;
        bool need_prefix_;

    public:
        ColorStream(std::ostream &stream, const std::string &prefix, std::string color)
            : stream_(stream), prefix_(prefix), color_(std::move(color)), need_prefix_(!prefix.empty()) {
        }

        template<typename T>
        ColorStream &operator<<(const T &value) {
            stream_ << color_;
            if (need_prefix_) {
                stream_ << prefix_;
                need_prefix_ = false;
            }
            stream_ << value << colors::RESET;
            return *this;
        }

        ColorStream &operator<<(std::ostream & (*manip)(std::ostream &)) {
            stream_ << manip;
            if (manip == static_cast<std::ostream& (*)(std::ostream &)>(std::endl)) {
                need_prefix_ = true;
            }
            return *this;
        }
    };

    inline ColorStream succ(const std::string& pref)  { static ColorStream s(std::cout, pref, colors::BOLD_GREEN); return s; }
    inline ColorStream warn(const std::string& pref)  { static ColorStream s(std::cout, pref, colors::BOLD_YELLOW); return s; }
    inline ColorStream err(const std::string& pref)   { static ColorStream s(std::cerr, pref, colors::BOLD_RED); return s; }
    inline ColorStream info()  { static ColorStream s(std::cout, "[INFO] ", colors::BLUE); return s; }
    inline ColorStream debug() { static ColorStream s(std::cout, "[DEBUG] ", colors::CYAN); return s; }
}

#endif // OUTPUT_H
