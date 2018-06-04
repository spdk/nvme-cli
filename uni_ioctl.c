#include "uni_ioctl.h"

#ifndef USER_IOCTL

const char *user_ioctl_dir = NULL;

int user_is_nvme_path(const char *path)
{
	return 0;
}

int user_is_char_path(const char *path)
{
	return 0;
}

int user_is_blk_path(const char *path)
{
	return 0;
}

int user_is_nvme(int fd)
{
	return 0;
}

int user_is_char(int fd)
{
	return 0;
}

int user_is_blk(int fd)
{
	return 0;
}

int user_open(const char *path, int oflag)
{
	return -1;
}

int user_ioctl(int fd, unsigned long request, ...)
{
	return -1;
}

#endif

int uni_is_nvme(int fd, struct stat *statp)
{
	if (statp && (S_ISCHR(statp->st_mode) || S_ISBLK(statp->st_mode))) {
		return 1;
	}
	if (user_is_nvme(fd)) {
		return 1;
	}
	return 0;
}
int uni_is_char(int fd, struct stat *statp)
{
	if (statp && S_ISCHR(statp->st_mode)) {
		return 1;
	}
	if (user_is_char(fd)) {
		return 1;
	}
	return 0;
}

int uni_is_blk(int fd, struct stat *statp)
{
	if (statp && S_ISBLK(statp->st_mode)) {
		return 1;
	}
	if (user_is_blk(fd)) {
		return 1;
	}
	return 0;
}

int
uni_open(const char *path, int oflag)
{
	if (!user_is_nvme_path(path)) {
		return open(path, oflag);
	} else {
		return user_open(path, oflag);
	}
}
