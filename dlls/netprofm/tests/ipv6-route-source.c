/*
 * IPv6 route-source fault injection for the netprofm integration tests.
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

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *open_route_source( const char *func, const char *path, const char *mode )
{
    FILE *(*open_file)( const char *, const char * ) = dlsym( RTLD_NEXT, func );
    const char *state = getenv( "WINETEST_NETPROFM_IPV6_ROUTES" );

    if (state && !strcmp( path, "/proc/net/ipv6_route" ))
    {
        if (!strcmp( state, "unavailable" ))
        {
            errno = ENOENT;
            return NULL;
        }
        if (!strcmp( state, "empty" )) path = "/dev/null";
    }
    return open_file( path, mode );
}

FILE *fopen( const char *path, const char *mode )
{
    return open_route_source( "fopen", path, mode );
}

FILE *fopen64( const char *path, const char *mode )
{
    return open_route_source( "fopen64", path, mode );
}
