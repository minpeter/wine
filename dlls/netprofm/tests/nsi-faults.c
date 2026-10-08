/* Exercise real NSI subscriptions and the manager against a failed backend.
 * Only the optional reachability provider is disabled in this manager copy.
 * Copyright 2026 Peter Min
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#define COBJMACROS
#define STANDALONE
#include <stdarg.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winioctl.h"
#include "winsock2.h"
#include "ws2ipdef.h"
#include "iphlpapi.h"
#include "netioapi.h"
#include "netiodef.h"
#include "wine/nsi.h"
#include "wine/unixlib.h"
#include "../unixlib.h"
#include "wine/test.h"

static NTSTATUS test_topology_supported( void *args ) { return STATUS_SUCCESS; }
static NTSTATUS test_reachability_start( void *args ) { return STATUS_NOT_SUPPORTED; }
static NTSTATUS test_reachability_wait( void *args ) { return STATUS_NOT_SUPPORTED; }
static NTSTATUS test_reachability_stop( void *args ) { return STATUS_SUCCESS; }
static void *watched_manager;
static LONG destroyed;

static void test_free( void *ptr )
{
    if (ptr == watched_manager) InterlockedIncrement( &destroyed );
    free( ptr );
}

#undef UNIX_CALL
#define UNIX_CALL(func, params) test_##func(params)
#define free test_free
#include "../list.c"
#undef free

static void release_manager( INetworkListManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    HANDLE worker = NULL;
    BOOL ret;

    ret = DuplicateHandle( GetCurrentProcess(), mgr->worker, GetCurrentProcess(), &worker,
                           SYNCHRONIZE, FALSE, 0 );
    ok( ret, "duplicate worker: %lu\n", GetLastError() );
    destroyed = 0;
    watched_manager = mgr;
    /* A callback may hold the last temporary reference. Check actual
     * destruction after joining, rather than the advisory Release count. */
    INetworkListManager_Release( iface );
    ok( WaitForSingleObject( worker, 10000 ) == WAIT_OBJECT_0, "manager teardown stuck\n" );
    ok( destroyed == 1, "manager destroyed %ld times\n", destroyed );
    watched_manager = NULL;
    CloseHandle( worker );
}

static char directory[MAX_PATH];

