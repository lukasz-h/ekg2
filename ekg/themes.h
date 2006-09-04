/* $Id$ */

/*
 *  (C) Copyright 2001-2003 Wojtek Kaniewski <wojtekka@irc.pl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __EKG_THEMES_H
#define __EKG_THEMES_H

#include "gettext.h" 
#define _(a) gettext(a)
#define N_(a) gettext_noop(a)

#include "char.h"
#include "dynstuff.h"
#include "sessions.h"

struct format {
	CHAR_T *name;
	int name_hash;
	char *value;
};

typedef struct {
	CHAR_T *str;	/* znaki, ci�g zako�czony \0 */
	short *attr;	/* atrybuty, ci�g o d�ugo�ci strlen(str) */
	int ts;		/* timestamp */

	int prompt_len;	/* d�ugo�� promptu, kt�ry b�dzie powtarzany przy i
			   przej�ciu do kolejnej linii. */
	int prompt_empty;	/* prompt przy przenoszeniu b�dzie pusty */
	int margin_left; 	/* where the margin is set (on what char) */
	void *private;          /* can be helpfull */
} fstring_t;

extern list_t formats;

#ifndef __EKG_STUFF_H
extern int config_default_status_window;	/* deklaracja zeby nie trzeba bylo includowac calego stuff.h */
#endif

#define print(x...) print_window( (config_default_status_window) ? "__status" : "__current", NULL, 0, x) 
#define wcs_print(x...) wcs_print_window( (config_default_status_window) ? "__status" : "__current", NULL, 0, x)
#define print_status(x...) print_window("__status", NULL, 0, x)

#ifndef EKG2_WIN32_NOFUNCTION

void print_window(const char *target, session_t *session, int separate, const char *theme, ...);
void wcs_print_window(const char *target, session_t *session, int separate, const char *theme, ...);

int format_add(const char *name, const char *value, int replace);
const char *format_find(const char *name);
CHAR_T *wcs_format_find(const char *name);
char *format_string(const char *format, ...);
CHAR_T *wcs_format_string(const CHAR_T *format, ...);

void theme_init();
void theme_plugins_init();
int theme_read(const char *filename, int replace);
void theme_cache_reset();
void theme_free();

fstring_t *fstring_new(const char *str);
fstring_t *wcs_fstring_new(const CHAR_T *str);
void fstring_free(fstring_t *str);

#endif

/*
 * makro udaj�ce isalpha() z LC_CTYPE="pl_PL". niestety ncurses co� psuje
 * i �le wykrywa p�e�.
 */
#define isalpha_pl_PL(x) ((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�' || x == '�')

#endif /* __EKG_THEMES_H */

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
