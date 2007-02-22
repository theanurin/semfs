/*
 *  ext2fs.c - implementation of ext2 using libext2fs
 *		based on debugfs and fuse-ext2
 *
 *  Copyright (C) 2006 Amit Gud <gud@ksu.edu>
 *
 *  This program can be distributed under the terms of the GNU GPL v2.
 *  See the file COPYING.
 */

#include "semfs.h"

#ifdef DEBUG
#define ext2_err(rc, fmt, arg...) \
	do { \
		com_err(__FUNCTION__, rc, fmt,  ## arg); \
	} while(0)
#else
	#define ext2_err(rc, fmt, arg...) \
		do { } while(0)
#endif


void fill_statbuf(ext2_ino_t ino, struct ext2_inode *inode, struct stat *st)
{
	/* st_dev */
	st->st_ino = ino;
	st->st_mode = inode->i_mode;
	st->st_nlink = inode->i_links_count;
	st->st_uid = inode->i_uid;	/* add in uid_high */
	st->st_gid = inode->i_gid;	/* add in gid_high */
	/* st_rdev */
	st->st_size = semfs_get_file_size(ino);
	st->st_blksize = 4096;		/* FIXME */
	st->st_blocks = inode->i_blocks;
	st->st_atime = inode->i_atime;
	st->st_mtime = inode->i_mtime;
	st->st_ctime = inode->i_ctime;
}

int read_inode(ext2_filsys fs, ext2_ino_t ino, struct ext2_inode *inode)
{
	int rc;
	rc = ext2fs_read_inode(fs, ino, inode);
	if(rc) {
		ext2_err(rc, "while reading inode %u", ino);
		return 1;
	}
	return 0;
}

int write_inode(ext2_filsys fs, ext2_ino_t ino, struct ext2_inode * inode)
{
	int rc;

	rc = ext2fs_write_inode(fs, ino, inode);
	if (rc) {
		ext2_err(rc, "while writing inode %u", ino);
		return 1;
	}
	return 0;
}

/*
 * Given a mode, return the ext2 file type
 */
int ext2_file_type(unsigned int mode)
{
	if(LINUX_S_ISREG(mode))
		return EXT2_FT_REG_FILE;

	if(LINUX_S_ISDIR(mode))
		return EXT2_FT_DIR;

	if(LINUX_S_ISCHR(mode))
		return EXT2_FT_CHRDEV;

	if(LINUX_S_ISBLK(mode))
		return EXT2_FT_BLKDEV;

	if(LINUX_S_ISLNK(mode))
		return EXT2_FT_SYMLINK;

	if(LINUX_S_ISFIFO(mode))
		return EXT2_FT_FIFO;

	if(LINUX_S_ISSOCK(mode))
		return EXT2_FT_SOCK;

	return 0;
}

int get_ino_by_name(ext2_filsys fs, ext2_ino_t parent, const char *name,
				ext2_ino_t *ino)
{
	return ext2fs_namei(fs, EXT2_ROOT_INO, parent, name, ino);
}

int do_lookup(ext2_filsys fs, ext2_ino_t parent, const char *name,
			ext2_ino_t *ino, struct ext2_inode *inode)
{
	errcode_t rc;

	rc = ext2fs_lookup(fs, parent, name, strlen(name), NULL, ino);
	if(rc) {
		ext2_err(rc, "while looking up %s#", name);
		return ENOENT;
	}

	rc = read_inode(fs, *ino, inode);
	if(rc) {
		ext2_err(rc, "while reading inode %u", *ino);
		return EIO;
	}

	return 0;
}

/* iterate the directory @ino and fillup the buffer @buf */
int do_dir_iterate(ext2_filsys fs, ext2_ino_t ino, int flags,
		int (*func)(struct ext2_dir_entry *, int, int, char *, void *), void *buf)
{
	errcode_t rc;
	rc = ext2fs_dir_iterate(fs, ino, flags, NULL, func, buf);
	if(rc) {
		ext2_err(rc, "while iterating inode %u", ino);
		return 1;
	}

	return 0;
}

ext2_file_t do_open(ext2_filsys fs, ext2_ino_t ino, int flags)
{
	ext2_file_t efile;
	errcode_t rc;

	rc = ext2fs_file_open(fs, ino, flags, &efile);
	printf("ino=%u, rc=%u, efile=%u, flags=%u\n", ino, rc, efile, flags);
	if(rc) {
		ext2_err(rc, "while opening inode %u", ino);
		return NULL;
	}

	return efile;
}

int do_create(ext2_filsys fs, ext2_ino_t parent, const char *name, mode_t mode,
			ext2_ino_t *ino, struct ext2_inode *inode)
{
	errcode_t rc;

	/* create a brand new inode */
	rc = ext2fs_new_inode(fs, parent, mode, 0, ino);
	printf("parent=%u, name=%s, mode=%u, rc=%u, ino=%u", parent, name, mode, rc, *ino);
	if(rc) {
		if(EXT2_ET_INODE_ALLOC_FAIL)
			rc = ENOSPC;
		return rc;
	}

	/* link it in the directory */
	rc = do_link(fs, parent, name, *ino, EXT2_FT_REG_FILE);
	if(rc) {
		ext2_err(rc, "while linking %s", name);
		return rc;
	}

	/* double-check if the inode is already set */
	if(ext2fs_test_inode_bitmap(fs->inode_map, *ino))
		ext2_err(0, "Warning: inode already set");

	/* update allocation statistics  */
	ext2fs_inode_alloc_stats2(fs, *ino, +1, 0);

	/* ready the inode for writing */
	memset(inode, 0, sizeof(struct ext2_inode));
	inode->i_mode = LINUX_S_IFREG | 0644;
	inode->i_atime = inode->i_ctime = inode->i_mtime = time(NULL);
	inode->i_links_count = 1;
	inode->i_size = 0;

	/* XXX: needed? */
	ext2fs_mark_inode_bitmap(fs->inode_map, *ino);
	ext2fs_mark_ib_dirty(fs);
	ext2fs_mark_bb_dirty(fs);
	ext2fs_mark_super_dirty(fs);

	/* write the inode */
	rc = ext2fs_write_new_inode(fs, *ino, inode);
	if(rc) {
		ext2_err(rc, "while creating inode %u", *ino);
		return EIO;
	}

	return 0;
}

static int release_blocks_proc(ext2_filsys fs, blk_t *blocknr, int blockcnt EXT2FS_ATTR((unused)),
				void *private EXT2FS_ATTR((unused)))
{
	blk_t block;

	block = *blocknr;
	ext2fs_block_alloc_stats(fs, block, -1);
	return 0;
}

void kill_file_by_inode(ext2_filsys fs, ext2_ino_t inode)
{
	struct ext2_inode inode_buf;

	if(read_inode(fs, inode, &inode_buf))
		return;

	inode_buf.i_dtime = time(NULL);
	if(write_inode(fs, inode, &inode_buf))
		return;

	if(!ext2fs_inode_has_valid_blocks(&inode_buf))
		return;

	ext2fs_block_iterate(fs, inode, 0, NULL, release_blocks_proc, NULL);
	ext2fs_inode_alloc_stats2(fs, inode, -1, LINUX_S_ISDIR(inode_buf.i_mode));
	ext2fs_unmark_inode_bitmap(fs->inode_map, inode);
	ext2fs_mark_ib_dirty(fs);
	ext2fs_mark_super_dirty(fs);
}

int do_link(ext2_filsys fs, ext2_ino_t parent, const char *name, ext2_ino_t ino,
			int filetype)
{
	errcode_t rc;

	rc = ext2fs_link(fs, parent, name, ino, filetype);
	if(rc == EXT2_ET_DIR_NO_SPACE) {
		rc = ext2fs_expand_dir(fs, parent);
		if(rc) {
			ext2_err(rc, "while expanding directory");
			return EIO;
		}
		rc = ext2fs_link(fs, parent, name, ino, EXT2_FT_REG_FILE);
	}
	return rc;
}

/* 
 * do_unlink: used for deleting files as well as while renaming the files
 * @links has the number (<= 0) to be added to the inode link count.
 */
int do_unlink(ext2_filsys fs, ext2_ino_t parent, const char *name, int links)
{
	errcode_t rc;
	ext2_ino_t ino;
	struct ext2_inode inode;

	rc = ext2fs_namei(fs, EXT2_ROOT_INO, parent, name, &ino);
	if(rc) {
		ext2_err(rc, "while trying to resolve filename");
		return ENOENT;
	}

	if(read_inode(fs, ino, &inode)) {
		ext2_err(rc, "while reading ino %u", ino);
		return EIO;
	}

	if(LINUX_S_ISDIR(inode.i_mode)) {
		ext2_err(0, "file is a directory");
		return ENOTDIR;
	}

	/* Assumption: @links is strictly < 1 */
	if(inode.i_links_count > 0)
		inode.i_links_count += links;
	
	if(write_inode(fs, ino, &inode)) {
		ext2_err(rc, "while writing ino %u", ino);
		return EIO;
	}

	rc = ext2fs_unlink(fs, parent, name, 0, 0);
	if(rc)
		ext2_err(rc, "while unlinking %s", name);

	if(inode.i_links_count < 1)
		kill_file_by_inode(fs, ino);

	return 0;
}

int do_read(struct ext2_file *fh, ext2_ino_t ino, size_t size, off_t off,
			char *buf, unsigned int *bytes)
{
	errcode_t rc;
	__u64 pos;

	if(!buf)
		return ENOMEM;

	rc = ext2fs_file_llseek(fh, off, SEEK_SET, &pos);
	if(rc) {
		ext2_err(rc, "while seeking %u by %u", ino, off);
		return EINVAL;
	}

	rc = ext2fs_file_read(fh, buf, size, bytes);
	if(rc) {
		ext2_err(rc, "while reading file %u", ino);
		return(rc);
	}

	return 0;
}

int do_write(struct ext2_file *fh, ext2_ino_t ino, const char *buf, size_t size,
			off_t off, unsigned int *bytes)
{
	errcode_t rc;
	__u64 pos;
	struct ext2_inode inode;
	rc = ext2fs_file_llseek(fh, off, SEEK_SET, &pos);
	if(rc) {
		ext2_err(rc, "while seeking %u by %u", ino, off);
		return EINVAL;
	}

	rc = ext2fs_file_write(fh, buf, size, bytes);
	if(rc) {
		ext2_err(rc, "while writing inode %u", ino);
		return EIO;
	}

	rc = read_inode(fh->fs, ino, &inode);
	if(rc)
		ext2_err(rc, "while reading inode %u", ino);
	else if(off + *bytes > inode.i_size)
          rc = ext2fs_file_set_size(fh, off + *bytes);
	return rc;
}

int do_file_flush(struct ext2_file *fh)
{
	errcode_t rc;
	rc = ext2fs_file_flush(fh);
	return rc;
}

int do_file_close(struct ext2_file *fh)
{
	int rc;
	rc = ext2fs_file_close(fh);
	return rc;	
}

int do_mkdir(ext2_filsys fs, ext2_ino_t parent, const char *name, mode_t mode,
			ext2_ino_t *ino)
{
	errcode_t rc;

try_again:
	rc = ext2fs_mkdir(fs, parent, 0, name);
	if(rc == EXT2_ET_DIR_NO_SPACE) {
		rc = ext2fs_expand_dir(fs, parent);
		if (rc) {
			ext2_err(rc, "while expanding directory");
			return ENOSPC;
		}
		goto try_again;
	}

	if(rc) {
		if(EXT2_ET_INODE_ALLOC_FAIL)
			rc = ENOSPC;
		ext2_err(rc, "while creating %s, rc=%u", name, rc);
		return rc;
	}

	rc = ext2fs_lookup(fs, parent, name, strlen(name), NULL, ino);
	if(rc) {
		ext2_err(rc, "while looking up %s", name);
		return ENOENT;
	}

	ext2fs_mark_ib_dirty(fs);
	ext2fs_mark_bb_dirty(fs);
	ext2fs_mark_super_dirty(fs);
	return 0;
}

size_t fs_size(ext2_filsys fs)
{
	if(!fs)
		return 0;

	return fs->super->s_free_blocks_count;
}

int do_statvfs(ext2_filsys fs, struct statvfs *stbuf)
{
	unsigned long overhead;

	if(!fs || !stbuf)
		return 1;

	overhead = fs->super->s_first_data_block;

	stbuf->f_bsize = EXT2_BLOCK_SIZE(fs->super);
	stbuf->f_frsize = EXT2_FRAG_SIZE(fs->super);
	stbuf->f_blocks = fs->super->s_blocks_count - overhead;
	stbuf->f_bavail = fs->super->s_free_blocks_count;
	stbuf->f_bfree = fs->super->s_r_blocks_count + stbuf->f_bavail;
	stbuf->f_files = fs->super->s_inodes_count;
	stbuf->f_ffree = stbuf->f_favail = fs->super->s_free_inodes_count;
	stbuf->f_fsid = 0;
	stbuf->f_flag = 0;
	stbuf->f_namemax = 256;

	return 0;
}

struct rd_struct {
	ext2_ino_t parent;
	int empty;
};

static int rmdir_proc(ext2_ino_t dir EXT2FS_ATTR((unused)), int entry EXT2FS_ATTR((unused)),
			struct ext2_dir_entry *dirent, int offset EXT2FS_ATTR((unused)),
			int blocksize EXT2FS_ATTR((unused)), char *buf EXT2FS_ATTR((unused)),
			void *private)
{
	struct rd_struct *rds = (struct rd_struct *) private;

	if(dirent->inode == 0)
		return 0;
	if(((dirent->name_len & 0xFF) == 1) && (dirent->name[0] == '.'))
		return 0;
	if(((dirent->name_len & 0xFF) == 2) && (dirent->name[0] == '.') &&
	    (dirent->name[1] == '.')) {
		rds->parent = dirent->inode;
		return 0;
	}
	rds->empty = 0;
	return 0;
}

int do_rmdir(ext2_filsys fs, ext2_ino_t parent, const char *name)
{
	errcode_t rc;
        ext2_ino_t ino;
        struct ext2_inode inode;
        struct rd_struct rds;

	rc = ext2fs_namei(fs, EXT2_ROOT_INO, parent, name, &ino);
	if(rc) {
		ext2_err(rc, "while trying to resolve filename");
		return ENOENT;
	}

	if(read_inode(fs, ino, &inode)) {
		ext2_err(rc, "while reading ino %u", ino);
		return EIO;
	}

	if(!LINUX_S_ISDIR(inode.i_mode)) {
		ext2_err(0, "file is not a directory");
		return ENOTDIR;
	}

	rds.parent = 0;
	rds.empty = 1;

	rc = ext2fs_dir_iterate2(fs, ino, 0, 0, rmdir_proc, &rds);
	if(rc) {
		ext2_err(rc, "while iterating over directory");
		return EIO;
	}

	if(rds.empty == 0) {
		ext2_err(0, "directory not empty");
		return ENOTEMPTY;
	}

	inode.i_links_count = 0;
	if(write_inode(fs, ino, &inode)) {
		ext2_err(rc, "while reading ino %u", ino);
		return EIO;
	}

	rc = ext2fs_unlink(fs, parent, name, 0, 0);
	if(rc) {
		ext2_err(rc, "while unlinking %s", name);
		return EIO;
	}

	kill_file_by_inode(fs, ino);

	if(rds.parent) {
		if(read_inode(fs, rds.parent, &inode))
			return EIO;
		if(inode.i_links_count > 1)
			inode.i_links_count--;
		if(write_inode(fs, rds.parent, &inode))
			return EIO;
	}
	return 0;
}
