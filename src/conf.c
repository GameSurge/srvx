/* conf.c - Config file reader
 * Copyright 2000-2004 srvx Development Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.  Important limitations are
 * listed in the COPYING file that accompanies this software.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, email srvx-maintainers@srvx.net.
 */

#include "conf.h"
#include "log.h"

static dict_t conf_db;
static conf_reload_func *reload_funcs;
static int num_rfs, size_rfs;

void
conf_register_reload(conf_reload_func crf)
{
    if (num_rfs >= size_rfs) {
        if (reload_funcs) {
            size_rfs <<= 1;
            reload_funcs = realloc(reload_funcs, size_rfs*sizeof(conf_reload_func));
        } else {
            size_rfs = 8;
            reload_funcs = calloc(size_rfs, sizeof(conf_reload_func));
        }
    }
    reload_funcs[num_rfs++] = crf;
    if (conf_db) {
        crf();
    }
}

void
conf_call_reload_funcs(void)
{
    int i;
    for (i=0; i<num_rfs; i++) reload_funcs[i]();
}

int
conf_read(const char *conf_file_name)
{
    dict_t old_conf = conf_db;
    if (!(conf_db = parse_database(conf_file_name))) {
        goto fail;
    }
    if (reload_funcs) {
        conf_call_reload_funcs();
    }
    if (old_conf && old_conf != conf_db) {
        free_database(old_conf);
    }
    return 1;

fail:
    log_module(MAIN_LOG, LOG_ERROR, "Reverting to previous configuration.");
    free_database(conf_db);
    conf_db = old_conf;
    return 0;
}

void
conf_close(void)
{
    free_database(conf_db);
    free(reload_funcs);
}

struct record_data *
conf_get_node(const char *full_path)
{
    return database_get_path(conf_db, full_path);
}

void *
conf_get_data(const char *full_path, enum recdb_type type)
{
    return database_get_data(conf_db, full_path, type);
}

const char*
conf_enum_root(dict_iterator_f it, void *extra)
{
    return dict_foreach(conf_db, it, extra);
}
