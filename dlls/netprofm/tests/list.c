/*
 * Copyright 2014 Hans Leidekker for CodeWeavers
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

#include <stdio.h>
#include "windows.h"
#define COBJMACROS
#include "initguid.h"
#include "objbase.h"
#include "ocidl.h"
#include "olectl.h"
#include "netlistmgr.h"
#include "wine/test.h"

struct dynamic_sink
{
    INetworkListManagerEvents INetworkListManagerEvents_iface;
    LONG refs;
    INetworkListManager *mgr;
    IConnectionPoint *connection_point;
    DWORD cookie;
    DWORD apartment_tid;
    LONG callback_count;
    LONG callback_mismatch;
    LONG callback_thread_mismatch;
    NLM_CONNECTIVITY callback_connectivity;
    BOOL unadvise_on_callback;
    BOOL teardown_on_callback;
    HRESULT unadvise_hr;
};

static void test_INetwork( INetwork *network, INetworkConnection *conn )
{
    NLM_NETWORK_CATEGORY category;
    NLM_CONNECTIVITY connectivity;
    NLM_DOMAIN_TYPE domain_type;
    VARIANT_BOOL connected;
    IEnumNetworkConnections *conn_iter;
    VARIANT_BOOL is_connection_present;
    GUID conn_id;
    GUID local_conn_id;
    GUID id;
    BSTR str;
    HRESULT hr;
    INetworkConnection *local_conn;
    ULONG fetched;

    str = NULL;
    hr = INetwork_GetName( network, &str );
    todo_wine ok( hr == S_OK, "got %08lx\n", hr );
    todo_wine ok( str != NULL, "str not set\n" );
    if (str) trace( "name %s\n", wine_dbgstr_w(str) );
    SysFreeString( str );

    str = NULL;
    hr = INetwork_GetDescription( network, &str );
    todo_wine ok( hr == S_OK, "got %08lx\n", hr );
    todo_wine ok( str != NULL, "str not set\n" );
    if (str) trace( "description %s\n", wine_dbgstr_w(str) );
    SysFreeString( str );

    memset( &id, 0, sizeof(id) );
    hr = INetwork_GetNetworkId( network, &id );
    ok( hr == S_OK, "got %08lx\n", hr );
    trace("network id %s\n", wine_dbgstr_guid(&id));

    domain_type = 0xdeadbeef;
    hr = INetwork_GetDomainType( network, &domain_type );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( domain_type != 0xdeadbeef, "domain_type not set\n" );
    trace( "domain type %08x\n", domain_type );

    category = 0xdeadbeef;
    hr = INetwork_GetCategory( network, &category );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( category != 0xdeadbeef, "category not set\n" );
    trace( "category %08x\n", category );

    connectivity = 0xdeadbeef;
    hr = INetwork_GetConnectivity( network, &connectivity );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( connectivity != 0xdeadbeef, "connectivity not set\n" );
    trace( "connectivity %08x\n", connectivity );

    connected = 0xdead;
    hr = INetwork_get_IsConnected( network, &connected );
    ok( hr == S_OK, "got %08lx\n", hr );
    trace("connected %d\n", connected);

    connected = 0xdead;
    hr = INetwork_get_IsConnectedToInternet( network, &connected );
    ok( hr == S_OK, "got %08lx\n", hr );
    trace("connected to internet %d\n", connected);

    if (!conn) return;

    trace("about to test GetNetworkConnections\n");
    memset( &conn_id, 0, sizeof(conn_id) );
    hr = INetworkConnection_GetConnectionId( conn, &conn_id );
    ok( hr == S_OK, "got %08lx\n", hr );
    trace("input connection id %s\n", wine_dbgstr_guid(&conn_id));

    conn_iter = NULL;
    hr = INetwork_GetNetworkConnections( network, &conn_iter );
    ok( hr == S_OK, "got %08lx\n", hr );

    is_connection_present = FALSE;
    if (conn_iter)
    {
        while ((hr = IEnumNetworkConnections_Next( conn_iter, 1, &local_conn, &fetched )) == S_OK)
        {
            memset( &local_conn_id, 0, sizeof(local_conn_id) );
            hr = INetworkConnection_GetConnectionId( local_conn, &local_conn_id );
            ok( hr == S_OK, "got %08lx\n", hr );
            trace("local connection id %s\n", wine_dbgstr_guid(&local_conn_id));

            if (IsEqualGUID(&conn_id, &local_conn_id))
                is_connection_present = TRUE;

            INetworkConnection_Release( local_conn );
            local_conn = (void *)0xdeadbeef;
        }
        ok( !local_conn, "got wrong pointer: %p.\n", local_conn );
        IEnumNetworkConnections_Release( conn_iter );
    }

    ok( is_connection_present, "connection was not present in network\n" );
}

static void test_INetworkConnection( INetworkConnection *conn )
{
    INetwork *network;
    INetworkConnectionCost *conn_cost;
    NLM_CONNECTIVITY connectivity;
    NLM_DOMAIN_TYPE domain_type;
    VARIANT_BOOL connected;
    GUID id;
    HRESULT hr;

    memset( &id, 0, sizeof(id) );
    hr = INetworkConnection_GetAdapterId( conn, &id );
    ok( hr == S_OK, "got %08lx\n", hr );
    trace("adapter id %s\n", wine_dbgstr_guid(&id));

    memset( &id, 0, sizeof(id) );
    hr = INetworkConnection_GetConnectionId( conn, &id );
    ok( hr == S_OK, "got %08lx\n", hr );
    trace("connection id %s\n", wine_dbgstr_guid(&id));

    connectivity = 0xdeadbeef;
    hr = INetworkConnection_GetConnectivity( conn, &connectivity );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( connectivity != 0xdeadbeef, "connectivity not set\n" );
    trace( "connectivity %08x\n", connectivity );

    domain_type = 0xdeadbeef;
    hr = INetworkConnection_GetDomainType( conn, &domain_type );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( domain_type != 0xdeadbeef, "domain_type not set\n" );
    trace( "domain type %08x\n", domain_type );

    connected = 0xdead;
    hr = INetworkConnection_get_IsConnected( conn, &connected );
    ok( hr == S_OK, "got %08lx\n", hr );
    trace("connected %d\n", connected);

    connected = 0xdead;
    hr = INetworkConnection_get_IsConnectedToInternet( conn, &connected );
    ok( hr == S_OK, "got %08lx\n", hr );
    trace("connected to internet %d\n", connected);

    network = NULL;
    hr = INetworkConnection_GetNetwork( conn, &network );
    ok( hr == S_OK, "got %08lx\n", hr );
    if (network)
    {
        test_INetwork( network, conn );
        INetwork_Release( network );
    }

    conn_cost = NULL;
    hr = INetworkConnection_QueryInterface( conn, &IID_INetworkConnectionCost, (void **)&conn_cost );
    ok( hr == S_OK || broken(hr == E_NOINTERFACE), "got %08lx\n", hr );
    if (conn_cost)
    {
        DWORD cost;
        NLM_DATAPLAN_STATUS status;

        cost = 0xdeadbeef;
        hr = INetworkConnectionCost_GetCost( conn_cost, &cost );
        ok( hr == S_OK, "got %08lx\n", hr );
        ok( cost != 0xdeadbeef, "cost not set\n" );
        trace("cost %08lx\n", cost);

        memset( &status, 0,sizeof(status) );
        hr = INetworkConnectionCost_GetDataPlanStatus( conn_cost, &status );
        ok( hr == S_OK, "got %08lx\n", hr );
        trace("InterfaceGuid %s\n", wine_dbgstr_guid(&status.InterfaceGuid));

        INetworkConnectionCost_Release( conn_cost );
    }
}

static HRESULT WINAPI Unknown_QueryInterface(INetworkListManagerEvents *iface, REFIID riid, void **ppv)
{
    if(IsEqualGUID(riid, &IID_IUnknown)) {
        *ppv = iface;
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

static HRESULT WINAPI NetworkListManagerEvents_QueryInterface(INetworkListManagerEvents *iface,
                                                              REFIID riid, void **ppv)
{
    if(IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_INetworkListManagerEvents)) {
        *ppv = iface;
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI NetworkListManagerEvents_AddRef(INetworkListManagerEvents *iface)
{
    return 2;
}

static ULONG WINAPI NetworkListManagerEvents_Release(INetworkListManagerEvents *iface)
{
    return 1;
}

static HRESULT WINAPI NetworkListManagerEvents_ConnectivityChanged(INetworkListManagerEvents *iface,
        NLM_CONNECTIVITY newConnectivity)
{
    return S_OK;
}

static const INetworkListManagerEventsVtbl mgr_sink_unk_vtbl = {
    Unknown_QueryInterface,
    NetworkListManagerEvents_AddRef,
    NetworkListManagerEvents_Release,
    NetworkListManagerEvents_ConnectivityChanged
};

static INetworkListManagerEvents mgr_sink_unk = { &mgr_sink_unk_vtbl };

static struct dynamic_sink *impl_from_dynamic_sink( INetworkListManagerEvents *iface )
{
    return CONTAINING_RECORD( iface, struct dynamic_sink, INetworkListManagerEvents_iface );
}

static HRESULT WINAPI dynamic_sink_QueryInterface( INetworkListManagerEvents *iface, REFIID iid, void **out )
{
    if (IsEqualIID( iid, &IID_IUnknown ) || IsEqualIID( iid, &IID_INetworkListManagerEvents ))
    {
        *out = iface;
        INetworkListManagerEvents_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dynamic_sink_AddRef( INetworkListManagerEvents *iface )
{
    return InterlockedIncrement( &impl_from_dynamic_sink( iface )->refs );
}

static ULONG WINAPI dynamic_sink_Release( INetworkListManagerEvents *iface )
{
    return InterlockedDecrement( &impl_from_dynamic_sink( iface )->refs );
}

static HRESULT WINAPI dynamic_sink_ConnectivityChanged( INetworkListManagerEvents *iface,
                                                        NLM_CONNECTIVITY connectivity )
{
    struct dynamic_sink *sink = impl_from_dynamic_sink( iface );
    APTTYPEQUALIFIER qualifier;
    NLM_CONNECTIVITY current;
    APTTYPE type;

    sink->callback_connectivity = connectivity;
    InterlockedIncrement( &sink->callback_count );
    if (GetCurrentThreadId() != sink->apartment_tid || FAILED(CoGetApartmentType( &type, &qualifier )) ||
        (type != APTTYPE_STA && type != APTTYPE_MAINSTA))
        InterlockedIncrement( &sink->callback_thread_mismatch );
    if (FAILED(INetworkListManager_GetConnectivity( sink->mgr, &current )) || current != connectivity)
        InterlockedIncrement( &sink->callback_mismatch );
    if (sink->unadvise_on_callback && sink->cookie)
    {
        sink->unadvise_hr = IConnectionPoint_Unadvise( sink->connection_point, sink->cookie );
        sink->cookie = 0;
    }
    if (sink->teardown_on_callback)
    {
        IConnectionPoint_Release( sink->connection_point );
        sink->connection_point = NULL;
        INetworkListManager_Release( sink->mgr );
        sink->mgr = NULL;
    }
    return S_OK;
}

static const INetworkListManagerEventsVtbl dynamic_sink_vtbl =
{
    dynamic_sink_QueryInterface,
    dynamic_sink_AddRef,
    dynamic_sink_Release,
    dynamic_sink_ConnectivityChanged,
};

static void init_dynamic_sink( struct dynamic_sink *sink, INetworkListManager *mgr,
                               IConnectionPoint *connection_point )
{
    memset( sink, 0, sizeof(*sink) );
    sink->INetworkListManagerEvents_iface.lpVtbl = &dynamic_sink_vtbl;
    sink->refs = 1;
    sink->mgr = mgr;
    sink->connection_point = connection_point;
    sink->apartment_tid = GetCurrentThreadId();
    sink->unadvise_hr = E_UNEXPECTED;
}

static void test_INetworkListManager( void )
{
    struct dynamic_sink sink;
    IConnectionPointContainer *cpc, *cpc2;
    INetworkListManager *mgr, *second_mgr;
    INetworkCostManager *cost_mgr;
    NLM_CONNECTIVITY connectivity;
    VARIANT_BOOL connected;
    IConnectionPoint *pt, *pt2;
    IEnumNetworks *network_iter;
    INetwork *network;
    IEnumNetworkConnections *conn_iter;
    INetworkConnection *conn;
    DWORD cookie;
    HRESULT hr;
    ULONG ref1, ref2, fetched;
    IID iid;

    hr = CoCreateInstance( &CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                           &IID_INetworkListManager, (void **)&mgr );
    if (hr != S_OK)
    {
        win_skip( "can't create instance of NetworkListManager\n" );
        return;
    }

    connectivity = 0xdeadbeef;
    hr = INetworkListManager_GetConnectivity( mgr, &connectivity );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( connectivity != 0xdeadbeef, "unchanged value\n" );
    trace( "GetConnectivity: %08x\n", connectivity );

    connected = 0xdead;
    hr = INetworkListManager_IsConnected( mgr, &connected );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( connected == VARIANT_TRUE || connected == VARIANT_FALSE, "expected boolean value\n" );

    connected = 0xdead;
    hr = INetworkListManager_IsConnectedToInternet( mgr, &connected );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( connected == VARIANT_TRUE || connected == VARIANT_FALSE, "expected boolean value\n" );

    /* INetworkCostManager is supported starting Win8 */
    cost_mgr = NULL;
    hr = INetworkListManager_QueryInterface( mgr, &IID_INetworkCostManager, (void **)&cost_mgr );
    ok( hr == S_OK || broken(hr == E_NOINTERFACE), "got %08lx\n", hr );
    if (cost_mgr)
    {
        DWORD cost;
        NLM_DATAPLAN_STATUS status;

        hr = INetworkCostManager_GetCost( cost_mgr, NULL, NULL );
        ok( hr == E_POINTER, "got %08lx\n", hr );

        cost = 0xdeadbeef;
        hr = INetworkCostManager_GetCost( cost_mgr, &cost, NULL );
        ok( hr == S_OK, "got %08lx\n", hr );
        ok( cost != 0xdeadbeef, "cost not set\n" );

        hr = INetworkCostManager_GetDataPlanStatus( cost_mgr, NULL, NULL );
        ok( hr == E_POINTER, "got %08lx\n", hr );

        hr = INetworkCostManager_GetDataPlanStatus( cost_mgr, &status, NULL );
        ok( hr == S_OK, "got %08lx\n", hr );

        INetworkCostManager_Release( cost_mgr );
    }

    hr = INetworkListManager_QueryInterface( mgr, &IID_IConnectionPointContainer, (void**)&cpc );
    ok( hr == S_OK, "got %08lx\n", hr );

    ref1 = IConnectionPointContainer_AddRef( cpc );

    hr = IConnectionPointContainer_FindConnectionPoint( cpc, &IID_INetworkListManagerEvents, &pt );
    ok( hr == S_OK, "got %08lx\n", hr );

    ref2 = IConnectionPointContainer_AddRef( cpc );
    ok( ref2 == ref1 + 2, "Expected refcount %ld, got %ld\n", ref1 + 2, ref2 );

    IConnectionPointContainer_Release( cpc );
    IConnectionPointContainer_Release( cpc );

    hr = IConnectionPoint_GetConnectionPointContainer( pt, &cpc2 );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( cpc2 == cpc, "Expected cpc2 == %p, but got %p\n", cpc, cpc2 );
    IConnectionPointContainer_Release( cpc2 );

    memset( &iid, 0, sizeof(iid) );
    hr = IConnectionPoint_GetConnectionInterface( pt, &iid );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( !memcmp( &iid, &IID_INetworkListManagerEvents, sizeof(iid) ),
        "Expected iid to be IID_INetworkListManagerEvents\n" );

    hr = IConnectionPoint_Advise( pt, (IUnknown*)&mgr_sink_unk, &cookie);
    ok( hr == CONNECT_E_CANNOTCONNECT, "Advise failed: %08lx\n", hr );

    init_dynamic_sink( &sink, mgr, pt );
    hr = IConnectionPoint_Advise( pt, (IUnknown *)&sink.INetworkListManagerEvents_iface, &cookie );
    ok( hr == S_OK, "Advise failed: %08lx\n", hr );
    ok( sink.refs > 1, "expected marshaled sink reference, got %ld\n", sink.refs );

    hr = IConnectionPoint_Unadvise( pt, 0xdeadbeef );
    ok( hr == OLE_E_NOCONNECTION || hr == CONNECT_E_NOCONNECTION, "Unadvise failed: %08lx\n", hr );

    hr = IConnectionPoint_Unadvise( pt, cookie );
    ok( hr == S_OK, "Unadvise failed: %08lx\n", hr );
    ok( sink.refs == 1, "expected sink reference release, got %ld\n", sink.refs );

    second_mgr = NULL;
    hr = CoCreateInstance( &CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                           &IID_INetworkListManager, (void **)&second_mgr );
    ok( hr == S_OK, "failed to create concurrent manager, hr %#lx\n", hr );
    ok( second_mgr != mgr, "expected independent manager instances\n" );
    if (second_mgr) INetworkListManager_Release( second_mgr );

    hr = IConnectionPointContainer_FindConnectionPoint( cpc, &IID_INetworkListManagerEvents, &pt2 );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok( pt == pt2, "pt != pt2\n");
    IConnectionPoint_Release( pt2 );

    hr = IConnectionPointContainer_FindConnectionPoint( cpc, &IID_INetworkCostManagerEvents, &pt );
    ok( hr == S_OK || hr == CONNECT_E_NOCONNECTION, "got %08lx\n", hr );
    if (hr == S_OK) IConnectionPoint_Release( pt );

    hr = IConnectionPointContainer_FindConnectionPoint( cpc, &IID_INetworkConnectionEvents, &pt );
    ok( hr == S_OK || hr == CONNECT_E_NOCONNECTION, "got %08lx\n", hr );
    if (hr == S_OK) IConnectionPoint_Release( pt );

    hr = IConnectionPointContainer_FindConnectionPoint( cpc, &IID_INetworkEvents, &pt );
    ok( hr == S_OK, "got %08lx\n", hr );
    IConnectionPoint_Release( pt );
    IConnectionPointContainer_Release( cpc );

    network_iter = NULL;
    hr = INetworkListManager_GetNetworks( mgr, NLM_ENUM_NETWORK_ALL, &network_iter );
    ok( hr == S_OK, "got %08lx\n", hr );
    ok(network_iter != NULL, "network_iter not set\n");
    hr = IEnumNetworks_Next( network_iter, 0, NULL, NULL );
    ok( hr == E_POINTER, "got %08lx\n", hr );
    network = (INetwork *)0xdeadbeef;
    while ((hr = IEnumNetworks_Next( network_iter, 1, &network, NULL )) == S_OK)
    {
        ok( network != (INetwork *)0xdeadbeef, "network not set\n" );
        connected = 1;
        hr = INetwork_get_IsConnected( network, &connected );
        ok( hr == S_OK, "got %08lx\n", hr );
        ok( connected == -1 || connected == 0, "got %d\n", connected );
        INetwork_Release( network );
        network = (INetwork *)0xdeadbeef;
    }
    ok( hr == S_FALSE, "got %08lx\n", hr );
    ok( network == NULL, "network not set\n" );
    IEnumNetworks_Release( network_iter );

    hr = INetworkListManager_GetNetworks( mgr, NLM_ENUM_NETWORK_CONNECTED, &network_iter );
    ok( hr == S_OK, "got %08lx\n", hr );
    while ((hr = IEnumNetworks_Next( network_iter, 1, &network, NULL )) == S_OK)
    {
        connected = 0;
        hr = INetwork_get_IsConnected( network, &connected );
        ok( hr == S_OK, "got %08lx\n", hr );
        ok( connected == -1, "got %d\n", connected );
        INetwork_Release( network );
    }
    IEnumNetworks_Release( network_iter );

    hr = INetworkListManager_GetNetworks( mgr, NLM_ENUM_NETWORK_DISCONNECTED, &network_iter );
    ok( hr == S_OK, "got %08lx\n", hr );
    while ((hr = IEnumNetworks_Next( network_iter, 1, &network, NULL )) == S_OK)
    {
        connected = 1;
        hr = INetwork_get_IsConnected( network, &connected );
        ok( hr == S_OK, "got %08lx\n", hr );
        ok( connected == 0 || broken(connected == -1) /* win11 */, "got %d\n", connected );
        INetwork_Release( network );
    }
    IEnumNetworks_Release( network_iter );

    conn_iter = NULL;
    hr = INetworkListManager_GetNetworkConnections( mgr, &conn_iter );
    ok( hr == S_OK, "got %08lx\n", hr );
    if (conn_iter)
    {
        fetched = 256;
        hr = IEnumNetworkConnections_Next( conn_iter, 1, NULL, &fetched );
        ok( hr == E_POINTER, "got hr %#lx.\n", hr );
        ok( fetched == 256, "got wrong feteched: %ld.\n", fetched );

        hr = IEnumNetworkConnections_Next( conn_iter, 0, NULL, &fetched );
        ok( hr == E_POINTER, "got hr %#lx.\n", hr );
        ok( fetched == 256, "got wrong feteched: %ld.\n", fetched );

        while ((hr = IEnumNetworkConnections_Next( conn_iter, 1, &conn, NULL )) == S_OK)
        {
            test_INetworkConnection( conn );
            INetworkConnection_Release( conn );
            conn = (void *)0xdeadbeef;
        }
        ok( !conn, "got wrong pointer: %p.\n", conn );
        IEnumNetworkConnections_Release( conn_iter );
    }

    /* cps and their container share the same ref count */
    IConnectionPoint_AddRef( pt );
    IConnectionPoint_AddRef( pt );

    ref1 = IConnectionPoint_Release( pt );
    ref2 = INetworkListManager_Release( mgr );
    ok( ref2 == ref1 - 1, "ref = %lu\n", ref1 );

    IConnectionPoint_Release( pt );
    ref1 = IConnectionPoint_Release( pt );
    ok( !ref1, "ref = %lu\n", ref1 );
}

