/*
 * text.h - adds text image support to beryl.
 * Copyright: (C) 2006 Patrick Niklaus
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

#ifndef _COMPIZ_TEXT_H
#define _COMPIZ_TEXT_H

#define TEXT_ABIVERSION 20090103

#define CompTextFlagStyleBold      (1 << 0)
#define CompTextFlagStyleItalic    (1 << 1)
#define CompTextFlagEllipsized     (1 << 2)
#define CompTextFlagWithBackground (1 << 3)
#define CompTextFlagNoAutoBinding  (1 << 4)

typedef struct _CompTextAttrib {
    char           *family;
    int            size;
    unsigned short color[4];

    unsigned int   flags;

    int            maxWidth;
    int            maxHeight;

    int            bgHMargin;
    int            bgVMargin;
    unsigned short bgColor[4];
} CompTextAttrib;

typedef struct _CompTextData {
    CompTexture  *texture;
    Pixmap       pixmap;
    unsigned int width;
    unsigned int height;
} CompTextData;

typedef CompTextData *
(*RenderTextProc) (CompScreen           *s,
		   const char           *text,
		   const CompTextAttrib *attrib);

typedef CompTextData *
(*RenderWindowTitleProc) (CompScreen           *s,
			  Window               window,
			  Bool                 withViewportNumber,
			  const CompTextAttrib *attrib);

typedef void (*DrawTextProc) (CompScreen         *s,
			      const CompTextData *data,
			      float              x,
			      float              y,
			      float              alpha);

typedef void (*FiniTextDataProc) (CompScreen   *s,
				  CompTextData *data);

typedef struct _TextFunc {
    RenderTextProc        renderText;
    RenderWindowTitleProc renderWindowTitle;
    DrawTextProc          drawText;
    FiniTextDataProc      finiTextData;
} TextFunc;

#endif
