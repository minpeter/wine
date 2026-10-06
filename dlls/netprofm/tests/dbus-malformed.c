/* Deterministic transport fixture for the real Unix provider. The message
 * encoder/iterators are libdbus, not mocks. Run with adversarial.sh.
 *
 * Copyright 2026 Peter Min
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <assert.h>
#include <stdio.h>
#define __wine_unix_call_funcs original_funcs
#define __wine_unix_call_wow64_funcs original_wow64_funcs
#ifndef PROVIDER_SOURCE
#define PROVIDER_SOURCE "../unixlib.c"
#endif
#include PROVIDER_SOURCE
#undef __wine_unix_call_funcs
#undef __wine_unix_call_wow64_funcs

static DBusMessage *response, *signal_message;
static const char *current_owner = ":1.10";
static unsigned int getalls, checks;
static struct reachability_context *dispatch_context;
static BOOL signal_during_reply;

#define check(expr) do { checks++; if (!(expr)) { \
    fprintf( stderr, "D-Bus check failed at %u: %s\n", __LINE__, #expr ); abort(); } } while (0)

static DBusMessage *send_reply( DBusConnection *connection, DBusMessage *request, int timeout, DBusError *error )
{
    DBusMessage *reply;

    if (dbus_message_is_method_call( request, "org.freedesktop.DBus", "GetNameOwner" ))
    {
        reply = dbus_message_new( DBUS_MESSAGE_TYPE_METHOD_RETURN );
        assert(dbus_message_append_args( reply, DBUS_TYPE_STRING, &current_owner, DBUS_TYPE_INVALID ));
        return reply;
    }
    check(dbus_message_is_method_call( request, "org.freedesktop.DBus.Properties", "GetAll" ));
    check(!strcmp( dbus_message_get_destination( request ), current_owner ));
    check(!dbus_message_get_auto_start( request ));
    getalls++;
    if (signal_during_reply)
    {
        signal_during_reply = FALSE;
        reachability_filter( connection, signal_message, dispatch_context );
    }
    return dbus_message_ref( response );
}

static dbus_bool_t dispatch( DBusConnection *connection, int timeout )
{
    if (signal_message)
    {
        reachability_filter( connection, signal_message, dispatch_context );
        dbus_message_unref( signal_message );
        signal_message = NULL;
    }
    return TRUE;
}

static void property( DBusMessageIter *array, const char *name, int type, const void *value )
{
    DBusMessageIter entry, variant;
    char signature[] = {type, 0};

    assert(dbus_message_iter_open_container( array, DBUS_TYPE_DICT_ENTRY, NULL, &entry ));
    assert(dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &name ));
    assert(dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, signature, &variant ));
    assert(dbus_message_iter_append_basic( &variant, type, value ));
    assert(dbus_message_iter_close_container( &entry, &variant ));
    assert(dbus_message_iter_close_container( array, &entry ));
}

static DBusMessage *properties( unsigned int connectivity, unsigned int wrong, unsigned int missing )
{
    DBusMessage *message = dbus_message_new( DBUS_MESSAGE_TYPE_METHOD_RETURN );
    DBusMessageIter iter, array;
    dbus_uint32_t value = connectivity;
    dbus_bool_t enabled = TRUE;
    const char *text = "not a number";

    dbus_message_iter_init_append( message, &iter );
    assert(dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "{sv}", &array ));
    if (!(missing & 1)) property( &array, "Connectivity", wrong & 1 ? DBUS_TYPE_STRING : DBUS_TYPE_UINT32,
                                 wrong & 1 ? (void *)&text : (void *)&value );
    if (!(missing & 2)) property( &array, "ConnectivityCheckAvailable", wrong & 2 ? DBUS_TYPE_UINT32 : DBUS_TYPE_BOOLEAN,
                                 wrong & 2 ? (void *)&value : (void *)&enabled );
    if (!(missing & 4)) property( &array, "ConnectivityCheckEnabled", wrong & 4 ? DBUS_TYPE_STRING : DBUS_TYPE_BOOLEAN,
                                 wrong & 4 ? (void *)&text : (void *)&enabled );
    if (wrong & 8) property( &array, "Connectivity", DBUS_TYPE_STRING, &text );
    if (wrong & 16) property( &array, "ConnectivityCheckAvailable", DBUS_TYPE_UINT32, &value );
    if (wrong & 32) property( &array, "ConnectivityCheckEnabled", DBUS_TYPE_STRING, &text );
    property( &array, "UnrelatedProperty", DBUS_TYPE_STRING, &text );
    assert(dbus_message_iter_close_container( &iter, &array ));
    return message;
}

static DBusMessage *bad_dictionary( const char *signature )
{
    DBusMessage *message = dbus_message_new( DBUS_MESSAGE_TYPE_METHOD_RETURN );
    DBusMessageIter iter, array, entry, variant;
    const char *text = "Connectivity";
    dbus_uint32_t number = 1;

    dbus_message_iter_init_append( message, &iter );
    assert(dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, signature, &array ));
    assert(dbus_message_iter_open_container( &array, DBUS_TYPE_DICT_ENTRY, NULL, &entry ));
    assert(dbus_message_iter_append_basic( &entry, signature[1], signature[1] == 's' ? (void *)&text : (void *)&number ));
    if (signature[2] == 'v')
    {
        assert(dbus_message_iter_open_container( &entry, DBUS_TYPE_VARIANT, "u", &variant ));
        assert(dbus_message_iter_append_basic( &variant, DBUS_TYPE_UINT32, &number ));
        assert(dbus_message_iter_close_container( &entry, &variant ));
    }
    else assert(dbus_message_iter_append_basic( &entry, DBUS_TYPE_STRING, &text ));
    assert(dbus_message_iter_close_container( &array, &entry ));
    assert(dbus_message_iter_close_container( &iter, &array ));
    return message;
}

static DBusMessage *properties_signal( const char *sender, const char *interface, unsigned int malformed )
{
    DBusMessage *message = dbus_message_new_signal( "/org/freedesktop/NetworkManager",
                                                  "org.freedesktop.DBus.Properties", "PropertiesChanged" );
    DBusMessageIter iter, array;
    dbus_uint32_t number = 1;

    if (sender) assert(dbus_message_set_sender( message, sender ));
    if (malformed == 2) return message;
    dbus_message_iter_init_append( message, &iter );
    if (malformed == 3) assert(dbus_message_iter_append_basic( &iter, DBUS_TYPE_UINT32, &number ));
    else assert(dbus_message_iter_append_basic( &iter, DBUS_TYPE_STRING, &interface ));
    if (malformed != 1)
    {
        assert(dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, malformed == 4 ? "{uv}" : "{sv}", &array ));
        assert(dbus_message_iter_close_container( &iter, &array ));
        assert(dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, malformed == 5 ? "u" : "s", &array ));
        assert(dbus_message_iter_close_container( &iter, &array ));
    }
    return message;
}

static DBusMessage *owner_signal( const char *sender, BOOL malformed )
{
    const char *name = "org.freedesktop.NetworkManager", *old = ":1.10", *next = ":1.11";
    DBusMessage *message = dbus_message_new_signal( "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged" );
    assert(dbus_message_set_sender( message, sender ));
    assert(dbus_message_append_args( message, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &old, DBUS_TYPE_INVALID ));
    if (!malformed) assert(dbus_message_append_args( message, DBUS_TYPE_STRING, &next, DBUS_TYPE_INVALID ));
    return message;
}

static void test_signal( struct reachability_context *context, DBusMessage *message,
                         unsigned int requests, enum reachability_state state, BOOL changed )
{
    struct reachability_wait_params params = {(UINT_PTR)context};
    unsigned int before = getalls;

    signal_message = message;
    dispatch_context = context;
    check(!reachability_wait( &params ));
    check(getalls == before + requests);
    check(params.state == state);
    check(params.changed == changed);
}

static void run_tests(void)
{
    struct reachability_context context = {0};
    DBusMessageIter iter, array, variant;
    DBusMessage *message;
    unsigned int i;
    const char *extra = "extra";
    static const char *bad[] = {"{uv}", "{ss}", "{iv}"};

    check(load_dbus_functions());
    p_dbus_connection_send_with_reply_and_block = send_reply;
    p_dbus_connection_read_write_dispatch = dispatch;
    context.connection = (void *)1;
    for (i = 0; i < ARRAY_SIZE(bad); i++)
    {
        response = bad_dictionary( bad[i] );
        fprintf( stderr, "checking GetAll a%s\n", bad[i] );
        check(read_reachability( &context ) == REACHABILITY_INDETERMINATE);
        /* Bypass the outer signature guard to exercise the entry boundary. */
        assert(dbus_message_iter_init( response, &iter ));
        dbus_message_iter_recurse( &iter, &array );
        check(!next_dict_entry( &array, &variant ));
        dbus_message_unref( response );
    }
    for (i = 0; i < 8; i++)
    {
        response = properties( 4, i, 0 );
        check(read_reachability( &context ) == (i ? REACHABILITY_INDETERMINATE : REACHABILITY_ONLINE));
        dbus_message_unref( response );
        response = properties( 4, 0, i );
        check(read_reachability( &context ) == (i ? REACHABILITY_INDETERMINATE : REACHABILITY_ONLINE));
        dbus_message_unref( response );
    }
    for (i = 8; i <= 32; i *= 2)
    {
        response = properties( 4, i, 0 );
        check(read_reachability( &context ) == REACHABILITY_INDETERMINATE);
        dbus_message_unref( response );
    }
    for (i = 0; i <= 5; i++)
    {
        response = properties( i, 0, 0 );
        check(read_reachability( &context ) == (!i || i == 5 ? REACHABILITY_INDETERMINATE :
                                                i == 4 ? REACHABILITY_ONLINE : REACHABILITY_OFFLINE));
        dbus_message_unref( response );
    }
    response = dbus_message_new( DBUS_MESSAGE_TYPE_METHOD_RETURN );
    assert(dbus_message_append_args( response, DBUS_TYPE_STRING, &extra, DBUS_TYPE_INVALID ));
    check(read_reachability( &context ) == REACHABILITY_INDETERMINATE);
    assert(dbus_message_iter_init( response, &iter ));
    check(!next_dict_entry( &iter, &variant ));
    dbus_message_unref( response );
    response = properties( 4, 0, 0 );
    assert(dbus_message_append_args( response, DBUS_TYPE_STRING, &extra, DBUS_TYPE_INVALID ));
    check(read_reachability( &context ) == REACHABILITY_INDETERMINATE);
    dbus_message_unref( response );
    response = dbus_message_new( DBUS_MESSAGE_TYPE_METHOD_RETURN );
    check(read_reachability( &context ) == REACHABILITY_INDETERMINATE);
    dbus_message_unref( response );
    response = properties( 2, 0, 0 );
    context.state = REACHABILITY_ONLINE;
    for (i = 1; i <= 5; i++)
        test_signal( &context, properties_signal( current_owner, "org.freedesktop.NetworkManager", i ), 0, REACHABILITY_ONLINE, FALSE );
    test_signal( &context, properties_signal( current_owner, "foreign.interface", FALSE ), 0, REACHABILITY_ONLINE, FALSE );
    test_signal( &context, properties_signal( ":1.99", "org.freedesktop.NetworkManager", FALSE ), 0, REACHABILITY_ONLINE, FALSE );
    test_signal( &context, properties_signal( NULL, "org.freedesktop.NetworkManager", FALSE ), 0, REACHABILITY_ONLINE, FALSE );
    test_signal( &context, owner_signal( "org.freedesktop.DBus", TRUE ), 0, REACHABILITY_ONLINE, FALSE );
    test_signal( &context, owner_signal( ":1.99", FALSE ), 0, REACHABILITY_ONLINE, FALSE );
    for (i = 0; i < 4; i++)
    {
        message = properties_signal( current_owner, "org.freedesktop.NetworkManager", FALSE );
        if (i == 0) assert(dbus_message_set_path( message, "/foreign" ));
        if (i == 1) assert(dbus_message_set_interface( message, "foreign.interface" ));
        if (i == 2) assert(dbus_message_set_member( message, "ForeignSignal" ));
        if (i == 3) assert(dbus_message_append_args( message, DBUS_TYPE_STRING, &extra, DBUS_TYPE_INVALID ));
        test_signal( &context, message, 0, REACHABILITY_ONLINE, FALSE );
    }
    message = owner_signal( "org.freedesktop.DBus", FALSE );
    assert(dbus_message_set_path( message, "/foreign" ));
    test_signal( &context, message, 0, REACHABILITY_ONLINE, FALSE );
    message = owner_signal( "org.freedesktop.DBus", FALSE );
    assert(dbus_message_append_args( message, DBUS_TYPE_STRING, &extra, DBUS_TYPE_INVALID ));
    test_signal( &context, message, 0, REACHABILITY_ONLINE, FALSE );
    test_signal( &context, properties_signal( current_owner, "org.freedesktop.NetworkManager", FALSE ), 1, REACHABILITY_OFFLINE, TRUE );
    test_signal( &context, properties_signal( current_owner, "org.freedesktop.NetworkManager", FALSE ), 1, REACHABILITY_OFFLINE, FALSE );
    current_owner = ":1.11";
    test_signal( &context, owner_signal( "org.freedesktop.DBus", FALSE ), 1, REACHABILITY_OFFLINE, FALSE );
    dbus_message_unref( response );
    response = properties( 4, 0, 0 );
    test_signal( &context, properties_signal( ":1.10", "org.freedesktop.NetworkManager", FALSE ), 0, REACHABILITY_OFFLINE, FALSE );
    test_signal( &context, properties_signal( current_owner, "org.freedesktop.NetworkManager", FALSE ), 1, REACHABILITY_ONLINE, TRUE );
    /* A historical queued owner change must not reinstall the old owner. */
    test_signal( &context, owner_signal( "org.freedesktop.DBus", FALSE ), 1, REACHABILITY_ONLINE, FALSE );
    test_signal( &context, properties_signal( ":1.10", "org.freedesktop.NetworkManager", FALSE ), 0, REACHABILITY_ONLINE, FALSE );
    signal_message = properties_signal( current_owner, "org.freedesktop.NetworkManager", FALSE );
    signal_during_reply = TRUE;
    check(read_reachability( &context ) == REACHABILITY_ONLINE);
    dbus_message_unref( signal_message );
    signal_message = NULL;
    test_signal( &context, NULL, 1, REACHABILITY_ONLINE, FALSE );
    dbus_message_unref( response );
    response = bad_dictionary( "{uv}" );
    test_signal( &context, properties_signal( current_owner, "org.freedesktop.NetworkManager", FALSE ), 1,
                 REACHABILITY_INDETERMINATE, TRUE );
    dbus_message_unref( response );
    free( context.owner );
    /* Restore transport functions before the actual COM integration run. */
#define DO_FUNC(f) p_##f = f
    DBUS_FUNCS;
#undef DO_FUNC
    fprintf( stderr, "D-Bus adversarial fixture: %u checks passed\n", checks );
}

static pthread_once_t tests_once = PTHREAD_ONCE_INIT;

static NTSTATUS tested_start( void *args )
{
    pthread_once( &tests_once, run_tests );
    return reachability_start( args );
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    tested_start, reachability_wait, reachability_stop, topology_supported,
};

#ifdef _WIN64
static NTSTATUS tested_start32( void *args )
{
    pthread_once( &tests_once, run_tests );
    return wow64_reachability_start( args );
}

const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    tested_start32, wow64_reachability_wait, wow64_reachability_stop, topology_supported,
};
#endif
