/*
XISO.C

Copies the maps folder out of an Xbox disc image (an "xiso", or an image of
a whole disc), for the desktop ports' first start without game data
(sdl_platform.c).

Written for SilentCartographer from a description of the Xbox's disc file
system (XDVDFS), not from another program's code:

- The image is read in 2048-byte sectors, counted from the start of the game
  partition. An xiso starts with the partition; an image of a whole disc has
  the disc's video partition first, so the game partition starts at one of
  xiso_partition_offsets.
- The partition's sector 32 is its volume descriptor: XISO_MAGIC at its start
  and again at XISO_MAGIC_END_OFFSET, and between them the root directory's
  sector and size in bytes (little-endian 32-bit words).
- A directory is a table of entries, each a node of a binary search tree
  whose root is at the table's start: the offsets of its left and right
  subtrees (16-bit, in 4-byte units; 0 for none), the first sector of its
  data and the data's size (32-bit), its attributes (a byte: XISO_DIRECTORY
  for a directory) and its name (a byte of length, then the name). Entries
  are aligned to 4 bytes and do not cross a sector; the space after the last
  in a sector is filled with 0xFF.

The image is the player's own file but untrusted all the same: each table is
bounded, every entry is checked against its table, every extent against the
image, and the walk of a table visits each offset once, so that a damaged or
crafted image is refused instead of read out of bounds or walked forever.

The files are written to <destination>/maps.partial first, which becomes
<destination>/maps once they all are, so an interrupted extraction never
leaves a maps folder that looks complete.
*/

#if !defined(HALO_ANDROID) && !defined(HALO_WEB)

#include "posix.h"
#include "xiso.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* ---------- constants */

#define XISO_SECTOR_SIZE 2048
#define XISO_DESCRIPTOR_SECTOR 32
#define XISO_MAGIC "MICROSOFT*XBOX*MEDIA"
#define XISO_MAGIC_SIZE 20
#define XISO_MAGIC_END_OFFSET 0x7EC
#define XISO_ROOT_SECTOR_OFFSET 20
#define XISO_ROOT_SIZE_OFFSET 24

#define XISO_ENTRY_HEADER_SIZE 14
#define XISO_DIRECTORY 0x10

/* larger than any directory of a game disc */
#define XISO_MAXIMUM_TABLE_SIZE (4 * 1024 * 1024)
/* Halo's maps folder holds about 30 files */
#define XISO_MAXIMUM_FILES 512
#define XISO_NAME_SIZE 256
#define XISO_COPY_SIZE (1024 * 1024)

/* where the game partition starts: an xiso, then images of a whole disc of
each of the Xbox's and the Xbox 360's disc formats */
static const unsigned long long xiso_partition_offsets[] =
{
	0ULL,
	0x18300000ULL,
	0x0FD90000ULL,
	0x02080000ULL,
};

/* ---------- types */

struct xiso_image
{
	int descriptor;
	unsigned long long size;
	unsigned long long partition;
	char *error;
	int error_size;
};

struct xiso_file
{
	char name[XISO_NAME_SIZE];
	unsigned long sector;
	unsigned long size;
	int directory;
};

struct xiso_file_list
{
	struct xiso_file *files;
	int count;
};

/* ---------- prototypes */

static int xiso_fail(struct xiso_image *image, const char *format, const char *detail);
static unsigned long xiso_read_u32(const unsigned char *bytes);
static unsigned short xiso_read_u16(const unsigned char *bytes);
static int xiso_read(struct xiso_image *image, unsigned long long offset, void *buffer, unsigned long size);
static int xiso_extent_fits(struct xiso_image *image, unsigned long sector, unsigned long size);
static int xiso_find_partition(struct xiso_image *image, unsigned long *root_sector, unsigned long *root_size);
static unsigned char *xiso_read_table(struct xiso_image *image, unsigned long sector, unsigned long size);
static int xiso_list_table(const unsigned char *table, unsigned long size, struct xiso_file_list *list);
static int xiso_plain_name(const char *name);
static int xiso_same_name(const char *a, const char *b);
static void xiso_join(char *path, int path_size, const char *directory, const char *name);
static int xiso_is_directory(const char *path);
static int xiso_make_directory(struct xiso_image *image, const char *path);
static void xiso_empty_directory(const char *path);
static int xiso_copy_file(struct xiso_image *image, const struct xiso_file *file, const char *path,
	unsigned char *buffer, unsigned long long *done, unsigned long long total, xiso_progress_proc progress,
	void *context);
