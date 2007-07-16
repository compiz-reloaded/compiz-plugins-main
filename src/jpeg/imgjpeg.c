/*
 * beryl-plugins::jpeg.c - adds JPEG image support to beryl.
 * Copyright: (C) 2006 Nicholas Thomas
 *		       Danny Baumann (JPEG writing, option stuff)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <compiz.h>
#include <jpeglib.h>
#include "imgjpeg_options.h"

static int displayPrivateIndex;

typedef struct _JPEGDisplay
{
	FileToImageProc fileToImage;
	ImageToFileProc imageToFile;
} JPEGDisplay;

#define GET_JPEG_DISPLAY(d)				    \
    ((JPEGDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define JPEG_DISPLAY(d)			 \
    JPEGDisplay *jd = GET_JPEG_DISPLAY (d)

#define NUM_OPTIONS(d) (sizeof ((d)->opt) / sizeof (CompOption))

static Bool rgbToBGRA(const JSAMPLE * source, void **data, int height,
					  int width, int alpha)
{
	int h, w;
	char *dest;

	dest = malloc(height * width * 4);
	if (!dest)
		return FALSE;

	*data = dest;

	for (h = 0; h < height; h++)
		for (w = 0; w < width; w++)
		{
			dest[((h * width + w) * 4) + 0] = source[((h * width + w) * 3) + 2];	//B
			dest[((h * width + w) * 4) + 1] = source[((h * width + w) * 3) + 1];	//G
			dest[((h * width + w) * 4) + 2] = source[((h * width + w) * 3) + 0];	//R
			dest[((h * width + w) * 4) + 3] = alpha;
		}

	return TRUE;
}

static Bool rgbaToRGB(char *source, JSAMPLE ** dest, int height, int width,
					  int stride)
{
	int h, w;
	int ps = stride / width;	/* pixel size */
	JSAMPLE *d;

	d = malloc(height * width * 3 * sizeof(JSAMPLE));
	if (!d)
		return FALSE;

	*dest = d;

	for (h = 0; h < height; h++)
		for (w = 0; w < width; w++)
		{
			d[((h * width + w) * 3) + 0] = source[((h * width + w) * ps) + 0];	//R
			d[((h * width + w) * 3) + 1] = source[((h * width + w) * ps) + 1];	//G
			d[((h * width + w) * 3) + 2] = source[((h * width + w) * ps) + 2];	//B
		}

	return TRUE;
}

static Bool readJPEGFileToImage(FILE * file, int *width, int *height,
								void **data)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPLE *buf;
	JSAMPROW *rows;
	int i;

	if (!file)
		return FALSE;

	cinfo.err = jpeg_std_error(&jerr);

	jpeg_create_decompress(&cinfo);

	jpeg_stdio_src(&cinfo, file);
	jpeg_read_header(&cinfo, TRUE);

	cinfo.out_color_space = JCS_RGB;

	jpeg_start_decompress(&cinfo);

	*height = cinfo.output_height;
	*width = cinfo.output_width;

	buf = malloc(cinfo.output_height * cinfo.output_width *
				 cinfo.output_components * sizeof(JSAMPLE));
	if (!buf)
		return FALSE;

	rows = malloc(cinfo.output_height * sizeof(JSAMPROW));
	if (!rows)
	{
		free(buf);
		return FALSE;
	}

	for (i = 0; i < cinfo.output_height; i++)
		rows[i] = &buf[i * cinfo.output_width * cinfo.output_components];

	while (cinfo.output_scanline < cinfo.output_height)
		jpeg_read_scanlines(&cinfo, &rows[cinfo.output_scanline],
							cinfo.output_height - cinfo.output_scanline);

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	// convert the rgb data into BGRA format
	if (!rgbToBGRA(buf, data, *height, *width, 255))
	{
		free(rows);
		free(buf);
		return FALSE;
	}

	free(rows);
	free(buf);
	return TRUE;
}

static Bool writeJPEG(CompDisplay * d, void *buffer, FILE * file,
					  int width, int height, int stride)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[1];
	JSAMPLE *data;

	// convert the rgb data into BGRA format
	if (!rgbaToRGB(buffer, &data, height, width, stride))
		return FALSE;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	jpeg_stdio_dest(&cinfo, file);

	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, imgjpegGetQuality(d), TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	while (cinfo.next_scanline < cinfo.image_height)
	{
		row_pointer[0] =
				&data[(cinfo.image_height - cinfo.next_scanline -
					   1) * width * 3];
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	free(data);

	return TRUE;
}