static void write_marker( const char *dir, const char *name )
{
    char path[MAX_PATH];
    HANDLE file;

    snprintf( path, sizeof(path), "%s\\%s", dir, name );
    file = CreateFileA( path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL );
    ok( file != INVALID_HANDLE_VALUE, "failed to create marker %s, error %lu\n", path, GetLastError() );
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
}

static void pump_messages( DWORD timeout )
{
    MSG msg;

    MsgWaitForMultipleObjects( 0, NULL, FALSE, timeout, QS_ALLINPUT );
    while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
}

static BOOL wait_for_connectivity( INetworkListManager *mgr, NLM_CONNECTIVITY expected, BOOL equal,
                                   NLM_CONNECTIVITY *value )
{
    unsigned int i;

    for (i = 0; i < 200; i++)
    {
        if (SUCCEEDED(INetworkListManager_GetConnectivity( mgr, value )) &&
            ((*value == expected) == equal)) return TRUE;
        pump_messages( 50 );
    }
    return FALSE;
}

static BOOL wait_for_marker( const char *dir, const char *name )
{
    char path[MAX_PATH];
    unsigned int i;

    snprintf( path, sizeof(path), "%s\\%s", dir, name );
    for (i = 0; i < 200; i++)
    {
        if (GetFileAttributesA( path ) != INVALID_FILE_ATTRIBUTES) return TRUE;
        Sleep( 50 );
    }
    return FALSE;
}