static int xiso_move_into_place(struct xiso_image *image, const struct xiso_file_list *list, const char *partial,
	const char *maps);

/* ---------- public code */

int xiso_extract_maps(const char *image_path, const char *destination, xiso_progress_proc progress, void *context,
	char *error, int error_size)
{
	struct xiso_image image;
	struct posix_file_information information;
	struct xiso_file_list root = { NULL, 0 };
	struct xiso_file_list maps = { NULL, 0 };
	unsigned char *table = NULL;
	unsigned char *buffer = NULL;
	unsigned long root_sector;
	unsigned long root_size;
	unsigned long long total = 0;
	unsigned long long done = 0;
	posix_ulong free_low;
	posix_ulong free_high;
	posix_ulong total_low;
	posix_ulong total_high;
	char partial[1200];
	char final[1200];
	char path[1500];
	const struct xiso_file *maps_entry = NULL;
	int has_ui = 0;
	int succeeded = 0;
	int index;

	memset(&image, 0, sizeof(image));
	image.descriptor = -1;
	image.error = error;
	image.error_size = error_size;
	if (error_size > 0)
		error[0] = '\0';
	xiso_join(partial, sizeof(partial), destination, "maps.partial");
	xiso_join(final, sizeof(final), destination, "maps");

	image.descriptor = open(image_path, O_RDONLY | O_BINARY | O_CLOEXEC);
	if (image.descriptor < 0)
	{
		xiso_fail(&image, "The disc image could not be opened:\n%s", strerror(errno));
		goto done;
	}
	if (posix_fstat(image.descriptor, &information) != 0)
	{
		xiso_fail(&image, "The disc image could not be read:\n%s", strerror(errno));
		goto done;
	}
	image.size = ((unsigned long long)information.size_high << 32) | information.size_low;
	if (!xiso_find_partition(&image, &root_sector, &root_size))
		goto done;

	/* the root's maps folder */
	table = xiso_read_table(&image, root_sector, root_size);
	if (!table || !xiso_list_table(table, root_size, &root))
	{
		xiso_fail(&image, "%s", "The disc image's root folder is damaged.");
		goto done;
	}
	free(table);
	table = NULL;
	for (index = 0; index < root.count; index++)
	{
		if (root.files[index].directory && xiso_same_name(root.files[index].name, "maps"))
		{
			maps_entry = &root.files[index];
			break;
		}
	}
	if (!maps_entry)
	{
		xiso_fail(&image, "%s", "The disc image has no maps folder: is it an image of Halo: Combat Evolved?");
		goto done;
	}
	table = xiso_read_table(&image, maps_entry->sector, maps_entry->size);
	if (!table || !xiso_list_table(table, maps_entry->size, &maps))
	{
		xiso_fail(&image, "%s", "The disc image's maps folder is damaged.");
		goto done;
	}

	/* its files: plain names only, each once, each inside the image */
	for (index = 0; index < maps.count; index++)
	{
		struct xiso_file *file = &maps.files[index];
		int other;

		if (file->directory || !xiso_plain_name(file->name))
		{
			file->name[0] = '\0';
			continue;
		}
		for (other = 0; other < index; other++)
		{
			if (maps.files[other].name[0] && xiso_same_name(maps.files[other].name, file->name))
				break;
		}
		if (other < index)
		{
			file->name[0] = '\0';
			continue;
		}
		if (!xiso_extent_fits(&image, file->sector, file->size))
		{
			xiso_fail(&image, "The disc image ends before %s does: is the image complete?", file->name);
			goto done;
		}
		if (xiso_same_name(file->name, "ui.map"))
			has_ui = 1;
		total += file->size;
	}
	if (!has_ui)
	{
		xiso_fail(&image, "%s", "The disc image's maps folder has no ui.map: is it an image of Halo: Combat Evolved?");
		goto done;
	}
	if (posix_disk_space(destination, &free_low, &free_high, &total_low, &total_high) == 0 &&
		(((unsigned long long)free_high << 32) | free_low) < total)
	{
		char needed[64];

		snprintf(needed, sizeof(needed), "%llu MB", (total + 1048575ULL) / 1048576ULL);
		xiso_fail(&image, "There is not enough free space for the maps folder (%s).", needed);
		goto done;
	}

	/* copied into maps.partial, then moved into place */
	if (!xiso_make_directory(&image, destination) || !xiso_make_directory(&image, partial))
		goto done;
	xiso_empty_directory(partial);
	buffer = malloc(XISO_COPY_SIZE);
	if (!buffer)
	{
		xiso_fail(&image, "%s", "Out of memory.");
		goto done;
	}
	for (index = 0; index < maps.count; index++)
	{
		const struct xiso_file *file = &maps.files[index];

		if (!file->name[0])
			continue;
		xiso_join(path, sizeof(path), partial, file->name);
		if (!xiso_copy_file(&image, file, path, buffer, &done, total, progress, context))
			goto done;
	}
	succeeded = xiso_move_into_place(&image, &maps, partial, final);

done:
	if (image.descriptor >= 0)
		close(image.descriptor);
	free(buffer);
	free(table);
	free(root.files);
	free(maps.files);
	if (!succeeded && error_size > 0 && !error[0])
		snprintf(error, error_size, "%s", "The maps folder could not be copied.");
	return succeeded;
}

