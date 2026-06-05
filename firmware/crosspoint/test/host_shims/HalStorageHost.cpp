/* host shim: HalStorage over plain POSIX — same header as the target build
 * (test/host_shims/HalStorage.h is a copy of port/hal/HalStorage.h), so
 * vendored code like ZipFile links unmodified against host files. */

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "HalStorage.h"
#include "HardwareSerial.h"

HalStorage HalStorage::instance;

class HalFile::Impl
{
public:
    int fd = -1;
    DIR *dir = nullptr;
    std::string path;

    ~Impl() { closeAll(); }
    void closeAll()
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
        if (dir)
        {
            ::closedir(dir);
            dir = nullptr;
        }
    }
};

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> i) : impl(std::move(i)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile &&) = default;
HalFile &HalFile::operator=(HalFile &&) = default;

void HalFile::flush()
{
    if (impl && impl->fd >= 0) ::fsync(impl->fd);
}

size_t HalFile::getName(char *name, size_t len)
{
    if (!impl || !name || !len) return 0;
    const char *base = strrchr(impl->path.c_str(), '/');
    base = base ? base + 1 : impl->path.c_str();
    size_t n = strlen(base);
    if (n >= len) n = len - 1;
    memcpy(name, base, n);
    name[n] = '\0';
    return n;
}

size_t HalFile::size() { return fileSize(); }

size_t HalFile::fileSize()
{
    if (!impl) return 0;
    struct stat st;
    if (::stat(impl->path.c_str(), &st) != 0) return 0;
    return (size_t)st.st_size;
}

uint64_t HalFile::fileSize64() { return fileSize(); }

bool HalFile::seek(size_t pos)
{
    return impl && impl->fd >= 0 && ::lseek(impl->fd, (off_t)pos, SEEK_SET) >= 0;
}
bool HalFile::seek64(uint64_t pos) { return seek((size_t)pos); }
bool HalFile::seekSet(size_t pos) { return seek(pos); }
bool HalFile::seekCur(int64_t offset)
{
    return impl && impl->fd >= 0 && ::lseek(impl->fd, (off_t)offset, SEEK_CUR) >= 0;
}

size_t HalFile::position() const
{
    if (!impl || impl->fd < 0) return 0;
    off_t p = ::lseek(impl->fd, 0, SEEK_CUR);
    return p < 0 ? 0 : (size_t)p;
}

int HalFile::available() const
{
    if (!impl || impl->fd < 0) return 0;
    off_t cur = ::lseek(impl->fd, 0, SEEK_CUR);
    struct stat st;
    if (cur < 0 || ::fstat(impl->fd, &st) != 0) return 0;
    return (int)(st.st_size - cur);
}

int HalFile::read(void *buf, size_t count)
{
    if (!impl || impl->fd < 0) return -1;
    return (int)::read(impl->fd, buf, count);
}

int HalFile::read()
{
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
}

size_t HalFile::write(const void *buf, size_t count)
{
    if (!impl || impl->fd < 0) return 0;
    ssize_t n = ::write(impl->fd, buf, count);
    return n < 0 ? 0 : (size_t)n;
}

size_t HalFile::write(uint8_t b) { return write(&b, 1); }

bool HalFile::rename(const char *newPath)
{
    if (!impl) return false;
    std::string old = impl->path;
    impl->closeAll();
    if (::rename(old.c_str(), newPath) != 0) return false;
    impl->path = newPath;
    impl->fd = ::open(newPath, O_RDONLY);
    return true;
}

bool HalFile::isDirectory() const { return impl && impl->dir; }

void HalFile::rewindDirectory()
{
    if (impl && impl->dir) ::rewinddir(impl->dir);
}

bool HalFile::close()
{
    if (!impl) return false;
    impl->closeAll();
    return true;
}

HalFile HalFile::openNextFile()
{
    if (!impl || !impl->dir) return HalFile();
    struct dirent *ent = ::readdir(impl->dir);
    if (!ent) return HalFile();
    std::string child = impl->path;
    if (child.empty() || child.back() != '/') child += '/';
    child += ent->d_name;
    return Storage.open(child.c_str(), O_RDONLY);
}

bool HalFile::isOpen() const { return impl && (impl->fd >= 0 || impl->dir); }
HalFile::operator bool() const { return isOpen(); }

/* --------------------------------------------------------------- HalStorage */
HalStorage::HalStorage() = default;

bool HalStorage::begin()
{
    initialized = true;
    return true;
}

bool HalStorage::ready() const { return initialized; }

