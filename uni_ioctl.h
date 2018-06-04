#ifndef _UNI_IOCTL_H_
#define _UNI_IOCTL_H_

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/fs.h>
#include "user_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define uni_ioctl(fd, request, ...)	\
		 ({ \
	  		int ret; \
			if (!user_is_nvme(fd)) { \
				ret = ioctl(fd, request, ## __VA_ARGS__); \
			} else { \
				ret = user_ioctl(fd, request, ## __VA_ARGS__); \
			} \
			ret; \
		})

int uni_is_nvme(int fd, struct stat *statp);
int uni_is_char(int fd, struct stat *statp);
int uni_is_blk(int fd, struct stat *statp);

int uni_open(const char *path, int oflag);

#ifdef __cplusplus
}
#endif

#endif
