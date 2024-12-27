#include "unzipper.h"

#include <stdio.h>
#include <string.h>

#include "unzip.h"
# include <direct.h>
# include <io.h>

#define dir_delimter '/'
#define MAX_FILENAME 512
#define READ_SIZE 8192

int unzip(char* zipFileName, char* unzippedPath)
{
	char szUnzippedFileName[256];
	// Open the zip file
	unzFile zipfile = unzOpen(zipFileName);
	if (zipfile == NULL)
	{
		return -1;
	}

	// Get info about the zip file
	unz_global_info global_info;
	if (unzGetGlobalInfo(zipfile, &global_info) != UNZ_OK)
	{
		unzClose(zipfile);
		return -1;
	}

	// Buffer to hold data read from the zip file.
	char read_buffer[READ_SIZE];

	// Loop to extract all files
	uLong i;
	for (i = 0; i < global_info.number_entry; ++i)
	{
		// Get info about current file.
		unz_file_info file_info;
		char filename[MAX_FILENAME];
		if (unzGetCurrentFileInfo(
			zipfile,
			&file_info,
			filename,
			MAX_FILENAME,
			NULL, 0, NULL, 0) != UNZ_OK)
		{
			unzClose(zipfile);
			return -1;
		}

		// Check if this entry is a directory or file.
		const size_t filename_length = strlen(filename);
		if (filename[filename_length - 1] == dir_delimter)
		{
			// Entry is a directory, so create it.
			sprintf(szUnzippedFileName, "%s\\%s", unzippedPath, filename);
			_mkdir(szUnzippedFileName);
		}
		else
		{
			// Entry is a file, so extract it.
			if (unzOpenCurrentFile(zipfile) != UNZ_OK)
			{
				unzClose(zipfile);
				return -1;
			}

			// Open a file to write out the data.
			sprintf(szUnzippedFileName, "%s\\%s", unzippedPath, filename);
			FILE *out = fopen(szUnzippedFileName, "wb");
			if (out == NULL)
			{
				unzCloseCurrentFile(zipfile);
				unzClose(zipfile);
				return -1;
			}

			int error = UNZ_OK;
			do
			{
				error = unzReadCurrentFile(zipfile, read_buffer, READ_SIZE);
				if (error < 0)
				{
					printf("error %d\n", error);
					unzCloseCurrentFile(zipfile);
					unzClose(zipfile);
					return -1;
				}

				// Write data to file.
				if (error > 0)
				{
					fwrite(read_buffer, error, 1, out); // You should check return of fwrite...
				}
			} while (error > 0);

			fclose(out);
		}

		unzCloseCurrentFile(zipfile);

		// Go the the next entry listed in the zip file.
		if ((i + 1) < global_info.number_entry)
		{
			if (unzGoToNextFile(zipfile) != UNZ_OK)
			{
				unzClose(zipfile);
				return -1;
			}
		}
	}

	unzClose(zipfile);

	return 0;
}