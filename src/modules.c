/* modules.c - Compiled-in module support
 * Copyright 2002-2003 srvx Development Team
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

#include "log.h"
#include "modules.h"

enum init_state {
    UNINIT,
    WORKING,
    BROKEN,
    INITED,
    DONE
};

struct cmodule {
    const char *name;
    int (*init_func)(void);
    int (*finalize_func)(void);
    const char **deps;
    enum init_state state;
};

#define WITH_MODULE(x) extern int x##_init(void); extern int x##_finalize(void); extern const char *x##_module_deps[];
#include "modules-list.h"
#undef WITH_MODULE

static struct cmodule cmodules[] = {
#define WITH_MODULE(x) { #x, x##_init, x##_finalize, x##_module_deps, UNINIT },
#include "modules-list.h"
#undef WITH_MODULE
    /* Placeholder at end of array */
    { NULL, NULL, NULL, NULL, UNINIT }
};

static int
modules_bsearch(const void *a, const void *b) {
    const char *key = a;
    const struct cmodule *cmod = b;
    return irccasecmp(key, cmod->name);
}

static int
modules_qsort(const void *a, const void *b) {
    const struct cmodule *ca = a, *cb = b;
    return irccasecmp(ca->name, cb->name);
}

static int
module_init(struct cmodule *cmod, int final) {
    unsigned int ii;
    struct cmodule *dep;

    switch (cmod->state) {
    case UNINIT: break;
    case INITED: if (!final) return 1; break;
    case DONE:   return 1;
    case BROKEN: return 0;
    case WORKING:
        log_module(MAIN_LOG, LOG_ERROR, "Tried to recursively enable code module %s.", cmod->name);
        return 0;
    }
    cmod->state = WORKING;
    for (ii=0; cmod->deps[ii]; ++ii) {
        dep = bsearch(cmod->deps[ii], cmodules, ArrayLength(cmodules)-1, sizeof(cmodules[0]), modules_bsearch);
        if (!dep) {
            log_module(MAIN_LOG, LOG_ERROR, "Code module %s depends on unknown module %s.", cmod->name, cmod->deps[ii]);
            cmod->state = BROKEN;
            return 0;
        }
        if (!module_init(dep, final)) {
            log_module(MAIN_LOG, LOG_ERROR, "Failed to initialize dependency %s of code module %s.", dep->name, cmod->name);
            cmod->state = BROKEN;
            return 0;
        }
    }
    if (final) {
        if (!cmod->finalize_func()) {
            log_module(MAIN_LOG, LOG_ERROR, "Failed to finalize code module %s.", cmod->name);
            cmod->state = BROKEN;
            return 0;
        }
        cmod->state = DONE;
        return 1;
    } else {
        if (!cmod->init_func()) {
            log_module(MAIN_LOG, LOG_ERROR, "Failed to initialize code module %s.", cmod->name);
            cmod->state = BROKEN;
            return 0;
        }
        cmod->state = INITED;
        return 1;
    }
}

void
modules_init(void) {
    unsigned int ii;

    qsort(cmodules, ArrayLength(cmodules)-1, sizeof(cmodules[0]), modules_qsort);
    for (ii=0; cmodules[ii].name; ++ii) {
        if (cmodules[ii].state != UNINIT) continue;
        module_init(cmodules + ii, 0);
        if (cmodules[ii].state != INITED) {
            log_module(MAIN_LOG, LOG_WARNING, "Code module %s not properly initialized.", cmodules[ii].name);
        }
    }
}

void
modules_finalize(void) {
    unsigned int ii;

    for (ii=0; cmodules[ii].name; ++ii) {
        if (cmodules[ii].state != INITED) continue;
        module_init(cmodules + ii, 1);
        if (cmodules[ii].state != DONE) {
            log_module(MAIN_LOG, LOG_WARNING, "Code module %s not properly finalized.", cmodules[ii].name);
        }
    }
}
