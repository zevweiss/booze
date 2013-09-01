/*
 * booze: FUSE bindings for bash.
 */

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

#define FUSE_USE_VERSION 26

#include <fuse.h>

#include "builtins.h"
#include "shell.h"
#include "bashgetopt.h"
#include "common.h"
#include "execute_cmd.h"
#include "error.h"

#define __wdname(name, num) __##name##_word_desc##num
#define __wlname(name, num) __##name##_word_list##num

#define __wldecl(name) WORD_LIST* name = &__wlname(name, 0)

#define __wlchunk(tag, num, wd, nxt) \
	WORD_DESC __wdname(tag, num) = { \
		.word = wd, \
		.flags = 0, \
	}; \
	WORD_LIST __wlname(tag, num) = { \
		.next = nxt, \
		.word = &__wdname(tag, num), \
	}

#define WL_SETUP0(name, nxt) \
	__wlchunk(name, 0, NULL, nxt)

#define WL_DECLINIT0(name) \
	WL_SETUP0(name); \
	__wldecl(name)

#define WL_SETUP1(name, f1, a1, nxt) \
	__wlchunk(name, 1, xasprintf(f1, a1), nxt); \
	WL_SETUP0(name, &__wlname(name, 1))

#define WL_DECLINIT1(name, f1, a1) \
	WL_SETUP1(name, f1, a1, NULL); \
	__wldecl(name)

#define WL_SETUP2(name, f1, a1, f2, a2, nxt) \
	__wlchunk(name, 2, xasprintf(f2, a2), nxt); \
	WL_SETUP1(name, f1, a1, &__wlname(name, 2))

#define WL_DECLINIT2(name, f1, a1, f2, a2) \
	WL_SETUP2(name, f1, a1, f2, a2, NULL); \
	__wldecl(name)

#define WL_SETUP3(name, f1, a1, f2, a2, f3, a3, nxt) \
	__wlchunk(name, 3, xasprintf(f3, a3), nxt); \
	WL_SETUP2(name, f1, a1, f2, a2, &__wlname(name, 3))

#define WL_DECLINIT3(name, f1, a1, f2, a2, f3, a3) \
	WL_SETUP3(name, f1, a1, f2, a2, f3, a3, NULL); \
	__wldecl(name)

#define WL_SETUP4(name, f1, a1, f2, a2, f3, a3, f4, a4, nxt) \
	__wlchunk(name, 4, xasprintf(f4, a4), nxt); \
	WL_SETUP3(name, f1, a1, f2, a2, f3, a3, &__wlname(name, 4))

#define WL_DECLINIT4(name, f1, a1, f2, a2, f3, a3, f4, a4) \
	WL_SETUP4(name, f1, a1, f2, a2, f3, a3, f4, a4, NULL); \
	__wldecl(name)

static char* xasprintf(const char* fmt, ...)
{
	char* tmp;
	va_list va;
	int status;

	va_start(va, fmt);
	status = vasprintf(&tmp, fmt, va);
	va_end(va);

	if (status < 0)
		fatal_error("vasprintf() failed\n");

	return tmp;
}

static ssize_t readall(int fd, void* buf, size_t size)
{
	ssize_t totalread, bytesread;

	totalread = 0;
	do {
		bytesread = read(fd, buf + totalread, size - totalread);
		if (bytesread < 0 && errno != EINTR)
			return -errno;
		else if (!bytesread)
			break;
		else
			totalread += bytesread;
	} while (size - totalread > 0);

	return totalread;
}

static ssize_t writeall(int fd, const void* buf, size_t size)
{
	ssize_t totalwritten, byteswritten;

	totalwritten = 0;
	do {
		byteswritten = write(fd, buf + totalwritten, size - totalwritten);
		if (byteswritten < 0 && errno != EINTR)
			return errno == EPIPE ? totalwritten : -errno;
		else if (!byteswritten)
			break;
		else
			totalwritten += byteswritten;
	} while (size - totalwritten > 0);

	return totalwritten;
}

