/*
 * Network List Manager Unix reachability provider
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <dlfcn.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#ifdef SONAME_LIBDBUS_1
# include <dbus/dbus.h>
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"
#include "wine/unixlib.h"
#include "unixlib.h"

#ifdef SONAME_LIBDBUS_1

WINE_DEFAULT_DEBUG_CHANNEL(netprofm);

#define DBUS_FUNCS                                      \
    DO_FUNC(dbus_bus_add_match);                        \
    DO_FUNC(dbus_bus_get_private);                      \
    DO_FUNC(dbus_connection_add_filter);                \
    DO_FUNC(dbus_connection_close);                     \
    DO_FUNC(dbus_connection_read_write_dispatch);       \
    DO_FUNC(dbus_connection_send_with_reply_and_block); \
    DO_FUNC(dbus_connection_set_exit_on_disconnect);    \
    DO_FUNC(dbus_connection_unref);                     \
    DO_FUNC(dbus_error_free);                           \
    DO_FUNC(dbus_error_init);                           \
    DO_FUNC(dbus_message_get_args);                     \
    DO_FUNC(dbus_message_get_path);                     \
    DO_FUNC(dbus_message_is_signal);                    \
    DO_FUNC(dbus_message_iter_append_basic);            \
    DO_FUNC(dbus_message_iter_get_arg_type);            \
    DO_FUNC(dbus_message_iter_get_basic);               \
    DO_FUNC(dbus_message_iter_init);                    \
    DO_FUNC(dbus_message_iter_init_append);             \
    DO_FUNC(dbus_message_iter_next);                    \
    DO_FUNC(dbus_message_iter_recurse);                 \
    DO_FUNC(dbus_message_new_method_call);              \
    DO_FUNC(dbus_message_unref);                        \
    DO_FUNC(dbus_threads_init_default)

#define DO_FUNC(f) static typeof(f) *p_##f
DBUS_FUNCS;
#undef DO_FUNC

struct reachability_context
{
    DBusConnection *connection;
    enum reachability_state state;
    BOOL refresh;
    unsigned int retry_count;
    unsigned int reconnect_count;
};

static pthread_once_t dbus_once = PTHREAD_ONCE_INIT;
static BOOL dbus_loaded;

static void load_dbus_functions_once(void)
{
    void *handle;

    if (!(handle = dlopen( SONAME_LIBDBUS_1, RTLD_NOW ))) goto failed;

#define DO_FUNC(f) if (!(p_##f = dlsym( handle, #f ))) goto failed
    DBUS_FUNCS;
#undef DO_FUNC
    dbus_loaded = TRUE;
    return;

failed:
    WARN( "failed to load D-Bus support: %s\n", dlerror() );
}

static BOOL load_dbus_functions(void)
{
    pthread_once( &dbus_once, load_dbus_functions_once );
    return dbus_loaded;
}

static const char *next_dict_entry( DBusMessageIter *iter, DBusMessageIter *variant )
{
    DBusMessageIter entry;
    const char *name;

    if (p_dbus_message_iter_get_arg_type( iter ) != DBUS_TYPE_DICT_ENTRY) return NULL;
    p_dbus_message_iter_recurse( iter, &entry );
    p_dbus_message_iter_next( iter );
    p_dbus_message_iter_get_basic( &entry, &name );
    p_dbus_message_iter_next( &entry );
    p_dbus_message_iter_recurse( &entry, variant );
    return name;
}

static enum reachability_state read_reachability( struct reachability_context *context )
{
    static const char service[] = "org.freedesktop.NetworkManager";
    static const char path[] = "/org/freedesktop/NetworkManager";
    static const char properties[] = "org.freedesktop.DBus.Properties";
    static const char interface[] = "org.freedesktop.NetworkManager";
    DBusMessageIter iter, variant;
    DBusMessage *request, *reply;
    dbus_uint32_t connectivity = 0;
    dbus_bool_t available = FALSE, enabled = FALSE;
    BOOL have_connectivity = FALSE, have_available = FALSE, have_enabled = FALSE;
    DBusError error;
    const char *name;

    request = p_dbus_message_new_method_call( service, path, properties, "GetAll" );
    if (!request) return REACHABILITY_INDETERMINATE;
    p_dbus_message_iter_init_append( request, &iter );
    name = interface;
    p_dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &name );

    p_dbus_error_init( &error );
    reply = p_dbus_connection_send_with_reply_and_block( context->connection, request, 1000, &error );
    p_dbus_message_unref( request );
    if (!reply)
    {
        p_dbus_error_free( &error );
        return REACHABILITY_INDETERMINATE;
    }
    p_dbus_error_free( &error );

    if (p_dbus_message_iter_init( reply, &iter ) &&
        p_dbus_message_iter_get_arg_type( &iter ) == DBUS_TYPE_ARRAY)
    {
        p_dbus_message_iter_recurse( &iter, &iter );
        while ((name = next_dict_entry( &iter, &variant )))
        {
            if (!strcmp( name, "Connectivity" ) &&
                p_dbus_message_iter_get_arg_type( &variant ) == DBUS_TYPE_UINT32)
            {
                p_dbus_message_iter_get_basic( &variant, &connectivity );
                have_connectivity = TRUE;
            }
            else if (!strcmp( name, "ConnectivityCheckAvailable" ) &&
                     p_dbus_message_iter_get_arg_type( &variant ) == DBUS_TYPE_BOOLEAN)
            {
                p_dbus_message_iter_get_basic( &variant, &available );
                have_available = TRUE;
            }
            else if (!strcmp( name, "ConnectivityCheckEnabled" ) &&
                     p_dbus_message_iter_get_arg_type( &variant ) == DBUS_TYPE_BOOLEAN)
            {
                p_dbus_message_iter_get_basic( &variant, &enabled );
                have_enabled = TRUE;
            }
        }
    }
    p_dbus_message_unref( reply );

    if (!have_connectivity || !have_available || !have_enabled || !available || !enabled || !connectivity)
        return REACHABILITY_INDETERMINATE;
    if (connectivity == 4) return REACHABILITY_ONLINE;
    if (connectivity <= 3) return REACHABILITY_OFFLINE;
    return REACHABILITY_INDETERMINATE;
}

static DBusHandlerResult reachability_filter( DBusConnection *connection, DBusMessage *message, void *data )
{
    struct reachability_context *context = data;
    const char *name, *old_owner, *new_owner;
    DBusError error;

    if (p_dbus_message_is_signal( message, "org.freedesktop.DBus.Properties", "PropertiesChanged" ) &&
        p_dbus_message_get_path( message ) &&
        !strcmp( p_dbus_message_get_path( message ), "/org/freedesktop/NetworkManager" ))
    {
        context->refresh = TRUE;
    }
    else if (p_dbus_message_is_signal( message, "org.freedesktop.DBus", "NameOwnerChanged" ))
    {
        p_dbus_error_init( &error );
        if (p_dbus_message_get_args( message, &error, DBUS_TYPE_STRING, &name,
                                     DBUS_TYPE_STRING, &old_owner, DBUS_TYPE_STRING, &new_owner,
                                     DBUS_TYPE_INVALID ) &&
            !strcmp( name, "org.freedesktop.NetworkManager" ))
            context->refresh = TRUE;
        p_dbus_error_free( &error );
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void disconnect_reachability( struct reachability_context *context )
{
    if (!context->connection) return;
    p_dbus_connection_close( context->connection );
    p_dbus_connection_unref( context->connection );
    context->connection = NULL;
}

static BOOL add_match( DBusConnection *connection, const char *match )
{
    DBusError error;

    p_dbus_error_init( &error );
    p_dbus_bus_add_match( connection, match, &error );
    if (error.name)
    {
        WARN( "failed to add D-Bus match %s: %s\n", debugstr_a(match), debugstr_a(error.message) );
        p_dbus_error_free( &error );
        return FALSE;
    }
    p_dbus_error_free( &error );
    return TRUE;
}

static BOOL connect_reachability( struct reachability_context *context )
{
    static const char properties_match[] = "type='signal',interface='org.freedesktop.DBus.Properties',"
                                           "member='PropertiesChanged',path='/org/freedesktop/NetworkManager'";
    static const char owner_match[] = "type='signal',interface='org.freedesktop.DBus',"
                                      "member='NameOwnerChanged',arg0='org.freedesktop.NetworkManager'";
    DBusError error;

    p_dbus_error_init( &error );
    context->connection = p_dbus_bus_get_private( DBUS_BUS_SYSTEM, &error );
    p_dbus_error_free( &error );
    if (!context->connection) return FALSE;

    p_dbus_connection_set_exit_on_disconnect( context->connection, FALSE );
    if (!p_dbus_connection_add_filter( context->connection, reachability_filter, context, NULL ))
        goto failed;

    if (!add_match( context->connection, properties_match ) ||
        !add_match( context->connection, owner_match )) goto failed;
    context->reconnect_count = 0;
    return TRUE;

failed:
    disconnect_reachability( context );
    return FALSE;
}

static NTSTATUS reachability_start( void *args )
{
    struct reachability_start_params *params = args;
    struct reachability_context *context;

    if (!load_dbus_functions()) return STATUS_NOT_SUPPORTED;
    p_dbus_threads_init_default();
    if (!(context = calloc( 1, sizeof(*context) ))) return STATUS_NO_MEMORY;

    context->state = connect_reachability( context ) ? read_reachability( context )
                                                     : REACHABILITY_INDETERMINATE;
    params->state = context->state;
    params->handle = (UINT_PTR)context;
    return STATUS_SUCCESS;
}

static NTSTATUS reachability_wait( void *args )
{
    struct reachability_wait_params *params = args;
    struct reachability_context *context = (void *)(UINT_PTR)params->handle;
    enum reachability_state state;
    BOOL retry = FALSE;

    context->refresh = FALSE;
    if (!context->connection)
    {
        poll( NULL, 0, 250 );
        state = REACHABILITY_INDETERMINATE;
        if (++context->reconnect_count >= 4)
        {
            context->reconnect_count = 0;
            if (connect_reachability( context )) state = read_reachability( context );
        }
        goto done;
    }
    if (context->state == REACHABILITY_INDETERMINATE && ++context->retry_count >= 4)
    {
        context->retry_count = 0;
        retry = TRUE;
    }
    if (!p_dbus_connection_read_write_dispatch( context->connection, 250 ))
    {
        disconnect_reachability( context );
        state = REACHABILITY_INDETERMINATE;
    }
    else if (context->refresh || retry) state = read_reachability( context );
    else state = context->state;

done:
    params->changed = state != context->state;
    params->state = context->state = state;
    return STATUS_SUCCESS;
}

static NTSTATUS reachability_stop( void *args )
{
    const struct reachability_stop_params *params = args;
    struct reachability_context *context = (void *)(UINT_PTR)params->handle;

    disconnect_reachability( context );
    free( context );
    return STATUS_SUCCESS;
}

#else /* SONAME_LIBDBUS_1 */

static NTSTATUS reachability_start( void *args )
{
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS reachability_wait( void *args )
{
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS reachability_stop( void *args )
{
    return STATUS_NOT_SUPPORTED;
}

#endif /* SONAME_LIBDBUS_1 */

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    reachability_start,
    reachability_wait,
    reachability_stop,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );
