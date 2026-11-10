#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

namespace hnswhytest {

class TempPath {
public:
    explicit TempPath(const std::string& suffix = ".db") {
        static std::atomic<unsigned long long> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("hnswhy_test_" + std::to_string(::getpid()) + "_" +
                 std::to_string(counter.fetch_add(1)) + suffix);
        cleanup();
    }

    ~TempPath() { cleanup(); }

    TempPath(const TempPath&) = delete;
    TempPath& operator=(const TempPath&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    void cleanup() {
        std::error_code error;
        std::filesystem::remove(path_, error);
        std::filesystem::remove(path_.string() + "-wal", error);
        std::filesystem::remove(path_.string() + "-shm", error);
    }

    std::filesystem::path path_;
};

class TempDir {
public:
    explicit TempDir(const std::string& suffix = "") {
        static std::atomic<unsigned long long> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("hnswhy_dir_" + std::to_string(::getpid()) + "_" +
                 std::to_string(counter.fetch_add(1)) + suffix);
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace hnswhytest