/* ---------- private code */

static int xiso_fail(struct xiso_image *image, const char *format, const char *detail)
{
	if (image->error_size > 0)
		snprintf(image->error, image->error_size, format, detail);
	return 0;
}

static unsigned long xiso_read_u32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

static unsigned short xiso_read_u16(const unsigned char *bytes)
{
	return (unsigned short)(bytes[0] | (bytes[1] << 8));
}

/* reads size bytes at offset (from the image's start); 0 if the image ends
first or cannot be read */
static int xiso_read(struct xiso_image *image, unsigned long long offset, void *buffer, unsigned long size)
{
	unsigned char *bytes = buffer;
	unsigned long got = 0;

	if (offset > image->size || size > image->size - offset)
		return 0;
	while (got < size)
	{
		ssize_t count = pread(image->descriptor, bytes + got, size - got, (off_t)(offset + got));

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			return 0;
		got += (unsigned long)count;
	}
	return 1;
}

/* whether size bytes from the partition's sector lie inside the image */
static int xiso_extent_fits(struct xiso_image *image, unsigned long sector, unsigned long size)
{
	unsigned long long start = image->partition + (unsigned long long)sector * XISO_SECTOR_SIZE;

	return start <= image->size && size <= image->size - start;
}

static int xiso_find_partition(struct xiso_image *image, unsigned long *root_sector, unsigned long *root_size)
{
	unsigned char sector[XISO_SECTOR_SIZE];
	int index;

	for (index = 0; index < (int)(sizeof(xiso_partition_offsets) / sizeof(*xiso_partition_offsets)); index++)
	{
		unsigned long long offset = xiso_partition_offsets[index] +
			(unsigned long long)XISO_DESCRIPTOR_SECTOR * XISO_SECTOR_SIZE;

		if (!xiso_read(image, offset, sector, sizeof(sector)))
			continue;
		if (memcmp(sector, XISO_MAGIC, XISO_MAGIC_SIZE) != 0 ||
			memcmp(sector + XISO_MAGIC_END_OFFSET, XISO_MAGIC, XISO_MAGIC_SIZE) != 0)
		{
			continue;
		}
		image->partition = xiso_partition_offsets[index];
		*root_sector = xiso_read_u32(sector + XISO_ROOT_SECTOR_OFFSET);
		*root_size = xiso_read_u32(sector + XISO_ROOT_SIZE_OFFSET);
		return 1;
	}
	return xiso_fail(image, "%s", "The file is not an Xbox disc image (no XDVDFS volume was found in it).");
}

