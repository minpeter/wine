/* Compile the real manager with injected OS failures, without production test
 * hooks. Run in the two-address/no-default-route namespace in adversarial.sh.
 *
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
#include "winsock2.h"
#include "ws2ipdef.h"
#include "iphlpapi.h"
#include "netioapi.h"
#include "netiodef.h"
#include "wine/nsi.h"
#include "wine/unixlib.h"
#include "../unixlib.h"
#include "wine/test.h"

static unsigned int fail_event, fail_subscription, fail_thread, events_created, threads_created;
static unsigned int subscription_calls, cancelled, outstanding, adapter_calls, fail_adapter;
static unsigned int fail_ready, fail_wait, fail_single, fail_completion, synchronous;
static unsigned int trigger_index, fail_reset, resets;
static BOOL fail_reconcile;
static LONG handles;
static BOOL provider_enabled, provider_fails, unsupported;
static unsigned int provider_stopped;
static HANDLE gate, ready_handle, rearmed;
static OVERLAPPED *pending[5];

static HANDLE WINAPI fault_event( SECURITY_ATTRIBUTES *sa, BOOL manual, BOOL state, const WCHAR *name )
{
    HANDLE handle;
    if (++events_created == fail_event) { SetLastError( ERROR_NOT_ENOUGH_MEMORY ); return NULL; }
    handle = CreateEventW( sa, manual, state, name );
    if (handle) InterlockedIncrement( &handles );
    if (events_created == 2) ready_handle = handle;
    return handle;
}

static HANDLE WINAPI fault_thread( SECURITY_ATTRIBUTES *sa, SIZE_T stack, LPTHREAD_START_ROUTINE proc,
                                    void *arg, DWORD flags, DWORD *tid )
{
    HANDLE handle;
    if (++threads_created == fail_thread) { SetLastError( ERROR_NOT_ENOUGH_MEMORY ); return NULL; }
    handle = CreateThread( sa, stack, proc, arg, flags, tid );
    if (handle) InterlockedIncrement( &handles );
    return handle;
}

static BOOL WINAPI fault_close( HANDLE handle )
{
    BOOL ret = CloseHandle( handle );
    ok( ret, "invalid/double handle close %p\n", handle );
    if (ret) InterlockedDecrement( &handles );
    return ret;
}

static BOOL WINAPI fault_reset( HANDLE handle )
{
    if (++resets == fail_reset) { SetLastError( ERROR_INVALID_HANDLE ); return FALSE; }
    return ResetEvent( handle );
}

static BOOL WINAPI fault_set( HANDLE handle )
{
    if (fail_ready && handle == ready_handle) { SetLastError( ERROR_INVALID_HANDLE ); return FALSE; }
    return SetEvent( handle );
}

static DWORD WINAPI fault_multiple( DWORD count, const HANDLE *events, BOOL all, DWORD timeout )
{
    HANDLE wait[2];
    DWORD ret;
    if (count == 2 && fail_wait == 1) { SetLastError( ERROR_INVALID_HANDLE ); return WAIT_FAILED; }
    if (count != 6) return WaitForMultipleObjects( count, events, all, timeout );
    wait[0] = events[0];
    wait[1] = gate;
    ret = WaitForMultipleObjects( 2, wait, FALSE, timeout );
    if (ret != WAIT_OBJECT_0 + 1) return ret;
    if (fail_wait == 2) { SetLastError( ERROR_INVALID_HANDLE ); return WAIT_FAILED; }
    ResetEvent( gate );
    SetEvent( events[trigger_index + 1] );
    return WAIT_OBJECT_0 + trigger_index + 1;
}

static DWORD WINAPI fault_single( HANDLE event, DWORD timeout )
{
    if ((fail_single == 1 && timeout == 150) || (fail_single == 2 && !timeout))
    { SetLastError( ERROR_INVALID_HANDLE ); return WAIT_FAILED; }
    return WaitForSingleObject( event, timeout );
}

static DWORD WINAPI fault_subscribe( DWORD unk, const NPI_MODULEID *module, DWORD table,
                                      OVERLAPPED *overlapped, HANDLE *handle )
{
    unsigned int i;
    subscription_calls++;
    if (fail_reconcile && subscription_calls == 5) fail_adapter = adapter_calls + 1;
    if (subscription_calls == fail_subscription) return ERROR_NOT_SUPPORTED;
    if (synchronous && subscription_calls == 1) return ERROR_SUCCESS;
    for (i = 0; i < ARRAY_SIZE(pending); i++) if (!pending[i]) break;
    ok( i < ARRAY_SIZE(pending), "too many subscriptions\n" );
    if (i == ARRAY_SIZE(pending)) return ERROR_NOT_ENOUGH_MEMORY;
    pending[i] = overlapped;
    outstanding++;
    *handle = (HANDLE)0x1234;
    if (subscription_calls == 6) SetEvent( rearmed );
    return ERROR_IO_PENDING;
}

static BOOL WINAPI fault_result( HANDLE handle, OVERLAPPED *overlapped, DWORD *bytes, BOOL wait )
{
    unsigned int i;
    if (!wait && fail_completion) { SetLastError( ERROR_GEN_FAILURE ); return FALSE; }
    for (i = 0; i < ARRAY_SIZE(pending); i++) if (pending[i] == overlapped) break;
    ok( i < ARRAY_SIZE(pending), "completion for unknown subscription\n" );
    if (i < ARRAY_SIZE(pending)) { pending[i] = NULL; outstanding--; }
    *bytes = 0;
    return TRUE;
}

static DWORD WINAPI fault_cancel( OVERLAPPED *overlapped )
{
    cancelled++;
    return ERROR_SUCCESS;
}

static ULONG WINAPI fault_adapters( ULONG family, ULONG flags, void *reserved,
                                    IP_ADAPTER_ADDRESSES *addresses, ULONG *size )
{
    if (++adapter_calls == fail_adapter) return ERROR_NOT_ENOUGH_MEMORY;
    return GetAdaptersAddresses( family, flags, reserved, addresses, size );
}

static NTSTATUS fake_topology_supported( void *args ) { return unsupported ? STATUS_NOT_SUPPORTED : STATUS_SUCCESS; }
static NTSTATUS fake_reachability_start( struct reachability_start_params *params )
{
    if (!provider_enabled) return STATUS_NOT_SUPPORTED;
    params->handle = 1;
    params->state = REACHABILITY_OFFLINE;
    return STATUS_SUCCESS;
}
static NTSTATUS fake_reachability_wait( struct reachability_wait_params *params )
{
    WaitForSingleObject( gate, INFINITE );
    return provider_fails ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}
static NTSTATUS fake_reachability_stop( void *args ) { provider_stopped++; return STATUS_SUCCESS; }

#undef UNIX_CALL
#define UNIX_CALL(func, params) fake_##func(params)
#define CreateEventW fault_event
#define CreateThread fault_thread
#define CloseHandle fault_close
#define ResetEvent fault_reset
#define SetEvent fault_set
#define WaitForMultipleObjects fault_multiple
#define WaitForSingleObject fault_single
#define NsiRequestChangeNotification fault_subscribe
#define NsiCancelChangeNotification fault_cancel
#define GetOverlappedResult fault_result
#define GetAdaptersAddresses fault_adapters
#ifndef MANAGER_SOURCE
#define MANAGER_SOURCE "../list.c"
#endif
#include MANAGER_SOURCE
#undef CreateEventW
#undef CreateThread
#undef CloseHandle
#undef ResetEvent
#undef SetEvent
#undef WaitForMultipleObjects
#undef WaitForSingleObject

static LONG callbacks;
static NLM_CONNECTIVITY callback_value;
static INetworkListManager *release_in_callback;
static HRESULT WINAPI sink_query( INetworkListManagerEvents *iface, REFIID iid, void **out )
{
    *out = NULL;
    if (!IsEqualIID( iid, &IID_IUnknown ) && !IsEqualIID( iid, &IID_INetworkListManagerEvents )) return E_NOINTERFACE;
    *out = iface;
    return S_OK;
}
static ULONG WINAPI sink_addref( INetworkListManagerEvents *iface ) { return 2; }
static ULONG WINAPI sink_release( INetworkListManagerEvents *iface ) { return 1; }
static HRESULT WINAPI sink_changed( INetworkListManagerEvents *iface, NLM_CONNECTIVITY value )
{
    callback_value = value;
    InterlockedIncrement( &callbacks );
    if (release_in_callback)
    {
        INetworkListManager_Release( release_in_callback );
        release_in_callback = NULL;
    }
    return S_OK;
}
static const INetworkListManagerEventsVtbl sink_vtbl = {sink_query, sink_addref, sink_release, sink_changed};
static INetworkListManagerEvents sink = {&sink_vtbl};

static void reset_faults(void)
{
    ok( !handles && !outstanding, "leaked %ld handles, %u subscriptions\n", handles, outstanding );
    events_created = threads_created = subscription_calls = cancelled = adapter_calls = 0;
    fail_event = fail_subscription = fail_thread = fail_ready = fail_wait = fail_single = fail_completion = fail_adapter = synchronous = 0;
    trigger_index = fail_reset = resets = 0;
    fail_reconcile = FALSE;
    provider_enabled = provider_fails = unsupported = FALSE;
    provider_stopped = 0;
    ready_handle = NULL;
    callbacks = 0;
    ResetEvent( gate );
    ResetEvent( rearmed );
}

static void creation_failure( HRESULT expected, unsigned int expected_cancelled )
{
    INetworkListManager *iface = NULL;
    HRESULT hr = list_manager_create( (void **)&iface );
    ok( hr == expected, "creation returned %#lx, expected %#lx\n", hr, expected );
    ok( !iface, "failed creation published manager %p\n", iface );
    if (iface) INetworkListManager_Release( iface );
    ok( cancelled == expected_cancelled, "cancelled %u, expected %u\n", cancelled, expected_cancelled );
    ok( !handles && !outstanding, "leaked %ld handles, %u subscriptions\n", handles, outstanding );
}

static void check_connectivity( INetworkListManager *iface, NLM_CONNECTIVITY expected )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct network *network;
    struct connection *connection;
    NLM_CONNECTIVITY value;
    HRESULT hr;

    hr = INetworkListManager_GetConnectivity( iface, &value );
    ok( hr == S_OK && value == expected, "manager %#x, expected %#x, hr %#lx\n", value, expected, hr );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        INetwork_GetConnectivity( &network->INetwork_iface, &value );
        ok( value == expected, "network %#x, expected %#x\n", value, expected );
    }
    LIST_FOR_EACH_ENTRY( connection, &mgr->connections, struct connection, entry )
    {
        INetworkConnection_GetConnectivity( &connection->INetworkConnection_iface, &value );
        ok( value == expected, "connection %#x, expected %#x\n", value, expected );
    }
}

START_TEST(monitor_faults)
{
    INetworkListManager *iface;
    struct list_manager *mgr;
    IConnectionPoint *point;
    DWORD cookie;
    HANDLE worker;
    HRESULT hr;
    unsigned int i;

    gate = CreateEventW( NULL, TRUE, FALSE, NULL );
    rearmed = CreateEventW( NULL, TRUE, FALSE, NULL );
    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    for (i = 1; i <= 7; i++)
    {
        reset_faults();
        fail_event = i;
        creation_failure( HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY), i > 2 ? i - 3 : 0 );
    }
    for (i = 1; i <= 5; i++)
    {
        reset_faults();
        fail_subscription = i;
        creation_failure( HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), i - 1 );
    }
    reset_faults();
    fail_thread = 1;
    creation_failure( HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY), 0 );
    reset_faults();
    fail_ready = 1;
    creation_failure( E_FAIL, 5 );
    reset_faults();
    fail_wait = 1;
    creation_failure( HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), 5 );
    reset_faults();
    fail_reconcile = TRUE;
    creation_failure( E_OUTOFMEMORY, 5 );
    for (i = 1; i <= 5; i++)
    {
        reset_faults();
        fail_reset = i;
        creation_failure( HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), i - 1 );
    }
    reset_faults();
    synchronous = TRUE;
    hr = list_manager_create( (void **)&iface );
    ok( hr == S_OK, "synchronous subscription creation failed %#lx\n", hr );
    if (SUCCEEDED(hr))
    {
        ok( outstanding == 4, "synchronous completion retained pending request\n" );
        SetEvent( gate );
        ok( WaitForSingleObject( rearmed, 5000 ) == WAIT_OBJECT_0, "synchronous completion not rearmed\n" );
        INetworkListManager_Release( iface );
        ok( cancelled == 5, "cancelled %u after synchronous completion\n", cancelled );
    }
    for (i = 0; i < 12; i++)
    {
        reset_faults();
        hr = list_manager_create( (void **)&iface );
        ok( hr == S_OK, "creation failed %#lx\n", hr );
        if (FAILED(hr)) continue;
        mgr = impl_from_INetworkListManager( iface );
        ok( mgr->dynamic_topology && outstanding == 5, "not fully armed\n" );
        check_connectivity( iface, 0x220 );
        IConnectionPointContainer_FindConnectionPoint( &mgr->IConnectionPointContainer_iface,
                                                       &IID_INetworkListManagerEvents, &point );
        IConnectionPoint_Advise( point, (IUnknown *)&sink, &cookie );
        if (i == 0 || i == 9) fail_wait = 2;
        if (i >= 1 && i <= 5) { trigger_index = i - 1; fail_subscription = 6; }
        if (i == 6) fail_single = 1;
        if (i == 7) fail_single = 2;
        if (i == 8) fail_completion = 1;
        if (i == 9 || i == 10) fail_adapter = adapter_calls + 1;
        if (i == 11) fail_reset = 6;
        SetEvent( gate );
        ok( WaitForSingleObject( mgr->worker, 5000 ) == WAIT_OBJECT_0, "worker did not exit\n" );
        ok( !mgr->dynamic_topology, "fatal failure retained dynamic policy\n" );
        check_connectivity( iface, i == 9 ? 0 : 0x440 );
        ok( callbacks == 1 && callback_value == (i == 9 ? 0 : 0x440), "callbacks %ld, value %#x\n", callbacks, callback_value );
        ok( !outstanding && cancelled == ((i >= 1 && i <= 5) || i == 11 ? 4 : 5),
            "subscriptions %u, cancellations %u\n", outstanding, cancelled );
        IConnectionPoint_Unadvise( point, cookie );
        IConnectionPoint_Release( point );
        INetworkListManager_Release( iface );
    }
    for (i = 0; i < 4; i++)
    {
        reset_faults();
        provider_enabled = TRUE;
        if (i == 0) fail_event = 8;
        if (i == 1) fail_thread = 2;
        if (i == 2) provider_fails = TRUE;
        if (i == 3) fail_single = 2;
        hr = list_manager_create( (void **)&iface );
        ok( hr == S_OK, "optional provider failure prevented creation %#lx\n", hr );
        if (FAILED(hr)) continue;
        mgr = impl_from_INetworkListManager( iface );
        if (i == 2)
        {
            fail_wait = 2;
            SetEvent( gate );
            WaitForSingleObject( mgr->reachability_worker, 5000 );
            WaitForSingleObject( mgr->worker, 5000 );
        }
        if (i == 3)
            ok( WaitForSingleObject( mgr->reachability_worker, 5000 ) == WAIT_OBJECT_0,
                "provider WAIT_FAILED did not terminate worker\n" );
        ok( mgr->reachability == REACHABILITY_INDETERMINATE, "stale provider state %u\n", mgr->reachability );
        INetworkListManager_Release( iface );
        ok( provider_stopped == 1, "provider stop count %u\n", provider_stopped );
    }
    /* The fatal-failure notification can itself drop the last client reference. */
    reset_faults();
    hr = list_manager_create( (void **)&iface );
    ok( hr == S_OK, "creation failed %#lx\n", hr );
    if (SUCCEEDED(hr))
    {
        mgr = impl_from_INetworkListManager( iface );
        IConnectionPointContainer_FindConnectionPoint( &mgr->IConnectionPointContainer_iface,
                                                       &IID_INetworkListManagerEvents, &point );
        IConnectionPoint_Advise( point, (IUnknown *)&sink, &cookie );
        IConnectionPoint_Release( point );
        DuplicateHandle( GetCurrentProcess(), mgr->worker, GetCurrentProcess(), &worker,
                         SYNCHRONIZE, FALSE, 0 );
        release_in_callback = iface;
        fail_wait = 2;
        SetEvent( gate );
        ok( WaitForSingleObject( worker, 5000 ) == WAIT_OBJECT_0, "callback teardown deadlocked\n" );
        CloseHandle( worker );
        ok( !release_in_callback && callbacks == 1, "final-release callback not delivered\n" );
    }
    reset_faults();
    unsupported = TRUE;
    hr = list_manager_create( (void **)&iface );
    ok( hr == S_OK && !subscription_calls, "unsupported backend started monitor\n" );
    if (SUCCEEDED(hr)) { check_connectivity( iface, 0x440 ); INetworkListManager_Release( iface ); }
    reset_faults();
    CoUninitialize();
    CloseHandle( gate );
    CloseHandle( rearmed );
}
