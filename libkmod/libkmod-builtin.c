/*
 * libkmod - interface to kernel built-in modules
 *
 * Copyright (C) 2019  Alexey Gladkov <gladkov.alexey@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libkmod.h"
#include "libkmod-internal.h"

#define MODULES_BUILTIN_MODINFO "modules.builtin.modinfo"

struct kmod_builtin_iter {
	struct kmod_ctx *ctx;
	struct kmod_file *file;
	off_t pos;
	off_t next;
};

struct kmod_builtin_iter *kmod_builtin_iter_new(struct kmod_ctx *ctx)
{
	char path[PATH_MAX];
	struct kmod_file *file;
	struct kmod_builtin_iter *iter;
	const char *dirname = kmod_get_dirname(ctx);
	size_t len = strlen(dirname);

	if ((len + 1 + strlen(MODULES_BUILTIN_MODINFO) + 1) >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	snprintf(path, PATH_MAX, "%s/%s", dirname, MODULES_BUILTIN_MODINFO);

	file = kmod_file_open(ctx, path);
	if (!file)
		return NULL;

	iter = malloc(sizeof(*iter));
	if (!iter) {
		kmod_file_unref(file);
		errno = ENOMEM;
		return NULL;
	}

	iter->ctx = ctx;
	iter->file = file;
	iter->pos = 0;
	iter->next = 0;

	return iter;
}

bool kmod_builtin_iter_next(struct kmod_builtin_iter *iter)
{
	char *mm, *s, *dot;
	off_t offset, mmsize;
	size_t len, modlen;
	char *modname = NULL;

	mm = kmod_file_get_contents(iter->file);
	mmsize = kmod_file_get_size(iter->file);

	offset = iter->next;

	while (offset < mmsize) {
		s = mm + offset;

		dot = strchr(s, '.');
		if (!dot)
			return false;

		len = dot - s;

		if (!modname) {
			modname = s;
			modlen = len;
		} else if (modlen != len || strncmp(modname, s, len)) {
			break;
		}

		offset += strlen(s) + 1;
	}

	if (!modname)
		return false;

	iter->next = offset;

	return true;
}

void kmod_builtin_iter_free(struct kmod_builtin_iter *iter)
{
	kmod_file_unref(iter->file);
	free(iter);
}

int kmod_builtin_iter_get_strings(struct kmod_builtin_iter *iter,
					const char **strings)
{
	char *mm = kmod_file_get_contents(iter->file);
	off_t pos = iter->pos;

	char *start = NULL;
	size_t count = 0;
	size_t modlen = 0;

	while (pos < iter->next) {
		char *dot = strchr(mm + pos, '.');
		size_t len;

		if (!dot)
			return -1;

		len = dot - (mm + pos);

		if (!start) {
			start = mm + pos;
			modlen = len;
		} else if (modlen != len || strncmp(start, mm + pos, len)) {
			break;
		}

		pos += strlen(mm + pos) + 1;
		count++;
	}

	*strings = start;
	iter->pos = iter->next;

	return count;
}

/* array will be allocated with strings in a single malloc, just free *array */
int kmod_builtin_get_modinfo(struct kmod_ctx *ctx, const char *modname,
				char ***modinfo)
{
	char *mm, *s, *section, *dot;
	off_t n, size, offset, mmoffset, mmsize;
	size_t modlen, len;
	struct kmod_builtin_iter *iter;
	int count = 0;

	iter = kmod_builtin_iter_new(ctx);

	if (!iter)
		return -1;

	modlen = strlen(modname);

	mmsize = kmod_file_get_size(iter->file);
	mm = kmod_file_get_contents(iter->file);

	section = NULL;
	size = 0;

	for (mmoffset = 0; mmoffset < mmsize;) {
		s = mm + mmoffset;
		dot = strchr(s, '.');

		if (!dot) {
			count = -ENODATA;
			goto fail;
		}

		len = dot - s;

		if (modlen != len || strncmp(modname, s, len)) {
			if (count)
				break;
			mmoffset += strlen(s) + 1;
			continue;
		} else if (!count) {
			section = s;
		}

		len = strlen(dot + 1) + 1;
		mmoffset += modlen + 1 + len;
		size += len;

		count++;
	}

	if (!count) {
		count = -ENOSYS;
		goto fail;
	}

	*modinfo = malloc(size + sizeof(char *) * (count + 1));
	if (!*modinfo) {
		count = -errno;
		goto fail;
	}

	s = (char *)(*modinfo + count + 1);

	n = 0;
	mmoffset = 0;

	for (offset = 0; offset < size;) {
		len = strlen(section + mmoffset + modlen + 1) + 1;

		strncpy(s + offset, section + mmoffset + modlen + 1, len);
		(*modinfo)[n++] = s + offset;

		mmoffset += modlen + 1 + len;
		offset += len;
	}

fail:
	kmod_builtin_iter_free(iter);
	return count;
}