/* a directory's table, or NULL if it is empty, too large or not inside the
image */
static unsigned char *xiso_read_table(struct xiso_image *image, unsigned long sector, unsigned long size)
{
	unsigned char *table;

	if (size < XISO_ENTRY_HEADER_SIZE || size > XISO_MAXIMUM_TABLE_SIZE || !xiso_extent_fits(image, sector, size))
		return NULL;
	table = malloc(size);
	if (table && !xiso_read(image, image->partition + (unsigned long long)sector * XISO_SECTOR_SIZE, table, size))
	{
		free(table);
		table = NULL;
	}
	return table;
}

/* lists a directory's entries by walking its tree from the table's start,
each offset at most once; returns 0 if the table holds no valid entry or
the list cannot be made */
static int xiso_list_table(const unsigned char *table, unsigned long size, struct xiso_file_list *list)
{
	unsigned long units = (size + 3) / 4;
	unsigned char *seen = calloc(units, 1);
	/* (each entry is visited once and adds at most two) */
	unsigned short *pending = malloc((2 * units + 1) * sizeof(*pending));
	unsigned long pending_count = 0;

	list->files = calloc(XISO_MAXIMUM_FILES, sizeof(*list->files));
	list->count = 0;
	if (!seen || !pending || !list->files)
	{
		free(seen);
		free(pending);
		return 0;
	}
	pending[pending_count++] = 0;
	while (pending_count > 0)
	{
		unsigned long unit = pending[--pending_count];
		unsigned long offset = unit * 4;
		const unsigned char *entry = table + offset;
		unsigned short left;
		unsigned short right;
		int name_length;
		struct xiso_file *file;

		if (unit >= units || seen[unit] || offset + XISO_ENTRY_HEADER_SIZE > size)
			continue;
		seen[unit] = 1;
		left = xiso_read_u16(entry);
		right = xiso_read_u16(entry + 2);
		name_length = entry[13];
		/* sector padding, or a name past the table */
		if ((left == 0xFFFF && right == 0xFFFF) || name_length == 0 ||
			offset + XISO_ENTRY_HEADER_SIZE + name_length > size)
		{
			continue;
		}
		if (list->count < XISO_MAXIMUM_FILES)
		{
			file = &list->files[list->count++];
			memcpy(file->name, entry + XISO_ENTRY_HEADER_SIZE, name_length);
			file->name[name_length] = '\0';
			file->sector = xiso_read_u32(entry + 4);
			file->size = xiso_read_u32(entry + 8);
			file->directory = (entry[12] & XISO_DIRECTORY) != 0;
		}
		/* (0 is no subtree: nothing points back at the root) */
		if (left != 0 && left < units)
			pending[pending_count++] = left;
		if (right != 0 && right < units)
			pending[pending_count++] = right;
	}
	free(seen);
	free(pending);
	return list->count > 0;
}

/* a name that is safe to create in the maps folder: no separators, drive
letters or control characters, and nothing hidden (".complete", "..") */
static int xiso_plain_name(const char *name)
{
	const unsigned char *character;

	if (!name[0] || name[0] == '.')
		return 0;
	for (character = (const unsigned char *)name; *character; character++)
	{
		if (*character < 0x20 || *character == 0x7F || *character == '/' || *character == '\\' ||
			*character == ':')
		{
			return 0;
		}
	}
	return 1;
}

