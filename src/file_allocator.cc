#include <iostream>
#include "file_allocator.h"

#include <stdlib.h>

#include <algorithm>

#include <sys/mman.h>
#include <unistd.h>

#include <cstring>

#include <vector>
#include <stdio.h>
#include <strings.h>

#include <cstdlib>

#include <fcntl.h>

static const size_t MIN_SIZE = 1024 * 1024 * 512;
static const size_t PAGE_SIZE = 1024 * 4;

enum LogLevel
{
    Fatal = 0,
    Warn,
    Info,
    Debug
};
static LogLevel current_level = Fatal;

static const char *mmap_file_dir = "/dev/shm";
static std::string mmap_file_dir_tmp;
// static std::string file_template = "/dev/shm";
extern "C" void set_file_allocator_directory(const char *path)
{
    mmap_file_dir_tmp = path;
    mmap_file_dir_tmp.push_back('\0');
    mmap_file_dir = &mmap_file_dir_tmp[0];
}
extern "C" void init_file_allocator_module()
{
    current_level = []()
    {
        const char *env_p = std::getenv("FILE_ALLOCATOR_LOG_LEVEL");
        if (env_p != nullptr)
        {
            if (strcasecmp(env_p, "Fatal") == 0)
            {
                return Fatal;
            }
            else if (strcasecmp(env_p, "Warn") == 0)
            {
                return Warn;
            }
            else if (strcasecmp(env_p, "Info") == 0)
            {
                return Info;
            }
            else if (strcasecmp(env_p, "Debug") == 0)
            {
                return Debug;
            }
        }
        return Warn;
    }();
}

#define LOG(level, fmt, ...)                            \
    {                                                   \
        if (level <= current_level)                     \
        {                                               \
            printf("[" #level "] " fmt, ##__VA_ARGS__); \
            printf("\n");                               \
        }                                               \
    }

static inline void *
pointer_advance(void *p, ptrdiff_t n)
{
    return (unsigned char *)p + n;
}

int FileAllocator::create_buffer(size_t size)
{
    // std::cout << "create_buffer" << std::endl;
    int fd;
    std::string file_template = mmap_file_dir;
    file_template += "/mmap_file_XXXXXX";
    std::vector<char> file_name(file_template.begin(), file_template.end());
    file_name.push_back('\0');
    // std::cout << "create_buffer size = " << size << " , path = " <<
    // &file_name[0]
    //           << std::endl;
    fd = mkstemp(&file_name[0]);
    if (fd < 0)
    {
        // std::cerr << "create_buffer failed to open file " << &file_name[0]
        //           << std::endl;
        fprintf(stderr, "create_buffer failed to open file %s \n", &file_name[0]);
        return -1;
    }
    LOG(Debug, "create mmap file: %s, size: %ld", &file_name[0], size);

    // Immediately unlink the file so we do not leave traces in the system.
    if (unlink(&file_name[0]) != 0)
    {
        // std::cerr << "failed to unlink file " << &file_name[0] << std::endl;
        fprintf(stderr, "failed to unlink file %s\n", &file_name[0]);
        return -1;
    }
    if (true)
    {
        // Increase the size of the file to the desired size. This seems not to be
        // needed for files that are backed by the huge page fs, see also
        // http://www.mail-archive.com/kvm-devel@lists.sourceforge.net/msg14737.html
        if (ftruncate(fd, (off_t)size) != 0)
        {
            // std::cerr << "failed to ftruncate file " << &file_name[0] << std::endl;
            fprintf(stderr, "failed to ftruncate file %s\n", &file_name[0]);
            return -1;
        }
    }
    return fd;
}

void *FileAllocator::fake_mmap(void *hint, size_t size)
{
    int fd = create_buffer(size);
    if (fd < 0)
    {
        // std::cerr << "Failed to create buffer during mmap" << std::endl;
        fprintf(stderr, "Failed to create buffer during mmap");
        return MAP_FAILED;
    }
    void *pointer = mmap(hint, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // void* pointer = mmap(hint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd,
    // 0);
    if (pointer == MAP_FAILED)
    {
        // std::cerr << "mmap failed with error: " << std::strerror(errno)
        //   << std::endl;
        fprintf(stderr, "mmap failed with error: %s\n", std::strerror(errno));
        return pointer;
    }

    MmapRecord &record = mmap_records[pointer];
    record.fd = fd;
    record.size = size;

    return pointer;
}

int FileAllocator::fake_munmap(void *addr, size_t size)
{
    // std::cout << "fake_munmap addr = " << addr << " , size = " << size <<
    // std::endl;
    auto entry = mmap_records.find(addr);

    if (entry == mmap_records.end() || entry->second.size != size)
    {
        // Reject requests to munmap that don't directly match previous
        // calls to mmap
        // std::cerr << "munmap not match, addr = " << addr << ", size = " << size
        //           << std::endl;
        fprintf(stderr, "munmap not match, addr: %p, size: %zd\n", addr, size);
        return -1;
    }

    int r = munmap(addr, size);
    if (r != 0)
    {
        // std::cerr << "munmap failed, addr = " << addr << ", size = " << size
        //   << ", error = " << std::strerror(errno) << std::endl;
        fprintf(stderr, "munmap failed, addr: %p, size: %zd, error: %s", addr, size, std::strerror(errno));
        return r;
    }

    if (entry->second.fd >= 0)
    {
        close(entry->second.fd);
    }

    mmap_records.erase(entry);
    return r;
}

void *FileAllocator::Alloc(size_t size, size_t *actual_size,
                           size_t alignment)
{
    // printf("xxx FileAllocator::Alloc\n");
    // void *ret1;
    // if (posix_memalign(&ret1, alignment, size) == 0)
    // {
    //     *actual_size = size;
    // }
    // else
    // {
    //     *actual_size = 0;
    //     ret1 = nullptr;
    // }
    // return ret1;
    ///
    // std::cout << "init size: " << size << " ,";
    size = std::max(size, MIN_SIZE);
    if (alignment > PAGE_SIZE)
    {
        size = size + alignment - PAGE_SIZE;
    }
    // size = std::max(size, MIN_SIZE); todo
    size = (size & (~(PAGE_SIZE - 1))) + ((size & (PAGE_SIZE - 1)) > 0 ? PAGE_SIZE : 0);

    void *ret = fake_mmap(nullptr, size);
    // std::cout << "mmap ret: "<< ret << " ,";
    if (ret == MAP_FAILED)
    {
        *actual_size = 0;
    }
    else
    {
        size_t advance_size = (alignment - (((size_t)ret) % alignment)) % alignment;
        *actual_size = size - advance_size;
        ret = pointer_advance(ret, advance_size);
        // std::cout << "advance_size: " << advance_size << " ,";
    }
    // void *ret;
    // if (posix_memalign(&ret, alignment, size) == 0)
    // {
    //     *actual_size = size;
    // }
    // else
    // {
    //     *actual_size = 0;
    //     ret = nullptr;
    // }
    LOG(Debug, "FileAllocator ret: %p, size: %ld, actual_size: %ld, alignment: %ld", ret, size, *actual_size, alignment);
    return ret;
}