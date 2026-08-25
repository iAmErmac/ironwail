#include "quakedef.h"
#include "miniz.h"

static qfileofs_t FS_Pk3FileLength(FILE *file)
{
	qfileofs_t pos = Sys_ftell(file);
	qfileofs_t length;
	Sys_fseek(file, 0, SEEK_END);
	length = Sys_ftell(file);
	Sys_fseek(file, pos, SEEK_SET);
	return length;
}
static size_t FS_Pk3Read(void *opaque, mz_uint64 ofs, void *buf, size_t bytes)
{
	FILE *file = opaque;
	if (Sys_fseek(file, (qfileofs_t)ofs, SEEK_SET) < 0)
		return 0;
	return fread(buf, 1, bytes, file);
}

qboolean FS_Pk3CanonicalizePath(const char *path, char *out, size_t outsize)
{
	const char *segment;
	size_t written = 0;

	if (!path || !*path || !out || outsize < 2 || *path == '/' || *path == '\\' || strchr(path, ':'))
		return false;
	segment = path;
	while (*path)
	{
		char c = *path++;
		if (c == '/' || c == '\\')
		{
			if (path - segment == 1 || (path - segment == 3 && segment[0] == '.' && segment[1] == '.'))
				return false;
			if (written + 1 >= outsize)
				return false;
			out[written++] = '/';
			segment = path;
			continue;
		}
		if ((unsigned char)c < 32 || written + 1 >= outsize)
			return false;
		out[written++] = (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
	}
	if (path - segment == 1 || (path - segment == 3 && segment[0] == '.' && segment[1] == '.'))
		return false;
	out[written] = 0;
	return true;
}

pack_t *FS_Pk3LoadArchive(const char *packfile)
{
	mz_zip_archive archive;
	mz_zip_archive_file_stat stat;
	FILE *file;
	packfile_t *files;
	pack_t *pack;
	mz_uint count, i;
	int accepted = 0, handle;
	qfileofs_t size;

	file = Sys_fopen(packfile, "rb");
	if (!file)
		return NULL;
	size = FS_Pk3FileLength(file);
	memset(&archive, 0, sizeof(archive));
	archive.m_pIO_opaque = file;
	archive.m_pRead = FS_Pk3Read;
	if (size <= 0 || !mz_zip_reader_init(&archive, (mz_uint64)size, 0))
	{
		Con_Printf("PK3 ignored: %s is not a supported ZIP archive\n", packfile);
		mz_zip_reader_end(&archive);
		fclose(file);
		return NULL;
	}
	count = archive.m_total_files;
	if (!count || count > INT_MAX || count > SIZE_MAX / sizeof(*files))
		goto fail;
	files = Z_Malloc((size_t)count * sizeof(*files));
	for (i = 0; i < count; ++i)
	{
		int j;
		if (!mz_zip_reader_file_stat(&archive, i, &stat) || stat.m_is_directory || stat.m_is_encrypted ||
			(stat.m_method != 0 && stat.m_method != 8) || stat.m_uncomp_size > INT_MAX ||
			!FS_Pk3CanonicalizePath(stat.m_filename, files[accepted].name, sizeof(files[accepted].name)))
			continue;
		for (j = 0; j < accepted; ++j)
			if (!strcmp(files[j].name, files[accepted].name))
				break;
		if (j != accepted)
		{
			Con_DPrintf("PK3 ignored duplicate entry %s in %s\n", stat.m_filename, packfile);
			continue;
		}
		files[accepted].filelen = (int)stat.m_uncomp_size;
		files[accepted].filepos = 0;
		files[accepted].zip_index = (int)i;
		files[accepted].compressed = true;
		++accepted;
	}
	mz_zip_reader_end(&archive);
	fclose(file);
	if (!accepted)
	{
		Z_Free(files);
		Con_Printf("PK3 ignored: %s contains no usable files\n", packfile);
		return NULL;
	}
	if (Sys_FileOpenRead(packfile, &handle) < 0)
	{
		Z_Free(files);
		return NULL;
	}
	pack = Z_Malloc(sizeof(*pack));
	q_strlcpy(pack->filename, packfile, sizeof(pack->filename));
	pack->handle = handle;
	pack->numfiles = accepted;
	pack->type = PACK_TYPE_PK3;
	pack->files = files;
	return pack;
fail:
	mz_zip_reader_end(&archive);
	fclose(file);
	Con_Printf("PK3 ignored: %s has an invalid directory\n", packfile);
	return NULL;
}

FILE *FS_Pk3OpenFile(const pack_t *pack, const packfile_t *entry)
{
	mz_zip_archive archive;
	FILE *source, *result;
	void *data;
	size_t size = 0;

	source = Sys_fopen(pack->filename, "rb");
	if (!source)
		return NULL;
	memset(&archive, 0, sizeof(archive));
	archive.m_pIO_opaque = source;
	archive.m_pRead = FS_Pk3Read;
	if (!mz_zip_reader_init(&archive, (mz_uint64)FS_Pk3FileLength(source), 0))
		goto fail;
	data = mz_zip_reader_extract_to_heap(&archive, (mz_uint)entry->zip_index, &size, 0);
	if (!data || size != (size_t)entry->filelen)
		goto fail_end;
	result = tmpfile();
	if (!result || fwrite(data, 1, size, result) != size)
	{
		if (result) fclose(result);
		free(data);
		goto fail_end;
	}
	free(data);
	mz_zip_reader_end(&archive);
	fclose(source);
	rewind(result);
	return result;
fail_end:
	mz_zip_reader_end(&archive);
fail:
	fclose(source);
	Con_Printf("PK3 read failed: %s in %s\n", entry->name, pack->filename);
	return NULL;
}