/* names compared as the Xbox compares them, ignoring ASCII case */
static int xiso_same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		int ca = (*a >= 'A' && *a <= 'Z') ? *a + ('a' - 'A') : *a;
		int cb = (*b >= 'A' && *b <= 'Z') ? *b + ('a' - 'A') : *b;

		if (ca != cb)
			return 0;
	}
	return *a == *b;
}

static void xiso_join(char *path, int path_size, const char *directory, const char *name)
{
	size_t length = strlen(directory);

	if (length > 0 && (directory[length - 1] == '/' || directory[length - 1] == '\\'))
		snprintf(path, path_size, "%s%s", directory, name);
	else
		snprintf(path, path_size, "%s/%s", directory, name);
}

static int xiso_is_directory(const char *path)
{
	struct posix_file_information information;

	return posix_stat(path, &information) == 0 && (information.flags & _posix_file_is_directory) != 0;
}

static int xiso_make_directory(struct xiso_image *image, const char *path)
{
	if (posix_make_directory(path) == 0 || xiso_is_directory(path))
		return 1;
	return xiso_fail(image, "The folder %s could not be made.", path);
}

/* removes the files an interrupted extraction left in maps.partial */
static void xiso_empty_directory(const char *path)
{
	void *directory = posix_directory_open(path);
	char name[XISO_NAME_SIZE];
	char file[1500];

	if (!directory)
		return;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		xiso_join(file, sizeof(file), path, name);
		remove(file);
	}
	posix_directory_close(directory);
}

static int xiso_copy_file(struct xiso_image *image, const struct xiso_file *file, const char *path,
	unsigned char *buffer, unsigned long long *done, unsigned long long total, xiso_progress_proc progress,
	void *context)
{
	unsigned long long source = image->partition + (unsigned long long)file->sector * XISO_SECTOR_SIZE;
	unsigned long left = file->size;
	int output = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | O_CLOEXEC, 0644);

	if (output < 0)
		return xiso_fail(image, "A map could not be written:\n%s", strerror(errno));
	if (progress)
		progress(context, file->name, *done, total);
	while (left > 0)
	{
		unsigned long count = left < XISO_COPY_SIZE ? left : XISO_COPY_SIZE;
		unsigned long written = 0;

		if (!xiso_read(image, source, buffer, count))
		{
			close(output);
			return xiso_fail(image, "The disc image could not be read at %s: is the image complete?", file->name);
		}
		while (written < count)
		{
			ssize_t result = write(output, buffer + written, count - written);

			if (result < 0 && errno == EINTR)
				continue;
			if (result <= 0)
			{
				close(output);
				return xiso_fail(image, "A map could not be written:\n%s", result < 0 ? strerror(errno) : "the disk is full");
			}
			written += (unsigned long)result;
		}
		source += count;
		left -= count;
		*done += count;
		if (progress)
			progress(context, file->name, *done, total);
	}
	if (close(output) != 0)
		return xiso_fail(image, "A map could not be written:\n%s", strerror(errno));
	return 1;
}

/* maps.partial becomes maps; where a maps folder is already there (empty, or
from an earlier copy that lacked ui.map) the files are moved into it one by
one, each replacing its namesake */
static int xiso_move_into_place(struct xiso_image *image, const struct xiso_file_list *list, const char *partial,
	const char *maps)
{
	char from[1500];
	char to[1500];
	int index;

	if (rename(partial, maps) == 0)
		return 1;
	if (!xiso_is_directory(maps))
		return xiso_fail(image, "The maps folder could not be put in place:\n%s", strerror(errno));
	for (index = 0; index < list->count; index++)
	{
		if (!list->files[index].name[0])
			continue;
		xiso_join(from, sizeof(from), partial, list->files[index].name);
		xiso_join(to, sizeof(to), maps, list->files[index].name);
		remove(to);
		if (rename(from, to) != 0)
			return xiso_fail(image, "The maps folder could not be put in place:\n%s", strerror(errno));
	}
	/* (empty now: remove takes an empty folder on POSIX systems; one left on
	Windows does no harm) */
	remove(partial);
	return 1;
}

#endif