static void free_onstack_wordlist(WORD_LIST* wl)
{
	WORD_LIST* w;

	for (w = wl; w; w = w->next) {
		if (w->word && w->word->word)
			free(w->word->word);
	}
}

static int call_boozefn(const char* handler_name, WORD_LIST* args, const char** output)
{
	int status;
	SHELL_VAR* err;
	SHELL_VAR* out;
	SHELL_VAR* fn;

	if (!handler_name)
		return -ENOSYS;

	fn = find_function(handler_name);

	if (!fn)
		return -ENOSYS;

	unbind_variable("booze_err");
	unbind_variable("booze_out");

	status = execute_shell_function(fn, args);

	free_onstack_wordlist(args);

	if (status) {
		err = find_variable("booze_err");
		if (!err)
			return -EIO;
		else
			return atoi(err->value);
	} else {
		out = find_variable("booze_out");
		if (output)
			*output = out ? out->value : NULL;
		return 0;
	}
}

#define __basic_decl1(name, t1, a1) \
	static int booze_##name(t1 a1)

#define __basic_decl2(name, t1, a1, t2, a2) \
	static int booze_##name(t1 a1, t2 a2)

#define __basic_decl3(name, t1, a1, t2, a2, t3, a3) \
	static int booze_##name(t1 a1, t2 a2, t3 a3)

#define __basic_decl4(name, t1, a1, t2, a2, t3, a3, t4, a4) \
	static int booze_##name(t1 a1, t2 a2, t3 a3, t4 a4)

#define __basic_decl5(name, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5) \
	static int booze_##name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5)

#define __basic_call(name, args) \
	call_boozefn(handlers.name, args, NULL)

#define __basic_body1(name, f1, a1) \
	WL_DECLINIT1(args, f1, a1); \
	return __basic_call(name, args)

#define __basic_body2(name, f1, a1, f2, a2) \
	WL_DECLINIT2(args, f1, a1, f2, a2); \
	return __basic_call(name, args)

#define __basic_body3(name, f1, a1, f2, a2, f3, a3) \
	WL_DECLINIT3(args, f1, a1, f2, a2, f3, a3); \
	return __basic_call(name, args)

#define __basic_body4(name, f1, a1, f2, a2, f3, a3, f4, a4) \
	WL_DECLINIT4(args, f1, a1, f2, a2, f3, a3, f4, a4); \
	return __basic_call(name, args)

#define BASIC1(name, t1, a1, f1) \
	__basic_decl1(name, t1, a1) \
	{ __basic_body1(name, f1, a1); }

#define BASIC2(name, t1, a1, f1, t2, a2, f2) \
	__basic_decl2(name, t1, a1, t2, a2) \
	{ __basic_body2(name, f1, a1, f2, a2); }

#define BASIC3(name, t1, a1, f1, t2, a2, f2, t3, a3, f3) \
	__basic_decl3(name, t1, a1, t2, a2, t3, a3) \
	{ __basic_body3(name, f1, a1, f2, a2, f3, a3); }

/* "ignore-last" variants of BASIC* */
#define BASIC1_IL(name, t1, a1, f1, it, in) \
	__basic_decl2(name, t1, a1, it, in) \
	{ __basic_body1(name, f1, a1); }

#define BASIC2_IL(name, t1, a1, f1, t2, a2, f2, it, in) \
	__basic_decl3(name, t1, a1, t2, a2, it, in) \
	{ __basic_body2(name, f1, a1, f2, a2); }

#define BASIC3_IL(name, t1, a1, f1, t2, a2, f2, t3, a3, f3, it, in) \
	__basic_decl4(name, t1, a1, t2, a2, t3, a3, it, in) \
	{ __basic_body3(name, f1, a1, f2, a2, f3, a3); }

#define BASIC4_IL(name, t1, a1, f1, t2, a2, f2, t3, a3, f3, t4, a4, f4, it, in) \
	__basic_decl5(name, t1, a1, t2, a2, t3, a3, t4, a4, it, in) \
	{ __basic_body4(name, f1, a1, f2, a2, f3, a3, f4, a4); }