static BOOL wait_for_callback_count( struct dynamic_sink *sink, LONG expected )
{
    unsigned int i;

    for (i = 0; i < 200; i++)
    {
        if (sink->callback_count == expected) return TRUE;
        pump_messages( 50 );
    }
    return FALSE;
}

static BOOL wait_for_sink_refs( struct dynamic_sink *sink, LONG expected )
{
    unsigned int i;

    for (i = 0; i < 200; i++)
    {
        if (sink->refs == expected) return TRUE;
        pump_messages( 50 );
    }
    return FALSE;
}

static void test_dynamic_connectivity( const char *marker_dir )
{
    struct dynamic_sink sink;
    IConnectionPointContainer *container;
    IConnectionPoint *connection_point;
    IEnumNetworks *networks;
    INetworkListManager *mgr;
    INetwork *network, *same_network;
    NLM_CONNECTIVITY initial, without_ipv4, without_ipv4_internet, without_ipv6_internet, current;
    GUID id;
    DWORD cookie;
    HRESULT hr;

    hr = CoCreateInstance( &CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                           &IID_INetworkListManager, (void **)&mgr );
    ok( hr == S_OK, "failed to create manager, hr %#lx\n", hr );
    if (FAILED(hr)) return;

    hr = INetworkListManager_GetConnectivity( mgr, &initial );
    ok( hr == S_OK, "initial GetConnectivity failed, hr %#lx\n", hr );
    ok( !(initial & NLM_CONNECTIVITY_IPV6_INTERNET),
        "reported IPv6 Internet without a default route, value %#x\n", initial );
    write_marker( marker_dir, "initial_no_ipv6_route" );
    ok( wait_for_marker( marker_dir, "initial_ipv6_route_added" ),
        "timed out waiting for initial IPv6 route\n" );
    INetworkListManager_Release( mgr );

    hr = CoCreateInstance( &CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                           &IID_INetworkListManager, (void **)&mgr );
    ok( hr == S_OK, "failed to recreate manager, hr %#lx\n", hr );
    if (FAILED(hr)) return;

    hr = INetworkListManager_GetNetworks( mgr, NLM_ENUM_NETWORK_ALL, &networks );
    ok( hr == S_OK, "GetNetworks failed, hr %#lx\n", hr );
    hr = IEnumNetworks_Next( networks, 1, &network, NULL );
    ok( hr == S_OK, "expected a network, hr %#lx\n", hr );
    IEnumNetworks_Release( networks );
    if (hr != S_OK)
    {
        INetworkListManager_Release( mgr );
        return;
    }
    INetwork_GetNetworkId( network, &id );

    hr = INetworkListManager_QueryInterface( mgr, &IID_IConnectionPointContainer, (void **)&container );
    ok( hr == S_OK, "QueryInterface failed, hr %#lx\n", hr );
    hr = IConnectionPointContainer_FindConnectionPoint( container, &IID_INetworkListManagerEvents,
                                                        &connection_point );
    ok( hr == S_OK, "FindConnectionPoint failed, hr %#lx\n", hr );
    IConnectionPointContainer_Release( container );

    init_dynamic_sink( &sink, mgr, connection_point );
    hr = IConnectionPoint_Advise( connection_point,
                                  (IUnknown *)&sink.INetworkListManagerEvents_iface, &cookie );
    ok( hr == S_OK, "Advise failed, hr %#lx\n", hr );
    sink.cookie = cookie;

    hr = INetworkListManager_GetConnectivity( mgr, &initial );
    ok( hr == S_OK && initial != NLM_CONNECTIVITY_DISCONNECTED,
        "expected initial connectivity, hr %#lx, value %#x\n", hr, initial );
    ok( initial & NLM_CONNECTIVITY_IPV6_INTERNET,
        "expected initial IPv6 Internet connectivity, value %#x\n", initial );
    write_marker( marker_dir, "ready" );

    without_ipv4_internet = initial & ~NLM_CONNECTIVITY_IPV4_INTERNET;
    without_ipv4_internet |= NLM_CONNECTIVITY_IPV4_LOCALNETWORK;
    ok( wait_for_connectivity( mgr, without_ipv4_internet, TRUE, &current ),
        "IPv4 default route removal did not remove IPv4 Internet connectivity, value %#x\n", current );
    write_marker( marker_dir, "route_removed" );
    ok( wait_for_connectivity( mgr, initial, TRUE, &current ),
        "route restoration did not restore connectivity, value %#x\n", current );
    write_marker( marker_dir, "route_restored" );

    without_ipv6_internet = initial & ~NLM_CONNECTIVITY_IPV6_INTERNET;
    without_ipv6_internet |= NLM_CONNECTIVITY_IPV6_LOCALNETWORK;
    ok( wait_for_connectivity( mgr, without_ipv6_internet, TRUE, &current ),
        "IPv6 route removal did not remove IPv6 Internet connectivity, value %#x\n", current );
    write_marker( marker_dir, "ipv6_route_removed" );
    ok( wait_for_connectivity( mgr, initial, TRUE, &current ),
        "IPv6 route restoration did not restore connectivity, value %#x\n", current );
    write_marker( marker_dir, "ipv6_route_restored" );

    without_ipv4 = initial & ~(NLM_CONNECTIVITY_IPV4_LOCALNETWORK | NLM_CONNECTIVITY_IPV4_INTERNET);
    ok( wait_for_connectivity( mgr, without_ipv4, TRUE, &current ),
        "address removal did not remove IPv4 connectivity, value %#x\n", current );
    write_marker( marker_dir, "address_removed" );
    ok( wait_for_connectivity( mgr, initial, TRUE, &current ),
        "address restoration did not restore connectivity, value %#x\n", current );
    write_marker( marker_dir, "address_restored" );

    ok( wait_for_connectivity( mgr, NLM_CONNECTIVITY_DISCONNECTED, TRUE, &current ),
        "link down did not disconnect, value %#x\n", current );
    ok( wait_for_callback_count( &sink, 7 ), "link-down callback was not delivered, count %ld\n",
        sink.callback_count );
    sink.unadvise_on_callback = TRUE;
    write_marker( marker_dir, "down" );
    ok( wait_for_connectivity( mgr, initial, TRUE, &current ), "connectivity did not recover, value %#x\n", current );
    ok( current == initial, "connectivity changed from %#x to %#x\n", initial, current );

    same_network = NULL;
    hr = INetworkListManager_GetNetwork( mgr, id, &same_network );
    ok( hr == S_OK, "GetNetwork failed, hr %#lx\n", hr );
    ok( same_network == network, "network identity changed, %p != %p\n", same_network, network );
    if (same_network) INetwork_Release( same_network );

    ok( sink.callback_count == 8, "expected eight callbacks, got %ld\n", sink.callback_count );
    ok( !sink.callback_mismatch, "callback reentrant GetConnectivity mismatched %ld times\n",
        sink.callback_mismatch );
    ok( !sink.callback_thread_mismatch, "callback ran outside the advising STA %ld times\n",
        sink.callback_thread_mismatch );
    ok( sink.callback_connectivity != NLM_CONNECTIVITY_DISCONNECTED,
        "recovery callback reported disconnected connectivity\n" );
    ok( sink.unadvise_hr == S_OK && !sink.cookie,
        "reentrant Unadvise failed, hr %#lx, cookie %lu\n", sink.unadvise_hr, sink.cookie );

    IConnectionPoint_Release( connection_point );
    INetwork_Release( network );
    INetworkListManager_Release( mgr );
    ok( wait_for_sink_refs( &sink, 1 ), "sink references were not released, refs %ld\n", sink.refs );
    ok( INetworkListManagerEvents_Release( &sink.INetworkListManagerEvents_iface ) == 0,
        "sink still referenced after teardown\n" );
}

