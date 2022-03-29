#pragma once

#include <gperftools/malloc_extension.h>
#include <unordered_map>
struct MmapRecord
{
    int fd;
    size_t size;
};
class FileAllocator : public SysAllocator
{
public:
    FileAllocator() : SysAllocator()
    {
    }
    void *Alloc(size_t size, size_t *actual_size, size_t alignment);

private:
    std::unordered_map<void *, MmapRecord> mmap_records;
    int create_buffer(size_t size);
    void *fake_mmap(void *hint, size_t size);
    int fake_munmap(void *addr, size_t size);
};

extern "C"
{
    void init_file_allocator_module();
    void set_file_allocator_directory(const char *path);
}
