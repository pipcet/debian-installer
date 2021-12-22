/*
 * Miscellaneous helper functions
 *
 * Copyright IBM Corp. 2015
 *
 * Author(s): Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 *	      Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "s390-dev.h"

#define READ_CHUNK_SIZE		4096

/* Read text from @fd and return resulting NULL-terminated text buffer.
 * If @chomp is non-zero, remove trailing newline character. Return %NULL
 * on error or when unprintable characters are read.
 */
static char *read_fd(FILE *fd, int chomp)
{
	char *buffer = NULL;
	size_t done, i;

	done = 0;
	while (!feof(fd)) {
		buffer = realloc(buffer, done + READ_CHUNK_SIZE + 1);
		if (buffer == NULL)
			return NULL;
		done += fread(&buffer[done], 1, READ_CHUNK_SIZE, fd);
		if (ferror(fd)) {
			free(buffer);
			return NULL;
		}
	}

	/* Check if this is a text file at all (required to filter out
	 * binary sysfs attributes). */
	for (i = 0; i < done; i++) {
		if (!isgraph(buffer[i]) && !isspace(buffer[i])) {
			free(buffer);
			return NULL;
		}
	}

	/* Remove trailing new-line character if requested. */
	if (chomp && done > 0 && buffer[done - 1] == '\n')
		done--;

	if (buffer) {
		/* NULL-terminate. */
		buffer[done++] = 0;
	}

	return realloc(buffer, done);
}

/* Read file as text and return NULL-terminated contents. Remove trailing
 * newline if CHOMP is specified.
 */
char *misc_read_text_file(const char *path, int chomp)
{
	char *buffer = NULL;
	FILE *fd;

	fd = fopen(path, "r");
	if (fd == NULL)
		goto out;

	buffer = read_fd(fd, chomp);
	fclose(fd);
out:
	return buffer;
}

/* Write text to file. */
static int write_text(const char *path, const char *text)
{
	size_t len = strlen(text);
	FILE *fd;
	int err;

	fd = fopen(path, "w");
	if (fd == NULL)
		goto err;
	if (fwrite(text, 1, len, fd) != len)
		goto err;
	if (fclose(fd)) {
		fd = NULL;
		goto err;
	}

	return 0;
err:
	if (fd != NULL) {
		err = errno;
		fclose(fd);
		errno = err;
	}

	return -1;
}

result_t misc_write_text_file(const char *path, const char *text)
{
	if (write_text(path, text))
		return RESULT_RUNTIME_ERROR;

	return RESULT_OK;
}

#define READLINE_SIZE	4096

/* Return a newly allocated string containing the contents of the symbolic link
 * at the specified path or NULL on error. */
char *misc_readlink(const char *path)
{
	char *name;
	ssize_t len;

	name = malloc(READLINE_SIZE);
	len = readlink(path, name, READLINE_SIZE - 1);
	if (len < 0) {
		free(name);
		return NULL;
	}
	name[len++] = 0;

	return realloc(name, len);
}

/* Check if a directory exists. */
int dir_exists(const char *path)
{
	struct stat s;

	if (stat(path, &s) != 0)
		return 0;
	return S_ISDIR(s.st_mode);
}

char *strtrim(char *s)
{
	size_t len;
	char *trail;

	len = strlen(s);
	if (!len)
		return s;

	trail = s + len - 1;
	while (trail >= s && isblank(*trail))
		trail--;
	trail[1] = '\0';

	while (*s && isblank(*s))
		s++;

	return s;
}