static struct {
	char* getattr;
	char* access;
	char* readlink;
	char* readdir;
	char* mknod;
	char* mkdir;
	char* unlink;
	char* rmdir;
	char* symlink;
	char* rename;
	char* link;
	char* chmod;
	char* chown;
	char* truncate;
	char* utimens;
	char* open;
	char* read;
	char* write;
	char* statfs;
	char* release;
	char* fsync;
	char* fallocate;
	char* setxattr;
	char* getxattr;
	char* listxattr;
	char* removexattr;
} handlers;

static int booze_getattr(const char* path, struct stat* st)
{
	const char* output;
	int status, scanned;
	WL_DECLINIT1(args, "%s", path);

	status = call_boozefn(handlers.getattr, args, &output);

	if (status)
		return status;

	if (!output)
		return -EIO;

	scanned = sscanf(output,"%li %o %li %i %i %li %li %li %li %li %li",
	                 &st->st_ino, &st->st_mode, &st->st_nlink, &st->st_uid,
	                 &st->st_gid, &st->st_rdev, &st->st_size, &st->st_blocks,
	                 &st->st_atime, &st->st_mtime, &st->st_ctime);
	return (scanned == 11) ? 0 : -EIO;
}

BASIC2(access, const char*, path, "%s", int, mask, "%d");

static int booze_readlink(const char* path, char* buf, size_t size)
{
	const char* output;
	int status;
	WL_DECLINIT1(args, "%s", path);

	status = call_boozefn(handlers.readlink, args, &output);

	if (status)
		return status;

	strncpy(buf, output, size);

	return 0;
}

static int booze_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
	const char* output;
	int status;
	char tmp[PATH_MAX];
	const char* de_start;
	const char* de_end;
	WL_DECLINIT1(args, "%s", path);

	status = call_boozefn(handlers.readdir, args, &output);

	if (status)
		return status;

	if (!output)
		return -EIO;

	for (de_start = output; *de_start; de_start = de_end + 1) {
		de_end = strchrnul(de_start, '/');

		/* -EIO if user booze_readdir provided an empty-string dirent */
		if (de_end == de_start)
			return -EIO;

		memcpy(tmp, de_start, de_end - de_start);
		tmp[de_end - de_start] = '\0';

		filler(buf, tmp, NULL, 0);

		if (!*de_end)
			break;
	}

	return 0;
}

BASIC3(mknod, const char*, path, "%s", mode_t, mode, "%d", dev_t, dev, "%d");
BASIC2(mkdir, const char*, path, "%s", mode_t, mode, "%d");
BASIC1(unlink, const char*, path, "%s");
BASIC1(rmdir, const char*, path, "%s");
BASIC2(symlink, const char*, from, "%s", const char*, to, "%s");
BASIC2(rename, const char*, from, "%s", const char*, to, "%s");
BASIC2(link, const char*, from, "%s", const char*, to, "%s");
BASIC2(chmod, const char*, path, "%s", mode_t, mode, "%d");
BASIC3(chown, const char*, path, "%s", uid_t, uid, "%d", gid_t, gid, "%d");
BASIC2(truncate, const char*, path, "%s", off_t, size, "%jd");

static int booze_utimens(const char* path, const struct timespec ts[2])
{
	char* t0 = xasprintf("%d.%09d", ts[0].tv_sec, ts[0].tv_nsec);
	char* t1 = xasprintf("%d.%09d", ts[1].tv_sec, ts[1].tv_nsec);
	WL_DECLINIT3(args, "%s", path, "%s", t0, "%s", t1);

	free(t0);
	free(t1);

	return call_boozefn(handlers.utimens, args, NULL);
}

static int booze_open(const char *path, struct fuse_file_info *fi)
{
	WL_DECLINIT2(args, "%s", path, "%d", fi->flags);

	return call_boozefn(handlers.open, args, NULL);
}