static void test_reachability( const char *marker_dir )
{
    struct dynamic_sink sink;
    IConnectionPointContainer *container;
    IConnectionPoint *connection_point;
    INetworkListManager *mgr, *second_mgr;
    NLM_CONNECTIVITY initial, local, current;
    DWORD cookie;
    HRESULT hr;

    hr = CoCreateInstance( &CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                           &IID_INetworkListManager, (void **)&mgr );
    ok( hr == S_OK, "failed to create manager, hr %#lx\n", hr );
    if (FAILED(hr)) return;

    hr = INetworkListManager_GetConnectivity( mgr, &initial );
    ok( hr == S_OK, "GetConnectivity failed, hr %#lx\n", hr );
    ok( initial & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET),
        "expected initial Internet connectivity, got %#x\n", initial );
    local = initial;
    if (local & NLM_CONNECTIVITY_IPV4_INTERNET)
    {
        local &= ~NLM_CONNECTIVITY_IPV4_INTERNET;
        local |= NLM_CONNECTIVITY_IPV4_LOCALNETWORK;
    }
    if (local & NLM_CONNECTIVITY_IPV6_INTERNET)
    {
        local &= ~NLM_CONNECTIVITY_IPV6_INTERNET;
        local |= NLM_CONNECTIVITY_IPV6_LOCALNETWORK;
    }

    second_mgr = NULL;
    hr = CoCreateInstance( &CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                           &IID_INetworkListManager, (void **)&second_mgr );
    ok( hr == S_OK, "failed to create second manager, hr %#lx\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = INetworkListManager_GetConnectivity( second_mgr, &current );
        ok( hr == S_OK && current == initial,
            "second manager has wrong initial connectivity, hr %#lx, value %#x\n", hr, current );
    }

    hr = INetworkListManager_QueryInterface( mgr, &IID_IConnectionPointContainer, (void **)&container );
    ok( hr == S_OK, "QueryInterface failed, hr %#lx\n", hr );
    hr = IConnectionPointContainer_FindConnectionPoint( container, &IID_INetworkListManagerEvents,
                                                        &connection_point );
    ok( hr == S_OK, "FindConnectionPoint failed, hr %#lx\n", hr );
    IConnectionPointContainer_Release( container );

    init_dynamic_sink( &sink, mgr, connection_point );
    hr = IConnectionPoint_Advise( connection_point,
                                  (IUnknown *)&sink.INetworkListManagerEvents_iface, &cookie );
    ok( hr == S_OK, "Advise failed, hr %#lx\n", hr );
    sink.cookie = cookie;
    write_marker( marker_dir, "ready" );

    ok( wait_for_connectivity( mgr, local, TRUE, &current ),
        "LIMITED did not suppress Internet connectivity, value %#x\n", current );
    if (second_mgr)
        ok( wait_for_connectivity( second_mgr, local, TRUE, &current ),
            "second manager did not observe LIMITED, value %#x\n", current );
    ok( wait_for_callback_count( &sink, 1 ), "LIMITED callback was not delivered, count %ld\n",
        sink.callback_count );
    write_marker( marker_dir, "limited" );
    ok( wait_for_connectivity( mgr, initial, TRUE, &current ),
        "UNKNOWN did not restore topology fallback, value %#x\n", current );
    ok( wait_for_callback_count( &sink, 2 ), "UNKNOWN callback was not delivered, count %ld\n",
        sink.callback_count );
    write_marker( marker_dir, "unknown" );

    ok( wait_for_marker( marker_dir, "disabled" ), "timed out waiting for disabled state\n" );
    pump_messages( 500 );
    hr = INetworkListManager_GetConnectivity( mgr, &current );
    ok( hr == S_OK && current == initial,
        "disabled connectivity check changed fallback, hr %#lx, value %#x\n", hr, current );
    ok( sink.callback_count == 2, "disabled connectivity check produced a callback, count %ld\n",
        sink.callback_count );
    write_marker( marker_dir, "disabled_checked" );

    ok( wait_for_connectivity( mgr, local, TRUE, &current ),
        "probe-backed NONE did not suppress Internet connectivity, value %#x\n", current );
    write_marker( marker_dir, "enabled" );
    ok( wait_for_connectivity( mgr, initial, TRUE, &current ),
        "service loss did not restore topology fallback, value %#x\n", current );
    write_marker( marker_dir, "stopped" );
    ok( wait_for_connectivity( mgr, local, TRUE, &current ),
        "restarted PORTAL service did not suppress Internet connectivity, value %#x\n", current );
    write_marker( marker_dir, "restarted" );
    ok( wait_for_connectivity( mgr, initial, TRUE, &current ),
        "FULL did not restore Internet connectivity, value %#x\n", current );
    write_marker( marker_dir, "full" );

    ok( wait_for_connectivity( mgr, local, TRUE, &current ),
        "PORTAL before bus loss did not suppress Internet connectivity, value %#x\n", current );
    write_marker( marker_dir, "bus_portal" );
    ok( wait_for_connectivity( mgr, initial, TRUE, &current ),
        "bus loss did not restore topology fallback, value %#x\n", current );
    write_marker( marker_dir, "bus_stopped" );
    ok( wait_for_connectivity( mgr, local, TRUE, &current ),
        "bus and service restart did not restore passive reachability, value %#x\n", current );
    ok( wait_for_callback_count( &sink, 9 ), "bus-restart callback was not delivered, count %ld\n",
        sink.callback_count );
    sink.unadvise_on_callback = sink.teardown_on_callback = TRUE;
    write_marker( marker_dir, "bus_restarted" );

    ok( wait_for_callback_count( &sink, 10 ), "expected ten callbacks, got %ld\n", sink.callback_count );
    ok( !sink.callback_mismatch, "callback reentrant GetConnectivity mismatched %ld times\n",
        sink.callback_mismatch );
    ok( !sink.callback_thread_mismatch, "callback ran outside the advising STA %ld times\n",
        sink.callback_thread_mismatch );
    ok( sink.callback_connectivity == initial, "last callback %#x, expected %#x\n",
        sink.callback_connectivity, initial );
    ok( sink.unadvise_hr == S_OK && !sink.cookie,
        "reentrant Unadvise failed, hr %#lx, cookie %lu\n", sink.unadvise_hr, sink.cookie );
    ok( !sink.mgr && !sink.connection_point, "callback did not tear down the manager\n" );

    if (second_mgr)
    {
        ok( wait_for_connectivity( second_mgr, initial, TRUE, &current ),
            "second manager did not follow final FULL transition, value %#x\n", current );
        INetworkListManager_Release( second_mgr );
    }
    if (sink.cookie) IConnectionPoint_Unadvise( connection_point, sink.cookie );
    if (sink.connection_point) IConnectionPoint_Release( sink.connection_point );
    if (sink.mgr) INetworkListManager_Release( sink.mgr );
    ok( wait_for_sink_refs( &sink, 1 ), "sink references were not released, refs %ld\n", sink.refs );
    ok( INetworkListManagerEvents_Release( &sink.INetworkListManagerEvents_iface ) == 0,
        "sink still referenced after teardown\n" );

    second_mgr = NULL;
    hr = CoCreateInstance( &CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER,
                           &IID_INetworkListManager, (void **)&second_mgr );
    ok( hr == S_OK, "failed to recreate manager after teardown, hr %#lx\n", hr );
    if (second_mgr)
    {
        INetworkListManager_Release( second_mgr );
    }
}

START_TEST( list )
{
    char dynamic_dir[MAX_PATH] = {0}, reachability_dir[MAX_PATH] = {0};

    GetEnvironmentVariableA( "WINETEST_NETPROFM_DYNAMIC_DIR", dynamic_dir, ARRAY_SIZE(dynamic_dir) );
    GetEnvironmentVariableA( "WINETEST_NETPROFM_REACHABILITY_DIR", reachability_dir,
                             ARRAY_SIZE(reachability_dir) );
    CoInitialize( NULL );
    test_INetworkListManager();
    if (dynamic_dir[0]) test_dynamic_connectivity( dynamic_dir );
    if (reachability_dir[0]) test_reachability( reachability_dir );
    CoUninitialize();
}
