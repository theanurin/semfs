/* TODO: */
/* * Provide a way for turning off verbose error reporting */
/* * Remove all printf's */
/* * Get Block and Fragment sizes dynamically */
/* * Get Symlinks working! */
/* * Check return Values for notification */
/* * Implement Transactions */
/* * Use Valgrind to check for memory leaks */

/* Fixed: */
/* * Fix Write error on updating files */
/* * Refactor Code in XML generation */
/* * IMPORTANT: Unable to notify full path! */
/* * Implement Notification for events without parent INO */

/* Problems: */


#define FUSE_USE_VERSION 25

#include <fuse_lowlevel.h>
#include <fuse.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "semfs.h"
#include "xml.h"

semfs_t semfs;

int connect_to_daemon()
{
     struct sockaddr_in sin;
     semfs.sock = socket(AF_INET, SOCK_STREAM, 0);
     bzero ((char *)&sin, sizeof(sin));
     sin.sin_family = AF_INET; /* Current uses INET Sockets. */
/* TODO: Consider using AF_UNIX sockets. No TCP stack traversal */
     sin.sin_addr.s_addr = inet_addr("127.0.0.1"); /* By Default Connect to
                                                    * localhost. */
/* TODO: Indexing Daemon need not be on localhost. But also Consider that
 * Plugin might need to read files. Hence lookup by plugin should also not
 * be local.
 */
     sin.sin_port = htons(40000); /* Daemon always listens on port 40000 */
     if ((connect (semfs.sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)) {
          fprintf(stderr, "Could not connect to service\n");
          return 0;
     }
     return 1;
}

/* Function No longer required. Used to check if Space is still available */
/* int semfs_space_ok (int resource) */
/* { */
/*     struct statvfs stbuf; */
/*     int ret = 0; */

/*     do_statvfs (semfs.fs, &stbuf); */
/*     printf("chunk=%d, avail=%Ld, blocks=%Lu, fill=%f, limit=%f, f_inodes=%u, f_blocks=%u\n", */
/*            semfs.id, stbuf.f_bavail, stbuf.f_blocks, */
/*            (stbuf.f_blocks * (float) (1.00f - cfill_limit)), cfill_limit, */
/*            free_inode_count (semfs.fs), free_blocks_count (semfs.fs)); */
/*     if (resource & ALLOC_INODES) */
/*          ret = free_inode_count (semfs.fs) > 0; */
/*     if (resource & ALLOC_BLOCKS) { */
/*          if (stbuf.f_bavail > (stbuf.f_blocks * (float) (1.00f - cfill_limit))) */
/*               ret = 1; */
/*          else */
/*               ret = 0; */
/*     } */
/*     return ret; */
/* } */

int reply_buf_limited (fuse_req_t req, const char *buf, size_t bufsize,
                       off_t off, size_t maxsize)
{
    if (off < bufsize)
         return fuse_reply_buf (req, buf + off,
                                min (bufsize - off, maxsize));
    else
         return fuse_reply_buf (req, NULL, 0);
}

void dirbuf_add (struct semfs_dirbuf *dbuf, const char *name,
                 ext2_ino_t ino)
{
    struct dirbuf *b = &dbuf->b;
    struct stat stbuf;
    size_t oldsize = b->size;

    /* avoid multiple occurance of . and .. */
/*     if (dbuf->following_cnode) /\* If a  */
/*          if (!strncmp (name, ".", 1) || !strncmp (name, "..", 2)) */
/*               return; */

    b->size += fuse_dirent_size (strlen (name));
    b->p = (char *) realloc (b->p, b->size);

    memset (&stbuf, 0, sizeof (stbuf));  /* Copy the entry into a buffer */
    stbuf.st_ino = ino;
    fuse_add_dirent (b->p + oldsize, name, &stbuf, b->size); /* Adds the buffer
                                                              * into the dirent
                                                              * buffer */
}

int walk_dir (struct ext2_dir_entry *de, int offset, int blocksize,
              char *buf, void *priv_data)
{
    struct semfs_dirbuf *b = (struct semfs_dirbuf *) priv_data;
    char *s;
    int name_len = de->name_len & 0xFF;

    s = malloc (name_len + 1);
    if (!s)
         return -ENOMEM;

    memcpy (s, de->name, name_len);
    s[name_len] = '\0';

    dirbuf_add (b, s, de->inode);
    free (s);
    return 0;
}

size_t semfs_get_file_size (semfs_ino_t ino)
{
     semfs_inode_t inode;
    int rc;
    if (ino < SEMFS_ROOT_INO) { /* Invalid Inode number */
         fprintf (stderr, "ino (%d) < ROOT INO(%d)\n", ino, SEMFS_ROOT_INO);
         return 0;
    }
    rc = read_inode (semfs.fs, SEMFS_INO(ino), &inode);
    if (rc) {
         fprintf (stderr, "error reading inode %d\n", ino);
         return 0;
    }
    return inode.i_size;
}

static void op_symlink (fuse_req_t req, const char *link, fuse_ino_t parent,
                    const char *name)
{
     /* To be Filled in */
}

static void op_readlink (fuse_req_t req, fuse_ino_t ino)
{
     /* Adapted from `errcode_t follow_link' in namei.c */
     struct ext2_inode ei;
     int retval;
     char *pathname;
     char *buffer = 0;
/* TODO: move this part into ext2fs.c */
     retval = ext2fs_read_inode (semfs.fs, SEMFS_INO(ino), &ei);
     if (retval || !LINUX_S_ISLNK (ei.i_mode)) {
          fuse_reply_err (req, EIO);
          return;
     }
     
     if (ext2fs_inode_data_blocks(semfs.fs,&ei)) {
          retval = ext2fs_get_mem(semfs.fs->blocksize, &buffer);
          if (retval) {
               fuse_reply_err (req, ENOMEM);
               return;
          }
          retval = io_channel_read_blk(semfs.fs->io, ei.i_block[0], 1, buffer);
          if (retval) {
               ext2fs_free_mem(&buffer);
               fuse_reply_err (req, EIO);
               return;
          }
          pathname = buffer;
     }
     else
          pathname = (char *)&(ei.i_block[0]);
     fuse_reply_readlink (req, pathname);
}

static void op_flush (fuse_req_t req, fuse_ino_t ino,
		      struct fuse_file_info *fi)
{
    int rc = 0;
    semfs_file_t *efile = SEMFS_FILE (fi->fh);

    if (!efile || !efile->file) {
         com_err ("semfs", EIO, "BUG: efile null, ino=%u\n", ino);
         fuse_reply_err (req, EIO);
         return;
    }
    rc = do_file_flush (SEMFS_FH (efile));
    if (rc)
         com_err ("semfs", rc, "error flushing file\n");
    fuse_reply_err (req, rc);
}

static void op_write (fuse_req_t req, fuse_ino_t ino, const char *buf,
                      size_t size, off_t off, struct fuse_file_info *fi)
{
    int rc;
    unsigned int bytes;
    semfs_inode_t *inode = NULL;
    semfs_file_t *efile = SEMFS_FILE (fi->fh);
    int fsize = 0;

    printf ("ino=%u, size=%u, off=%u\n", ino, size, off);
    if (!efile || !efile->file) {
         com_err ("semfs", EIO, "BUG! efile=%p *efile=%p, file=%p\n", efile,
                  *efile, efile ? efile->file : 0);
         rc = EIO;
         fuse_reply_err (req, rc);
    }

    inode = &SEMFS_FH (efile)->inode;
    fsize = semfs_get_file_size (SEMFS_INO (ino));
    rc = do_write (SEMFS_FH (efile), SEMFS_INO (efile->ino), buf, size,
                   off, &bytes);
    if (rc)
         fuse_reply_err (req, rc);

    do_file_flush (SEMFS_FH (efile)); /* flush to file as soon as you
                                       * write. problems arise due to user land
                                       * execution */
    fuse_reply_write (req, bytes);
    if (!notify_inode_event("write", SEMFS_INO(ino)))
         fprintf(stderr, "Unable to notify write event for %u\n", SEMFS_INO(ino));
}

static void op_read (fuse_req_t req, fuse_ino_t ino, size_t size,
                     off_t off, struct fuse_file_info *fi)
{
    int rc;
    void *buf;
    semfs_file_t *efile = SEMFS_FILE (fi->fh);
    semfs_inode_t *inode;
    size_t fsize = 0;
    unsigned int temp = 0;

    printf ("ino=%u, size=%u, off=%u\n", ino, size, off);
    if (!efile || !efile->file) {
         com_err ("semfs", EIO, "BUG! efile=%p, file=%p\n", efile,
                  efile ? efile->file : 0);
         rc = EIO;
         fuse_reply_err (req, rc);
    }

    inode = &SEMFS_FH (efile)->inode;
    fsize = inode->i_size;

    buf = malloc (min (size, inode->i_size)); /* Memory to put the data is
                                               * created here. This Buffer is
                                               * automatically deallocated by
                                               * FUSE */
    if (!buf) {
         com_err ("semfs", ENOMEM, "Out of Memory\n");
         fuse_reply_err (req, ENOMEM);
    }

    rc = do_read (SEMFS_FH (efile), SEMFS_INO (efile->ino),
                  min (size, inode->i_size), off, buf, &temp);
    if (rc) {
         fuse_reply_err (req, EIO);
         free (buf);
    }

    fuse_reply_buf (req, buf, min (temp, size));
    free (buf);
}

static void op_create (fuse_req_t req, fuse_ino_t parent, const char *name,
                       mode_t mode, struct fuse_file_info *fi)
{
    int rc;
    semfs_ino_t ino;
    semfs_file_t *efile;
    semfs_inode_t inode;
    struct fuse_entry_param fep;
    printf ("name=%s, parent=%u, mode=%u\n", name, parent, mode);
    rc = do_create (semfs.fs, SEMFS_INO (parent), name, mode, &ino,
                    &inode);
    if (rc) {
         com_err ("semfs", EIO, "Could not create file\n");
         fuse_reply_err (req, EIO);
    }

    efile = (semfs_file_t *) malloc (sizeof (struct semfs_file_s));
    /* Structure for maintaining the file properties */
    if (!efile) {
         com_err ("semfs", ENOMEM, "Out of Memory\n");
         fuse_reply_err (req, ENOMEM);
    }

    efile->file = (unsigned long) do_open (semfs.fs, SEMFS_INO (ino),
                                           fi->flags & ~O_CREAT);
    if (!efile->file) {
         com_err ("semfs", EIO, "Could not create file\n");
         fuse_reply_err (req, EIO);
    }

	/* initialize file handle */
	efile->next = efile->prev = NULL;
	efile->size = 0;
	efile->ino = ino;

    printf ("efile=%p *efile=%p, file=%lu\n", efile, *efile, efile->file);
    fi->fh = (unsigned long) efile;
    fep.ino = ino;
    fill_statbuf (ino, &inode, &fep.attr);
    fuse_reply_create (req, &fep, fi);
    // Notify here
    if (!notify_event("create", SEMFS_INO(parent), SEMFS_INO(ino)))
         printf ("Could not notify creation of Inode %d/%d\n",
                 SEMFS_INO(parent), SEMFS_INO(ino));
}

static void op_unlink (fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int ret;
    semfs_inode_t cnode;
    semfs_ino_t ino;

    ret =	ext2fs_namei (semfs.fs, SEMFS_ROOT_INO, SEMFS_INO (parent), name,
                        &ino);
    if(!notify_event ("unlink", SEMFS_INO(parent), SEMFS_INO(ino)))
         printf ("Could not notify unlink of Inode %d/%d\n",
                 SEMFS_INO(parent), SEMFS_INO(ino));
    
    ret = do_unlink (semfs.fs, SEMFS_INO (parent), name, -1);
    if (ret) {
         com_err ("semfs", ret, "error unlinking %s", name);
         fuse_reply_err (req, ret);
    }

/*     ret = read_inode (semfs.fs, SEMFS_INO(ino), &cnode); */
/*     if (ret) */
/*          com_err ("semfs", ret, "error reading Inode %u", ino); */
    fuse_reply_err (req, ret);
}

static void op_link (fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
                     const char *newname)
{
    int rc;
    struct fuse_entry_param fe;
    semfs_inode_t inode;
    printf ("Inside Link\nINO: %u, Parent: %u, Name: %s\n", ino, newparent, newname);
    rc = read_inode (semfs.fs, SEMFS_INO(ino), &inode);
    if (rc) {
         com_err ("semfs", rc, "error reading Inode %u", ino);
         fuse_reply_err (req, EIO);
    }

    rc = do_link (semfs.fs, SEMFS_INO (newparent), newname,
                  SEMFS_INO (ino), ext2_file_type (inode.i_mode));
    if (rc) {
         com_err ("semfs", rc, "error linking %s", newname);
         fuse_reply_err (req, EIO);
    }

    inode.i_links_count++;
    rc = write_inode (semfs.fs, ino, &inode);
    if (rc) {
         com_err ("semfs", rc, "error writing Inode %u", ino);
         fuse_reply_err (req, EIO);
    }

    fe.ino = ino;
    fe.generation = inode.i_generation;
    fill_statbuf (ino, &inode, &fe.attr);
    fe.attr_timeout = 2.0;
    fe.entry_timeout = 2.0;

    fuse_reply_entry (req, &fe);
    // Notify here
}

static void op_move (fuse_req_t req, fuse_ino_t parent, const char *name,
                     fuse_ino_t newparent, const char *newname)
{
    int ret;
    semfs_ino_t ino;
    semfs_inode_t inode;

    /* determine file-type */
    ret = get_ino_by_name (semfs.fs, SEMFS_INO (parent), name, &ino);
    if (ret) {
         fuse_reply_err (req, ENOENT);
         return;
    }

    ret = read_inode (semfs.fs, SEMFS_INO(ino), &inode);
    if (ret) {
         fuse_reply_err (req, EIO);
         return;
    }

    /* first link, then unlink */
    ret = do_link (semfs.fs, SEMFS_INO (newparent), newname, SEMFS_INO (ino),
                   ext2_file_type (inode.i_mode));
    if (ret) {
         fuse_reply_err (req, ENOENT);
         return;
    }

    /* 0 - don't decrement the link count */
    ret = do_unlink (semfs.fs, SEMFS_INO (parent), name, 0);
    fuse_reply_err (req, ret);
}

static void op_rmdir (fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int ret;
    semfs_ino_t ino;

    ret =
	ext2fs_namei (semfs.fs, SEMFS_ROOT_INO, SEMFS_INO (parent), name,
		      &ino);
    if (ret) {
	com_err ("semfs", ret, "directory not found\n");
	fuse_reply_err (req, EIO);
    }
    // Notify here
    if(!notify_event ("rmdir", SEMFS_INO(parent), SEMFS_INO(ino)))
         printf ("Could not notify rmdir of Inode %d/%d\n",
                 SEMFS_INO(parent), SEMFS_INO(ino));
    ret = do_rmdir (semfs.fs, SEMFS_INO (parent), name);
    fuse_reply_err (req, ret);
}

static void op_statfs (fuse_req_t req)
{
    struct statvfs stbuf;
    struct statvfs tstat;
    memset (&stbuf, 0, sizeof (stbuf));

    if (!do_statvfs (semfs.fs, &tstat)) {
         /* Will not come here only if FS not available. Which should not occur */
         stbuf.f_blocks += tstat.f_blocks;
         stbuf.f_bavail += tstat.f_bavail;
         stbuf.f_bfree += tstat.f_bfree;
         stbuf.f_files += tstat.f_files;
         stbuf.f_ffree += tstat.f_ffree;
    }
    stbuf.f_bsize = 4096;	/* FIXME */
    stbuf.f_frsize = 4096;	/* FIXME */
    stbuf.f_fsid = 0;
    stbuf.f_flag = 0;
    stbuf.f_namemax = 256;
    fuse_reply_statfs (req, &stbuf);
}

static void op_release (fuse_req_t req, fuse_ino_t ino,
                        struct fuse_file_info *fi)
{
    int rc = 0;
    semfs_file_t *efile = SEMFS_FILE (fi->fh);

    if (!efile || !efile->file) {
         com_err ("semfs", EIO, "BUG: efile null, ino=%u\n", ino);
         fuse_reply_err (req, EIO);
         return;
    }

    rc = do_file_close (SEMFS_FH (efile));
    if (rc)
         com_err ("semfs", rc, "error closing file\n");
    free (efile);
    fuse_reply_err (req, rc);
}

static void op_readdir (fuse_req_t req, fuse_ino_t ino, size_t size,
                        off_t off, struct fuse_file_info *fi)
{
    struct semfs_dirbuf b;
    semfs_inode_t inode;
    errcode_t rc;

    memset (&b, 0, sizeof (b));
    b.following_cnode = 0;
    rc = do_dir_iterate (semfs.fs, SEMFS_INO(ino), 0, walk_dir, (void *) &b);
    /* Search directory if where it points to the file */
    if (rc) {
         com_err ("semfs", rc, "Error in dir_iterate\n");
         fuse_reply_err (req, EIO);
         free (b.b.p);
    }
    rc = read_inode (semfs.fs, SEMFS_INO(ino), &inode);
    if (rc) {
         com_err ("semfs", rc, "Error in read_inode\n");
         fuse_reply_err (req, EIO);
         free (b.b.p);
    }

    reply_buf_limited (req, b.b.p, b.b.size, off, size);
    free (b.b.p);
}

static void op_mkdir (fuse_req_t req, fuse_ino_t parent, const char *name,
                      mode_t mode)
{
    struct fuse_entry_param fe;
    semfs_ino_t ino;
    semfs_inode_t inode;
    int rc;

    rc = do_mkdir (semfs.fs, SEMFS_INO (parent), name, mode, &ino);
    if (rc) {
         fuse_reply_err (req, rc);
         return;
    }
    rc = read_inode (semfs.fs, SEMFS_INO(ino), &inode);
    if (rc) {
         fuse_reply_err (req, EIO);
         return;
    }
    fe.ino = ino;
    fe.generation = inode.i_generation;
    fill_statbuf (ino, &inode, &fe.attr);
    fe.attr_timeout = 2.0;
    fe.entry_timeout = 2.0;

    fuse_reply_entry (req, &fe);
    // Notify here
    if (!notify_event("mkdir", SEMFS_INO(parent), SEMFS_INO(ino)))
         printf ("Could not notify mkdir of Inode %d/%d\n",
                 SEMFS_INO(parent), SEMFS_INO(ino));
}

static void op_open (fuse_req_t req, fuse_ino_t ino,
                     struct fuse_file_info *fi)
{
    int rc;
    semfs_file_t fh, *efile;

    fh.next = fh.prev = NULL;
    efile = &fh;

    printf ("ino=%u\n", ino);
    efile->next = (semfs_file_t *) malloc (sizeof (semfs_file_t));
    /* Add the instance of the open file into the list of Open instances for
       the driver */
    if (!efile->next) {
         rc = ENOMEM;
         efile = efile->prev;
         free (efile->next);
         fuse_reply_err (req, rc);
    }

    efile->next->prev = efile;
    efile = efile->next;
    efile->next = NULL;
    efile->ino = ino;

    efile->file = (unsigned long) do_open (semfs.fs, SEMFS_INO (ino),
                                           fi->flags);
    if (!efile->file) {
         efile = efile->prev;
         free (efile->next);
         fuse_reply_err (req, EIO);
    }

    fh.next->prev = NULL;
    fi->fh = (unsigned long) fh.next;
    fuse_reply_open (req, fi);
    // Notify here
}

static void op_setattr (fuse_req_t req, fuse_ino_t ino, struct stat *attr,
                        int to_set, struct fuse_file_info *fi)
{
    int rc;
    struct stat stbuf;
    semfs_inode_t inode;
    printf ("ino=%u\n", ino);
    memset (&stbuf, 0, sizeof (stbuf));
    rc = read_inode (semfs.fs, SEMFS_INO(ino), &inode);
    if (rc) {
         printf ("Error in reading in setattr\n");
         fuse_reply_err (req, ENOENT);
         return;
    }

    /* Flag is checked so that an attribute is modified only if it has been
       said so by the user. Else leave it in current state */
    if (to_set & FUSE_SET_ATTR_MODE)
         inode.i_mode = attr->st_mode;

    if (to_set & FUSE_SET_ATTR_UID)
         inode.i_uid = attr->st_uid;

    if (to_set & FUSE_SET_ATTR_GID)
         inode.i_gid = attr->st_gid;

    if (to_set & FUSE_SET_ATTR_SIZE)
         inode.i_size = attr->st_size;

    if (to_set & FUSE_SET_ATTR_ATIME)
         inode.i_atime = attr->st_atime;

    if (to_set & FUSE_SET_ATTR_MTIME)
         inode.i_mtime = attr->st_mtime;

    inode.i_mtime = time (NULL);     /* modification time has to be changed no
                                      * matter what */
    rc = write_inode (semfs.fs, ino, &inode);
    if (rc) {
         com_err ("semfs", rc, "Error in setting in setattr for ino %d\n",
                  ino);
         fuse_reply_err (req, EIO);
         return;
    }

    fill_statbuf (SEMFS_INO (ino), &inode, &stbuf);
    fuse_reply_attr (req, &stbuf, 1.0);
    // Notify here
}


static void op_getattr (fuse_req_t req, fuse_ino_t ino,
                        struct fuse_file_info *fi)
{
    int rc;
    struct stat stbuf;
    semfs_inode_t inode;
    memset (&stbuf, 0, sizeof (stbuf));
    rc = read_inode (semfs.fs, SEMFS_INO(ino), &inode);
    if (rc) {
         com_err ("semfs", rc, "Error in getattr for ino: %d\n", ino);
         fuse_reply_err (req, ENOENT);
         return;
    }

    fill_statbuf (SEMFS_INO (ino), &inode, &stbuf);
    fuse_reply_attr (req, &stbuf, 1.0);
}

static void op_lookup (fuse_req_t req, fuse_ino_t parent, const char *name)
{
    struct fuse_entry_param fe;
    semfs_inode_t inode;
    semfs_ino_t ino;
    int rc;
    printf ("Looking for name: %s\n", name);
    if (!strncmp (name, "(null)", 6)) {
         fuse_reply_err (req, ENOENT);
         return;
    }
    rc = do_lookup (semfs.fs, SEMFS_INO (parent), name, &ino, &inode);
    if (rc) {
/* TODO: Code for querying the daemon should come here */
         fuse_reply_err (req, ENOENT);
         return;
    }
    fe.ino = ino;
    fe.generation = inode.i_generation;
    fill_statbuf (SEMFS_INO (ino), &inode, &fe.attr);
    fe.attr_timeout = 2.0;
    fe.entry_timeout = 2.0;
    fuse_reply_entry (req, &fe);
}

static void op_init (void *userdata)
{
    errcode_t ret;
    int i;
    char uuid[17];
    ret = ext2fs_open (semfs.device, EXT2_FLAG_RW | EXT2_FLAG_JOURNAL_DEV_OK, 0,
                       0, unix_io_manager, &semfs.fs);
    if (ret) {
         com_err ("semfs", ret, "while trying to open %s", semfs.device);
         ext2fs_close (semfs.fs);
         exit (1);
    }
    printf ("Device has been opened on %d\n", semfs.fs);
    ret = ext2fs_read_inode_bitmap (semfs.fs);
    if (ret) {
         com_err (semfs.device, ret, "while reading inode bitmap");
         ext2fs_close (semfs.fs);
         exit (1);
    }

    ret = ext2fs_read_block_bitmap (semfs.fs);
    if (ret) {
         com_err (semfs.device, ret, "while reading block bitmap");
         ext2fs_close (semfs.fs);
         exit (1);
    }
    /* If file is mountable, then connect and see if the daemon is running */
    ret = connect_to_daemon();
    if (!ret) {
         com_err (semfs.device, ret, "while connecting to daemon");
         ext2fs_close (semfs.fs);
         exit (1);
    }
    for (i=0; i<16; i++) {
         printf("%c", semfs.fs->super->s_uuid[i]);
         uuid[i] = semfs.fs->super->s_uuid[i];
    }
    uuid[i] = '\0';
    /* Do and md5sum and convert the non-ascii uuid into ASCII text and send to
     * daemon */    
/* TODO: UUID is currently non-ascii. Do an md5sum and get the hexdigest and
   send it to daemon. Currently, sample value is being sent */
    if(!connection_event ("uuid", semfs.mnt_point, "start")) {
         com_err (semfs.device, ret, "while initialising connection to daemon");
         ext2fs_close (semfs.fs);
         exit(1);
    }
}

static void op_destroy (void *userdata)
{
    errcode_t ret;
    ret = ext2fs_close (semfs.fs);
    if (ret)
         com_err ("semfs", ret, "while trying to close");
    if(!connection_event ("uuid", semfs.mnt_point, "close"))
         com_err (semfs.device, ret, "while initialising connection to daemon");
    printf ("semfs destroyed\n");
}

static struct fuse_lowlevel_ops semfs_ops = {
    .init = op_init,
    .destroy = op_destroy,
    .lookup = op_lookup,
    .forget = NULL,
    .getattr = op_getattr,
    .setattr = op_setattr,
    .readlink = op_readlink,
    .mknod = NULL,
    .mkdir = op_mkdir,
    .rmdir = op_rmdir,
    .symlink = NULL, /* TODO */
    .rename = op_move,
    .link = op_link,
    .unlink = op_unlink,
    .open = op_open,
    .read = op_read,
    .write = op_write,
    .flush = op_flush,
    .release = op_release,
    .fsync = NULL,
    .opendir = op_open,
    .readdir = op_readdir,
    .releasedir = op_release,
    .fsyncdir = NULL,
    .statfs = op_statfs,
    .setxattr = NULL,
    .getxattr = NULL,
    .listxattr = NULL,
    .removexattr = NULL,
    .access = NULL,
    .create = op_create,
};

int main (int argc, char *argv[])
{
/* USAGE: semfs <ext2-formatted-file> <mount-directory> */
    char *mountpoint = NULL;
    int err = -1;
    int fd;
    struct fuse_args args = FUSE_ARGS_INIT (argc-1, argv+1);
    semfs.device = argv[1];
    semfs.mnt_point = argv[2];
    if (fuse_parse_cmdline (&args, &mountpoint, NULL, NULL) != -1 &&
        (fd = fuse_mount (mountpoint, &args)) != -1) {
         struct fuse_session *se;
         se = fuse_lowlevel_new (&args, &semfs_ops, sizeof (semfs_ops),
                                 NULL);
         if (se != NULL) {
              if (fuse_set_signal_handlers (se) != -1) {
                   struct fuse_chan *ch = fuse_kern_chan_new (fd);
                   if (ch != NULL) {
                        fuse_session_add_chan (se, ch);
                        err = fuse_session_loop (se);
                   }
                   fuse_remove_signal_handlers (se);
              }
              fuse_session_destroy (se);
         }
         close (fd);
    }
    fuse_unmount (mountpoint);
    fuse_opt_free_args (&args);

    return err ? 1 : 0;
}
