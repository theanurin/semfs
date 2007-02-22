#ifndef   	SEMFS_H_
#define   	SEMFS_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <ext2fs/ext2fs.h>
#include <ext2fs/ext2_fs.h>

typedef struct semfs_s {
    int id;
//    char device[32];
    ext2_filsys fs;
    char *device;
    char *mnt_point;
    int sock;
} semfs_t;

struct dirbuf {
    char *p;
    size_t size;
};

struct semfs_dirbuf {
    struct dirbuf b;
    struct semfs_s *chunk;
    int following_cnode;	/* for avoiding multiple '.' and '..' */
};

/* this is declared in fileio.c of e2fsprogs, but we need it  */
struct ext2_file {
    errcode_t magic;
    ext2_filsys fs;
    ext2_ino_t ino;
    struct ext2_inode inode;
    int flags;
    __u64 pos;
    blk_t blockno;
    blk_t physblock;
    char *buf;
};

typedef struct ext2_inode semfs_inode_t;
typedef ext2_ino_t semfs_ino_t;
// typedef struct ext2_file semfs_file_t;

typedef struct semfs_file_s {
    unsigned long file;
    semfs_ino_t ino;
    size_t size;
    struct semfs_file_s *next, *prev;
} semfs_file_t;

#ifdef DEBUG
#define dbg(fmt, arg...) \
	do { \
		printf("%s: " fmt "\n", __FUNCTION__,  ## arg);     \
	} while(0)
#else
#define dbg(fmt, arg...) \
	do { } while(0)
#endif

#define min(x, y) ((x) < (y) ? (x) : (y))
#define ERROR(msg) fprintf(stderr, msg)
#define SEMFS_ROOT_INO EXT2_ROOT_INO
/* #define SEMFS_INO_MASK 0x00ffffff		/\* an inode's first byte indicates chunk number */
/* 						   and rest is the actual inode number *\/ */

/* file flags */
#define SEMFS_FILE_RW	EXT2_FILE_WRITE

/* inode flags */
#define SEMFS_HAS_CONT_INODE	0x00100000
#define SEMFS_IS_CONT_INODE	0x00200000
#define ALLOC_INODES	0x1
#define ALLOC_BLOCKS	0x2

/* all the 'ino's coming from fuse should be passed through SEMFS_INO  */
#define SEMFS_INO(ino) ((semfs_ino_t)(((ino) < SEMFS_ROOT_INO ? SEMFS_ROOT_INO : (ino)) ))

/* convert fuse file-handle to semfs file-handle */
#define SEMFS_FILE(ffh) ((semfs_file_t *) (unsigned long) (ffh))
/* get ext2 file-handle from fuse file-handle */
#define SEMFS_FH(ffh) ((ext2_file_t)SEMFS_FILE((ffh))->file)
/* get ext2fs_filsys (i.e. sem_t) from fuse file-handle */
#define SEMFS_FS(ffh) (SEMFS_FILE((ffh))->fs)

#define i_cont_ino	i_reserved1
#define i_back_ptr	i_reserved2

#define free_inode_count(fs) (fs->super->s_free_inodes_count)
#define free_blocks_count(fs) (fs->super->s_free_blocks_count)

/* ext2fs.c */
void fill_statbuf(ext2_ino_t, struct ext2_inode *, struct stat *);

int read_inode(ext2_filsys, ext2_ino_t, struct ext2_inode *);
int write_inode(ext2_filsys, ext2_ino_t, struct ext2_inode *);
int ext2_file_type(unsigned int);
int get_ino_by_name(ext2_filsys, ext2_ino_t ,const char *, ext2_ino_t *);

int do_dir_iterate(ext2_filsys, ext2_ino_t, int, int (*)(struct ext2_dir_entry *, int, int, char *, void *), void *);
int do_lookup(ext2_filsys, ext2_ino_t, const char *, ext2_ino_t *, struct ext2_inode *);
ext2_file_t do_open(ext2_filsys, ext2_ino_t, int);
int do_create(ext2_filsys, ext2_ino_t, const char *, mode_t,
				ext2_ino_t *, struct ext2_inode *);
void kill_file_by_inode(ext2_filsys, ext2_ino_t);
int do_link(ext2_filsys, ext2_ino_t, const char *, ext2_ino_t, int);
int do_unlink(ext2_filsys, ext2_ino_t, const char *, int);
int do_read(struct ext2_file *, ext2_ino_t, size_t, off_t, char *, unsigned int *);
int do_write(struct ext2_file *, ext2_ino_t, const char *, size_t, off_t, unsigned int *);

int do_file_flush(struct ext2_file *fh);
int do_file_close(struct ext2_file *fh);

int do_mkdir(ext2_filsys, ext2_ino_t, const char *, mode_t, ext2_ino_t *);
int do_rmdir(ext2_filsys, ext2_ino_t, const char *);

int do_statvfs(ext2_filsys, struct statvfs *);

/* int reply_buf_limited (fuse_req_t, const char *, size_t, off_t, size_t); */
/* void dirbuf_add (struct semfs_dirbuf *dbuf, const char *name, */
/*                  ext2_ino_t ino); */
/* int walk_dir (struct ext2_dir_entry *de, int offset, int blocksize, */
/*               char *buf, void *priv_data); */
size_t semfs_get_file_size (semfs_ino_t); 
/* void op_flush (fuse_req_t req, fuse_ino_t ino, */
/*                struct fuse_file_info *fi); */
/* void op_write (fuse_req_t req, fuse_ino_t ino, const char *buf, */
/*                size_t size, off_t off, struct fuse_file_info *fi); */
/* void op_read (fuse_req_t req, fuse_ino_t ino, size_t size, */
/*               off_t off, struct fuse_file_info *fi); */
/* void op_create (fuse_req_t req, fuse_ino_t parent, const char *name, */
/*                 mode_t mode, struct fuse_file_info *fi); */
/* void op_unlink (fuse_req_t req, fuse_ino_t parent, const char *name); */
/* void op_link (fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent, */
/*               const char *newname); */
/* void op_move (fuse_req_t req, fuse_ino_t parent, const char *name, */
/*                      fuse_ino_t newparent, const char *newname); */
/* void op_rmdir (fuse_req_t req, fuse_ino_t parent, const char *name); */
/* void op_statfs (fuse_req_t req); */
/* void op_release (fuse_req_t req, fuse_ino_t ino, */
/*                  struct fuse_file_info *fi); */
/* void op_readdir (fuse_req_t req, fuse_ino_t ino, size_t size, */
/*                  off_t off, struct fuse_file_info *fi); */
/* void op_mkdir (fuse_req_t req, fuse_ino_t parent, const char *name, */
/*                mode_t mode); */
/* void op_open (fuse_req_t req, fuse_ino_t ino, */
/*               struct fuse_file_info *fi); */
/* void op_setattr (fuse_req_t req, fuse_ino_t ino, struct stat *attr, */
/*                  int to_set, struct fuse_file_info *fi); */
/* void op_getattr (fuse_req_t req, fuse_ino_t ino, */
/*                  struct fuse_file_info *fi); */
/* void op_lookup (fuse_req_t req, fuse_ino_t parent, const char *name); */
/* void op_init (void *userdata); */
/* void op_destroy (void *userdata); */

#endif				/* !SEMFS_H_ */
