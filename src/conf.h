/* conf.h - Config file reader
 * Copyright 2000-2004 srvx Development Team
 *
 * This file is part of srvx.
 *
 * srvx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with srvx; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#ifndef CONF_H
#define CONF_H

#include "recdb.h"

int conf_read(const char *conf_file_name);
void conf_close(void);

typedef void (*conf_reload_func)(void);
void conf_register_reload(conf_reload_func crf);
void conf_call_reload_funcs(void);

void *conf_get_data(const char *full_path, enum recdb_type type);
struct record_data *conf_get_node(const char *full_path);
const char *conf_enum_root(dict_iterator_f it, void *extra);

#endif