// Turns the path & name into a real, absolute path
// No extensions jiggery-pokery here, as JPEGs can be
// .jpg or .jpeg - or, indeed, whatever.
// Deals with the path, regardless of what it's passed.
static char *createFilename(const char *path, const char *name)
{
	int len;
	char *filename = NULL;

	if (path && !name)
		len = strlen(path) + 2;
	else if (!path && name)
		len = strlen(name) + 2;
	else if (path && name)
		len = strlen(path) + strlen(name) + 2;
	else
		return NULL;

	filename = malloc(len);
	if (!filename)
		return NULL;

	if (path && !name)
		sprintf(filename, "%s", path);
	else if (!path && name)
		sprintf(filename, "%s", name);
	else
		sprintf(filename, "%s/%s", path, name);

	return filename;
}

static Bool JPEGImageToFile(CompDisplay * d,
							const char *path,
							const char *name,
							const char *format,
							int width, int height, int stride, void *data)
{
	Bool status = FALSE;

	// Not a JPEG
	if (strcasecmp(format, "jpeg") != 0 && strcasecmp(format, "jpg") != 0)
	{
		JPEG_DISPLAY(d);
		UNWRAP(jd, d, imageToFile);
		status = (*d->imageToFile) (d, path, name, format, width, height,
									stride, data);
		WRAP(jd, d, imageToFile, JPEGImageToFile);
		return status;
	}

	// Is a JPEG
	char *filename = createFilename(path, name);

	if (!filename)
		return FALSE;

	FILE *file = fopen(filename, "wb");

	if (file)
	{
		status = writeJPEG(d, data, file, width, height, stride);
		fclose(file);
	}

	free(filename);
	return status;
}

static Bool JPEGFileToImage(CompDisplay * d,
							const char *path,
							const char *name,
							int *width, int *height, int *stride, void **data)
{
	Bool status = FALSE;

	char *filename = createFilename(path, name);

	if (!filename)
		return FALSE;

	// Do some testing here to see if it's got a .jpg or .jpeg extension
	char *extension = strrchr(filename, '.');

	if (extension)
	{
		if (strcasecmp(extension, ".jpeg") == 0 ||
			strcasecmp(extension, ".jpg") == 0)
		{
			FILE *file = fopen(filename, "rb");

			if (file)
			{
				status = readJPEGFileToImage(file, width, height, data);
				fclose(file);

				if (status)		// Success!
				{
					free(filename);
					*stride = *width * 4;
					return TRUE;
				}
			}
		}
	}

	free(filename);

	// Isn't a JPEG - pass to the next in the chain.
	JPEG_DISPLAY(d);

	UNWRAP(jd, d, fileToImage);
	status = (*d->fileToImage) (d, path, name, width, height, stride, data);
	WRAP(jd, d, fileToImage, JPEGFileToImage);

	return status;
}

static Bool JPEGInitDisplay(CompPlugin * p, CompDisplay * d)
{
	JPEGDisplay *jd;

	jd = malloc(sizeof(JPEGDisplay));
	if (!jd)
		return FALSE;

	WRAP(jd, d, fileToImage, JPEGFileToImage);
	WRAP(jd, d, imageToFile, JPEGImageToFile);

	d->privates[displayPrivateIndex].ptr = jd;

	return TRUE;
}

static void JPEGFiniDisplay(CompPlugin * p, CompDisplay * d)
{
	JPEG_DISPLAY(d);

	UNWRAP(jd, d, fileToImage);
	UNWRAP(jd, d, imageToFile);

	free(jd);
}


static Bool JPEGInit(CompPlugin * p)
{
	displayPrivateIndex = allocateDisplayPrivateIndex();
	if (displayPrivateIndex < 0)
		return FALSE;

	return TRUE;
}

static void JPEGFini(CompPlugin * p)
{
	if (displayPrivateIndex >= 0)
		freeDisplayPrivateIndex(displayPrivateIndex);
}

static int JPEGGetVersion(CompPlugin * p, int version)
{
	return ABIVERSION;
}

CompPluginVTable JPEGVTable = {
	"imgjpeg",
	JPEGGetVersion,
	0,
	JPEGInit,
	JPEGFini,
	JPEGInitDisplay,
	JPEGFiniDisplay,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

CompPluginVTable *getCompPluginInfo(void)
{
	return &JPEGVTable;
}
