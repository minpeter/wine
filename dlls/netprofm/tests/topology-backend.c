/*
 * Unsupported topology backend fixture for the netprofm integration tests.
 *
 * Copyright 2026 Peter Min
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifdef NETPROFM_UNSUPPORTED_BACKEND

/* Compile the real Unix provider's unsupported branch, including its WoW64
 * table. No production test switch or public API replacement is needed. */
#include "config.h"
#undef HAVE_LINUX_RTNETLINK_H
#undef SONAME_LIBDBUS_1
#include "../unixlib.c"

#else

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *dlopen( const char *path, int flags )
{
    void *(*open_library)( const char *, int ) = dlsym( RTLD_NEXT, "dlopen" );
    const char *backend = getenv( "WINETEST_NETPROFM_BACKEND" );
    const char *name = path ? strrchr( path, '/' ) : NULL;
    void *handle;

    if (backend && name && !strcmp( name + 1, "netprofm.so" ))
    {
        /* A failed fixture load must not masquerade as unsupported capability. */
        if (!(handle = open_library( backend, flags )))
        {
            fprintf( stderr, "failed to load topology fixture: %s\n", dlerror() );
            abort();
        }
        return handle;
    }
    return open_library( path, flags );
}

#endif