static void marker( const char *name )
{
    char path[MAX_PATH];
    HANDLE file;
    snprintf( path, sizeof(path), "%s/%s", directory, name );
    file = CreateFileA( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    ok( file != INVALID_HANDLE_VALUE, "marker %s error %lu\n", name, GetLastError() );
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
}

static void wait_marker( const char *name )
{
    char path[MAX_PATH];
    unsigned int i;
    snprintf( path, sizeof(path), "%s/%s", directory, name );
    for (i = 0; i < 1000; i++)
    {
        if (GetFileAttributesA( path ) != INVALID_FILE_ATTRIBUTES) return;
        Sleep( 10 );
    }
    ok( 0, "timed out waiting for %s\n", name );
}

START_TEST(nsi_faults)
{
    OVERLAPPED pending[32] = {{0}};
    OVERLAPPED closed = {0};
    struct nsiproxy_request_notification request = {0};
    HANDLE handles[32];
    HANDLE device;
    INetworkListManager *iface = NULL, *other = NULL;
    struct list_manager *mgr;
    NLM_CONNECTIVITY value, expected;
    char mode[32];
    HRESULT hr;
    DWORD ret, bytes, expected_error;
    unsigned int i;
    BOOL control;

    GetEnvironmentVariableA( "WINETEST_NSI_DIR", directory, sizeof(directory) );
    GetEnvironmentVariableA( "WINETEST_NSI_FAULT", mode, sizeof(mode) );
    control = !strcmp( mode, "control" );
    expected_error = !strcmp( mode, "socket" ) ? ERROR_INVALID_FUNCTION : ERROR_GEN_FAILURE;
    expected = !strcmp( mode, "retained" ) ?
        NLM_CONNECTIVITY_IPV4_LOCALNETWORK | NLM_CONNECTIVITY_IPV6_INTERNET : NLM_CONNECTIVITY_DISCONNECTED;
    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    hr = list_manager_create( (void **)&iface );
    ok( hr == S_OK, "create %#lx\n", hr );
    if (FAILED(hr)) goto done;
    mgr = impl_from_INetworkListManager( iface );
    INetworkListManager_GetConnectivity( iface, &value );
    ok( value == (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_LOCALNETWORK),
        "initial connectivity %#x\n", value );
    ok( mgr->dynamic_topology, "initial manager is not dynamic\n" );

    /* Releasing another manager must cancel and drain its real subscriptions. */
    hr = list_manager_create( (void **)&other );
    ok( hr == S_OK, "second create %#lx\n", hr );
    if (other) release_manager( other );
    /* Wine does not dispatch IRP_MJ_CLEANUP on handle close. Keep this
     * OVERLAPPED alive and verify that backend completion still drains the
     * request after its file handle has closed. */
    device = CreateFileW( L"\\\\.\\Nsi", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL );
    ok( device != INVALID_HANDLE_VALUE, "open device: %lu\n", GetLastError() );
    closed.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    request.module = npi_ndis_module;
    request.table = NSI_NDIS_IFINFO_TABLE;
    ret = DeviceIoControl( device, IOCTL_NSIPROXY_WINE_CHANGE_NOTIFICATION, &request, sizeof(request),
                           NULL, 0, &bytes, &closed );
    ok( !ret && GetLastError() == ERROR_IO_PENDING, "device request: %lu/%lu\n", ret, GetLastError() );
    CloseHandle( device );
    for (i = 0; i < ARRAY_SIZE(pending); i++)
    {
        pending[i].hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
        ret = NsiRequestChangeNotification( 0, &npi_ndis_module, NSI_NDIS_IFINFO_TABLE, pending + i, handles + i );
        ok( ret == ERROR_IO_PENDING, "subscription %u: %lu\n", i, ret );
    }
    for (i = 0; i < ARRAY_SIZE(pending) / 2; i++)
    {
        ret = NsiCancelChangeNotification( pending + i );
        ok( !ret, "cancel %u: %lu\n", i, ret );
        ret = GetOverlappedResult( handles[i], pending + i, &bytes, TRUE );
        ok( !ret && GetLastError() == ERROR_OPERATION_ABORTED, "cancel result %lu/%lu\n", ret, GetLastError() );
    }
    marker( "ready" );
    wait_marker( "changed" );
    ret = WaitForSingleObject( closed.hEvent, 10000 );
    ok( ret == WAIT_OBJECT_0, "closed file request stranded: %lu\n", ret );
    ok( (NTSTATUS)closed.Internal == (control ? STATUS_SUCCESS :
        !strcmp( mode, "socket" ) ? STATUS_NOT_IMPLEMENTED : STATUS_UNSUCCESSFUL),
        "closed file completion %#lx\n", (NTSTATUS)closed.Internal );
    CloseHandle( closed.hEvent );
    if (!control)
    {
        ret = WaitForSingleObject( mgr->worker, 10000 );
        ok( ret == WAIT_OBJECT_0, "monitor did not exit: %lu\n", ret );
        ok( !mgr->dynamic_topology, "failed backend retained dynamic policy\n" );
    }
    for (i = ARRAY_SIZE(pending) / 2; i < ARRAY_SIZE(pending); i++)
    {
        ret = WaitForSingleObject( pending[i].hEvent, 10000 );
        ok( ret == WAIT_OBJECT_0, "pending %u was stranded: %lu\n", i, ret );
        ret = GetOverlappedResult( handles[i], pending + i, &bytes, FALSE );
        ok( control ? ret : !ret && GetLastError() == expected_error,
            "completion %u: %lu/%lu\n", i, ret, GetLastError() );
    }
    for (i = 0; i < 1000; i++)
    {
        INetworkListManager_GetConnectivity( iface, &value );
        if (value == expected) break;
        Sleep( 10 );
    }
    ok( value == expected, "final connectivity %#x, expected %#x\n", value, expected );
    ok( mgr->dynamic_topology == control, "final dynamic policy %u\n", mgr->dynamic_topology );
    trace( "%s: final connectivity %#x, dynamic %u\n", mode, value, mgr->dynamic_topology );
    release_manager( iface );
    iface = NULL;
    /* A terminal error is sticky, even after the socket fault is removed. */
    for (i = 0; i < ARRAY_SIZE(pending); i++)
    {
        ResetEvent( pending[i].hEvent );
        ret = NsiRequestChangeNotification( 0, &npi_ndis_module, NSI_NDIS_IFINFO_TABLE, pending + i, handles + i );
        ok( control ? ret == ERROR_IO_PENDING : ret == expected_error,
            "future subscription %u: %lu\n", i, ret );
        if (ret == ERROR_IO_PENDING)
        {
            NsiCancelChangeNotification( pending + i );
            GetOverlappedResult( handles[i], pending + i, &bytes, TRUE );
        }
        CloseHandle( pending[i].hEvent );
    }
    other = NULL;
    hr = list_manager_create( (void **)&other );
    ok( hr == (control ? S_OK : HRESULT_FROM_WIN32( expected_error )), "future manager create %#lx\n", hr );
    if (other) release_manager( other );
done:
    if (iface) release_manager( iface );
    CoUninitialize();
}
