#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>
#include "../libWad/Wad.h"

static Wad* g_wad = nullptr;   // loaded WAD handle

/* ------------------------------------------------------------- */
/*  Helpers                                                      */
/* ------------------------------------------------------------- */
static bool path_is_root(const char* path) {
    return std::strcmp(path, "/") == 0;
}

/* ------------------------------------------------------------- */
/*  FUSE callbacks                                               */
/* ------------------------------------------------------------- */

static int wadfs_getattr(const char* path, struct stat* stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    if (path_is_root(path)) {
        stbuf->st_mode  = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
        return 0;
    }

    if (g_wad->isDirectory(path)) {
        stbuf->st_mode  = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
        return 0;
    }

    if (g_wad->isContent(path)) {
        stbuf->st_mode  = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size  = g_wad->getSize(path);
        return 0;
    }

    return -ENOENT;
}

static int wadfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                         off_t /*offset*/, struct fuse_file_info* /*fi*/)
{
    // always add . and ..
    filler(buf, ".",  nullptr, 0);
    filler(buf, "..", nullptr, 0);

    // root directory
    if (path_is_root(path)) {
        std::vector<std::string> list;
        g_wad->getDirectory("/", &list);
        for (const auto& name : list) filler(buf, name.c_str(), nullptr, 0);
        return 0;
    }

    // other directories
    if (!g_wad->isDirectory(path)) return -ENOTDIR;

    std::vector<std::string> entries;
    g_wad->getDirectory(path, &entries);
    for (const auto& n : entries) filler(buf, n.c_str(), nullptr, 0);

    return 0;
}

static int wadfs_read(const char* path, char* buf, size_t size, off_t offset,
                      struct fuse_file_info* /*fi*/)
{
    int n = g_wad->getContents(path, buf, static_cast<int>(size), static_cast<int>(offset));
    return (n < 0) ? -EIO : n;
}

static int wadfs_write(const char* path, const char* buf, size_t size, off_t offset,
                       struct fuse_file_info* /*fi*/)
{
    int n = g_wad->writeToFile(path, buf, static_cast<int>(size), static_cast<int>(offset));
    return (n < 0) ? -EIO : n;
}

static int wadfs_mkdir(const char* path, mode_t /*mode*/)
{
    if (g_wad->isContent(path) || g_wad->isDirectory(path)) return -EEXIST;
    g_wad->createDirectory(path);
    return 0;
}

static int wadfs_mknod(const char* path, mode_t mode, dev_t /*dev*/)
{
    if (!S_ISREG(mode)) return -EPERM; // only regular files supported
    if (g_wad->isContent(path) || g_wad->isDirectory(path)) return -EEXIST;
    g_wad->createFile(path);
    return 0;
}

/* ------------------------------------------------------------- */
/*  main                                                          */
/* ------------------------------------------------------------- */

static struct fuse_operations wadfs_ops; // zero‑initialised global

int main(int argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s [-s] <wadfile> <mountpoint> [FUSE opts]\n", argv[0]);
        return 1;
    }

    // wad file is second‑last argument, mountpoint is last.
    std::string wadPath = argv[argc - 2];

    g_wad = Wad::loadWad(wadPath);
    if (!g_wad) {
        fprintf(stderr, "Failed to load WAD %s\n", wadPath.c_str());
        return 1;
    }

    // remove wadPath from argv so FUSE doesn't see it
    std::vector<char*> fuse_argv;
    for (int i = 0; i < argc; ++i) if (i != argc - 2) fuse_argv.push_back(argv[i]);
    int fuse_argc = static_cast<int>(fuse_argv.size());

    // fill operations table
    wadfs_ops.getattr = wadfs_getattr;
    wadfs_ops.readdir = wadfs_readdir;
    wadfs_ops.read    = wadfs_read;
    wadfs_ops.write   = wadfs_write;
    wadfs_ops.mkdir   = wadfs_mkdir;
    wadfs_ops.mknod   = wadfs_mknod;

    int ret = fuse_main(fuse_argc, fuse_argv.data(), &wadfs_ops, nullptr);

    delete g_wad;
    return ret;
}
