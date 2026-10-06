/* Real NSI polling backend with faults at the Unix socket boundary.
 * Copyright 2026 Peter Min
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef NSI_FAULT_BACKEND
#include <sys/socket.h>
#include <linux/netlink.h>

static int notification_fd = -1;

static void marker( const char *name )
{
    char path[1024];
    FILE *file;
    snprintf( path, sizeof(path), "%s/%s", getenv( "WINETEST_NSI_DIR" ), name );
    if (!(file = fopen( path, "w" ))) abort();
    fclose( file );
}

static void wait_fault(void)
{
    char path[1024];
    snprintf( path, sizeof(path), "%s/go", getenv( "WINETEST_NSI_DIR" ) );
    marker( "polling" );
    while (access( path, F_OK )) usleep( 10000 );
}

static int fault_socket( int domain, int type, int protocol )
{
    if (domain == PF_NETLINK && protocol == NETLINK_ROUTE)
    {
        if (!strcmp( getenv( "WINETEST_NSI_FAULT" ), "socket" ))
        {
            wait_fault();
            marker( "injected" );
            errno = EMFILE;
            return -1;
        }
        notification_fd = socket( domain, type, protocol );
        return notification_fd;
    }
    return socket( domain, type, protocol );
}

static ssize_t fault_recv( int fd, void *buffer, size_t len, int flags )
{
    if (fd == notification_fd)
    {
        marker( "polling" );
        if (strcmp( getenv( "WINETEST_NSI_FAULT" ), "control" ))
        {
            wait_fault();
            marker( "injected" );
            errno = ENOBUFS;
            return -1;
        }
    }
    return recv( fd, buffer, len, flags );
}

static int fault_close( int fd )
{
    if (fd == notification_fd)
    {
        marker( "closed" );
        notification_fd = -1;
    }
    return close( fd );
}

#define socket fault_socket
#define recv fault_recv
#define close fault_close
#include "../../nsiproxy.sys/nsi.c"

#else

void *dlopen( const char *path, int flags )
{
    void *(*open_library)( const char *, int ) = dlsym( RTLD_NEXT, "dlopen" );
    const char *backend = getenv( "WINETEST_NSI_BACKEND" );
    const char *name = path ? strrchr( path, '/' ) : NULL;
    void *handle;

    if (backend && name && !strcmp( name + 1, "nsiproxy.so" ))
    {
        if (!(handle = open_library( backend, flags )))
        {
            fprintf( stderr, "failed to load NSI fixture: %s\n", dlerror() );
            abort();
        }
        return handle;
    }
    return open_library( path, flags );
}
#endif