static void booze_read_child(const char* path, size_t size, off_t offset, int retpipe)
{
	int status;
	WL_DECLINIT3(args, "%s", path, "%zd", size, "%jd", (intmax_t)offset);

	status = call_boozefn(handlers.read, args, NULL);

	if (status)
		write(retpipe, &status, sizeof(status));

	exit(!!status);
}

static int booze_read(const char* path, char* buf, size_t size, off_t offset,
                      struct fuse_file_info* fi)
{
	pid_t pid;
	int childstatus;
	ssize_t datalen, retlen;
	int retpipe[2], datapipe[2];

	if (pipe(retpipe) || pipe(datapipe))
		return -errno;

	pid = fork();
	if (pid < 0)
		return -errno;
	else if (!pid) {
		/* Close pipe read ends */
		close(retpipe[0]);
		close(datapipe[0]);

		/* Move datapipe's write end to stdout */
		close(STDOUT_FILENO);
		if (dup2(datapipe[1], STDOUT_FILENO) != STDOUT_FILENO)
			exit(1);
		close(datapipe[1]);

		booze_read_child(path, size, offset, retpipe[1]);
	}

	close(retpipe[1]);
	close(datapipe[1]);

	datalen = readall(datapipe[0], buf, size);
	close(datapipe[0]);

	retlen = readall(retpipe[0], &childstatus, sizeof(childstatus));
	close(retpipe[0]);

	/*
	 * No wait(2) here; we're running inside bash, which handles that via
	 * some internal zombie-be-gone magic (trying to wait here results in
	 * a race, sometimes failing with ECHILD).
	 */

	if (retlen > 0)
		return (retlen == sizeof(childstatus)) ? childstatus : -EIO;
	else
		return datalen;
}

static void booze_write_child(const char* path, size_t size, off_t offset, int retpipe)
{
	int status;
	const char* output;
	WL_DECLINIT3(args, "%s", path, "%zd", size, "%jd", (intmax_t)offset);

	status = call_boozefn(handlers.write, args, &output);

	if (!status)
		status = atoi(output);

	write(retpipe, &status, sizeof(status));

	exit(status < 0);
}

static int booze_write(const char* path, const char* buf, size_t size, off_t offset,
                       struct fuse_file_info* fi)
{
	pid_t pid;
	int childstatus;
	ssize_t written, retlen;
	int retpipe[2], datapipe[2];

	if (pipe(retpipe) || pipe(datapipe))
		return -errno;

	pid = fork();
	if (pid < 0)
		return -errno;
	else if (!pid) {
		/* Close unused pipe ends */
		close(retpipe[0]);
		close(datapipe[1]);

		/* Move datapipe's read end to stdin */
		close(STDIN_FILENO);
		if (dup2(datapipe[0], STDIN_FILENO) != STDIN_FILENO)
			exit(1);
		close(datapipe[0]);

		booze_write_child(path, size, offset, retpipe[1]);
	}

	close(retpipe[1]);
	close(datapipe[0]);

	written = writeall(datapipe[1], buf, size);
	close(datapipe[1]);

	retlen = readall(retpipe[0], &childstatus, sizeof(childstatus));
	close(retpipe[0]);

	/* Again, no wait(2) here.  Bash handles this for us (somehow). */

	if (retlen != sizeof(childstatus))
		return -EIO;
	else /* The child can't claim to have written more than we sent it. */
		return (childstatus > written) ? -EIO : childstatus;
}

static int booze_statfs(const char* path, struct statvfs* st)
{
	const char* output;
	int status, scanned;
	WL_DECLINIT1(args, "%s", path);

	status = call_boozefn(handlers.statfs, args, &output);

	if (status)
		return status;

	if (!output)
		return -EIO;

	/* f_{frsize,favail,fsid,flag} are ignored */
	scanned = sscanf(output, "%lu %lu %lu %lu %lu %lu %lu",
	                 &st->f_bsize, &st->f_blocks, &st->f_bfree, &st->f_bavail,
	                 &st->f_files, &st->f_ffree, &st->f_namemax);

	return (scanned == 7) ? 0 : -EIO;
}