std::vector<String> HalStorage::listFiles(const char *path, int maxFiles)
{
    std::vector<String> out;
    DIR *d = ::opendir(path);
    if (!d) return out;
    struct dirent *ent;
    while ((int)out.size() < maxFiles && (ent = ::readdir(d)))
        out.emplace_back(ent->d_name);
    ::closedir(d);
    return out;
}

String HalStorage::readFile(const char *path)
{
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return String();
    std::string s;
    char buf[256];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0)
        s.append(buf, (size_t)n);
    ::close(fd);
    return String(std::move(s));
}

bool HalStorage::readFileToStream(const char *path, Print &out, size_t chunkSize)
{
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return false;
    if (chunkSize == 0 || chunkSize > 1024) chunkSize = 256;
    char buf[1024];
    ssize_t n;
    while ((n = ::read(fd, buf, chunkSize)) > 0)
        out.write((const uint8_t *)buf, (size_t)n);
    ::close(fd);
    return n >= 0;
}

size_t HalStorage::readFileToBuffer(const char *path, char *buffer, size_t bufferSize, size_t maxBytes)
{
    if (!buffer || bufferSize == 0) return 0;
    int fd = ::open(path, O_RDONLY);
    if (fd < 0)
    {
        buffer[0] = '\0';
        return 0;
    }
    size_t want = bufferSize - 1;
    if (maxBytes && maxBytes < want) want = maxBytes;
    ssize_t n = ::read(fd, buffer, want);
    ::close(fd);
    if (n < 0) n = 0;
    buffer[n] = '\0';
    return (size_t)n;
}

bool HalStorage::writeFile(const char *path, const String &content)
{
    int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    ssize_t n = ::write(fd, content.c_str(), content.length());
    ::close(fd);
    return n == (ssize_t)content.length();
}

bool HalStorage::ensureDirectoryExists(const char *path) { return mkdir(path, true); }

HalFile HalStorage::open(const char *path, const oflag_t oflag)
{
    auto impl = std::make_unique<HalFile::Impl>();
    impl->path = path;

    struct stat st;
    bool isDir = (::stat(path, &st) == 0) && S_ISDIR(st.st_mode);
    if (isDir)
    {
        impl->dir = ::opendir(path);
        if (!impl->dir) return HalFile();
        return HalFile(std::move(impl));
    }

    int flags = oflag & ~O_AT_END;
    impl->fd = ::open(path, flags, 0644);
    if (impl->fd < 0) return HalFile();
    if (oflag & O_AT_END) ::lseek(impl->fd, 0, SEEK_END);
    return HalFile(std::move(impl));
}

bool HalStorage::mkdir(const char *path, const bool pFlag)
{
    if (!path || !*path) return false;
    struct stat st;
    if (::stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    if (!pFlag) return ::mkdir(path, 0755) == 0;
    std::string p(path);
    for (size_t i = 1; i < p.size(); i++)
    {
        if (p[i] == '/')
        {
            p[i] = '\0';
            if (::stat(p.c_str(), &st) != 0) ::mkdir(p.c_str(), 0755);
            p[i] = '/';
        }
    }
    return ::mkdir(p.c_str(), 0755) == 0 || (::stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

bool HalStorage::exists(const char *path)
{
    struct stat st;
    return ::stat(path, &st) == 0;
}

bool HalStorage::remove(const char *path) { return ::unlink(path) == 0; }
bool HalStorage::rename(const char *oldPath, const char *newPath) { return ::rename(oldPath, newPath) == 0; }
bool HalStorage::rmdir(const char *path) { return ::rmdir(path) == 0; }
bool HalStorage::removeDir(const char *path) { return rmdir(path); }

static bool openChecked(const char *, const char *path, oflag_t flags, HalFile &file)
{
    file = Storage.open(path, flags);
    return (bool)file;
}

bool HalStorage::openFileForRead(const char *m, const char *p, HalFile &f) { return openChecked(m, p, O_RDONLY, f); }
bool HalStorage::openFileForRead(const char *m, const std::string &p, HalFile &f) { return openChecked(m, p.c_str(), O_RDONLY, f); }
bool HalStorage::openFileForRead(const char *m, const String &p, HalFile &f) { return openChecked(m, p.c_str(), O_RDONLY, f); }
bool HalStorage::openFileForWrite(const char *m, const char *p, HalFile &f)
{
    return openChecked(m, p, O_WRONLY | O_CREAT | O_TRUNC, f);
}
bool HalStorage::openFileForWrite(const char *m, const std::string &p, HalFile &f) { return openFileForWrite(m, p.c_str(), f); }
bool HalStorage::openFileForWrite(const char *m, const String &p, HalFile &f) { return openFileForWrite(m, p.c_str(), f); }

HWCDC Serial; /* host Logging sink */
