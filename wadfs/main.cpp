#include "Wad.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fuse.h>
#include <string>
#include <vector>

static Wad* g_wad = nullptr; // global pointer to loaded WAD

/* -------------------------------------------------------------- */
/*  Helpers                                                      */
/* -------------------------------------------------------------- */

static int wadfs_getattr(const char* path, struct stat* st)
{
    memset(st, 0, sizeof(struct stat));

    Node* node = g_wad->resolve(std::string(path));
    if (!node)
        return -ENOENT;

    st->st_uid = fuse_get_context()->uid;
    st->st_gid = fuse_get_context()->gid;
    st->st_atime = st->st_mtime = st->st_ctime = time(nullptr);

    if (node->isDir) {
        st->st_mode = S_IFDIR | 0777;
        st->st_nlink = 2 + node->children.size();
        st->st_size = 0;
    } else {
        st->st_mode = S_IFREG | 0777;
        st->st_nlink = 1;
        st->st_size = node->length;
    }
    return 0;
}

static int wadfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t /*offset*/,
    struct fuse_file_info* /*fi*/)
{
    Node* dir = g_wad->resolve(std::string(path));
    if (!dir || !dir->isDir)
        return -ENOENT;

    filler(buf, ".", nullptr, 0);
    filler(buf, "..", nullptr, 0);

    for (Node* child : dir->children)
        filler(buf, child->name.c_str(), nullptr, 0);

    return 0;
}

static int wadfs_read(
    const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info*)
{
    int n = g_wad->getContents(
        std::string(path), buf, static_cast<int>(size), static_cast<int>(offset));
    return (n < 0) ? -ENOENT : n;
}

static int wadfs_write(
    const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info*)
{
    int n = g_wad->writeToFile(
        std::string(path), buf, static_cast<int>(size), static_cast<int>(offset));
    return (n < 0) ? -EACCES : n;
}

static int wadfs_mkdir(const char* path, mode_t /*mode*/)
{
    if (g_wad->isDirectory(std::string(path)))
        return -EEXIST;
    g_wad->createDirectory(std::string(path));
    return 0;
}

static int wadfs_mknod(const char* path, mode_t mode, dev_t /*rdev*/)
{
    if (!S_ISREG(mode))
        return -EPERM; // only regular files
    if (g_wad->isContent(std::string(path)))
        return -EEXIST;
    g_wad->createFile(std::string(path));
    return 0;
}

/* -------------------------------------------------------------- */
/*  ops table                                                     */
/* -------------------------------------------------------------- */

static struct fuse_operations wadfs_ops {
    .getattr = wadfs_getattr,
    .readdir = wadfs_readdir,
    .read = wadfs_read,
    .write = wadfs_write,
    .mkdir = wadfs_mkdir,
    .mknod = wadfs_mknod,
};

/* -------------------------------------------------------------- */
/*  main                                                          */
/* -------------------------------------------------------------- */

int main(int argc, char* argv[])
{
    if (argc < 4 || strcmp(argv[1], "-s") != 0) {
        fprintf(stderr, "Usage: %s -s <wadfile> <mountpoint> [FUSE opts]\n", argv[0]);
        return 1;
    }

    std::string wadPath = argv[2];
    g_wad = Wad::loadWad(wadPath);
    if (!g_wad) {
        fprintf(stderr, "Failed to load WAD %s\n", wadPath.c_str());
        return 1;
    }

    /* shift argv so that FUSE sees argv[2] as mountpoint */
    argv[2] = argv[3]; // mount point
    argc -= 2; // drop -s and wadfile

    int ret = fuse_main(argc, argv, &wadfs_ops, nullptr);

    delete g_wad;
    return ret;
}