BASIC1_IL(release, const char*, path, "%s", struct fuse_file_info*, fi);
BASIC2_IL(fsync, const char*, path, "%s", int, datasync, "%d", struct fuse_file_info*, fi);
BASIC4_IL(fallocate, const char*, path, "%s", int, mode, "%d", off_t, offset, "%jd",
          off_t, length, "%jd", struct fuse_file_info*, fi);

static int booze_setxattr(const char* path, const char* name, const char* value,
                          size_t size, int flags)
{
	return -ENOSYS;
}

static int booze_getxattr(const char* path, const char* name, char* value, size_t size)
{
	return -ENOSYS;
}

static int booze_listxattr(const char* path, char* list, size_t size)
{
	return -ENOSYS;
}

static int booze_removexattr(const char* path, const char* name)
{
	return -ENOSYS;
}

static struct fuse_operations booze_ops = {
	.getattr = booze_getattr,
	.access = booze_access,
	.readlink = booze_readlink,
	.readdir = booze_readdir,
	.mknod = booze_mknod,
	.mkdir = booze_mkdir,
	.unlink = booze_unlink,
	.rmdir = booze_rmdir,
	.symlink = booze_symlink,
	.rename = booze_rename,
	.link = booze_link,
	.chmod = booze_chmod,
	.chown = booze_chown,
	.truncate = booze_truncate,
	.utimens = booze_utimens,
	.open = booze_open,
	.read = booze_read,
	.write = booze_write,
	.statfs = booze_statfs,
	.release = booze_release,
	.fsync = booze_fsync,
	.fallocate = booze_fallocate,
	.setxattr = booze_setxattr,
	.getxattr = booze_getxattr,
	.listxattr = booze_listxattr,
	.removexattr = booze_removexattr,
};

/*
 * Bash internals complicate getting FUSE to mount.  Normally it operates by
 * creating a socket, forking a 'fusermount' process and passing it the FD
 * number for the socket to use in the environment variable _FUSE_COMMFD using
 * setenv(3).  Bash, however, defines its own function 'setenv', which doesn't
 * call the C library's setenv, so libfuse's call to setenv gets linked to
 * bash's rather than the C library's, and thus when it fork()s and execve()s
 * fusermount, fusermount doesn't inherit the _FUSE_COMMFD environment
 * variable, because it's not in the process's *real* environment, just bash's
 * internally-managed one.  This results in fusermount failing with the error
 * message "old style mounting not supported".
 *
 * To work around this we set up the evironment variable for it.  First we
 * open a new file descriptor and immediately close it in order to guess the
 * numeric value of the file descriptor it *will* use.  Then we call dlsym(3)
 * with RTLD_NEXT to find the *real* setenv function from libc, and call that
 * to set _FUSE_COMMFD to whatever file descriptor value we're predicting
 * libfuse will use.
 *
 * This is a pretty gross hack, and frankly I'm not even sure why it works.
 * Since it's the bash executable that defines the first (RTLD_DEFAULT) setenv
 * and not booze.so, I would have expected the RTLD_NEXT dslym() lookup from
 * booze.so to return the same one, not libc's.  I doubt this is reliable, but
 * a better solution isn't immediately obvious to me (without modifying fuse
 * or bash)...hotpatch code in memory??
 */

static int next_fd(void)
{
	int fd;

	fd = dup(fileno(stdout));

	if (fd < 0) {
		perror("dup(stdout)");
		return -1;
	} else if (close(fd)) {
		perror("close(dup(stdout))");
		return -1;
	}

	return fd;
}

static int fuse_env_hack(void)
{
	int fd;
	char fdbuf[16];
	int (*real_setenv)(const char* name, const char* value, int overwrite);
	void* next_setenv_addr;
	char* err;

	if ((fd = next_fd()) < 0)
		return -1;

	dlerror();
	next_setenv_addr = dlsym(RTLD_NEXT, "setenv");
	err = dlerror();

	if (!next_setenv_addr || err) {
		internal_error("couldn't find real setenv: %s",
		               err ? err : "[no dlerror??]");
		return -1;
	}

	memcpy(&real_setenv, &next_setenv_addr, sizeof(real_setenv));

	snprintf(fdbuf, sizeof(fdbuf), "%d", fd);

	if (real_setenv("_FUSE_COMMFD", fdbuf, 1)) {
		perror("real_setenv");
		return -1;
	}

	return 0;
}

static int set_up_handlers(const char* arrname)
{
	SHELL_VAR* var;
	char* hname;

	var = find_variable(arrname);
	if (!var) {
		builtin_error("%s: variable not found", arrname);
		return -1;
	}

	if (!assoc_p(var)) {
		builtin_error("%s: not an associative array", arrname);
		return -1;
	}

#define SETUP(fn) \
	do { \
		hname = assoc_reference(assoc_cell(var), #fn); \
		handlers.fn = hname ? xasprintf("%s", hname) : NULL; \
	} while (0)

	SETUP(getattr);
	SETUP(access);
	SETUP(readlink);
	SETUP(readdir);
	SETUP(mknod);
	SETUP(mkdir);
	SETUP(unlink);
	SETUP(rmdir);
	SETUP(symlink);
	SETUP(rename);
	SETUP(link);
	SETUP(chmod);
	SETUP(chown);
	SETUP(truncate);
	SETUP(utimens);
	SETUP(open);
	SETUP(read);
	SETUP(write);
	SETUP(statfs);
	SETUP(release);
	SETUP(fsync);
	SETUP(fallocate);
	SETUP(setxattr);
	SETUP(getxattr);
	SETUP(listxattr);
	SETUP(removexattr);

#undef SETUP

	return 0;
}

static void tear_down_handlers(void)
{
#define TEARDOWN(fn) do { if (handlers.fn) free(handlers.fn); } while (0)
	TEARDOWN(getattr);
	TEARDOWN(access);
	TEARDOWN(readlink);
	TEARDOWN(readdir);
	TEARDOWN(mknod);
	TEARDOWN(mkdir);
	TEARDOWN(unlink);
	TEARDOWN(rmdir);
	TEARDOWN(symlink);
	TEARDOWN(rename);
	TEARDOWN(link);
	TEARDOWN(chmod);
	TEARDOWN(chown);
	TEARDOWN(truncate);
	TEARDOWN(utimens);
	TEARDOWN(open);
	TEARDOWN(read);
	TEARDOWN(write);
	TEARDOWN(statfs);
	TEARDOWN(release);
	TEARDOWN(fsync);
	TEARDOWN(fallocate);
	TEARDOWN(setxattr);
	TEARDOWN(getxattr);
	TEARDOWN(listxattr);
	TEARDOWN(removexattr);
#undef TEARDOWN
}

static int booze_builtin(WORD_LIST* args)
{
	int status;
	char* fuse_argv[] = {
		"booze", NULL, "-s",
#ifdef BOOZE_DEBUG
		"-d",
#endif
	};

	if (!args || !args->next || args->next->next) {
		builtin_usage();
		return EX_USAGE;
	}

	set_up_handlers(args->word->word);
	fuse_argv[1] = args->next->word->word;

	if (fuse_env_hack())
		return EXECUTION_FAILURE;

	status = fuse_main(sizeof(fuse_argv)/sizeof(fuse_argv[0]), fuse_argv,
	                   &booze_ops, NULL);

	tear_down_handlers();

	return status ? EXECUTION_FAILURE : EXECUTION_SUCCESS;
}

static char* booze_doc[] = {
	"Mount a booze filesystem at MOUNTPOINT using functions in FN_ASSOC.",
	"",
	"If for any reason this doesn't seem like a good idea, the user is ",
	"encouraged to drink until it does.",
	NULL,
};

struct builtin booze_struct = {
	"booze",
	booze_builtin,
	BUILTIN_ENABLED,
	booze_doc,
	"booze FN_ASSOC MOUNTPOINT",
	0,
};
