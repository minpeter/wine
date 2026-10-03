/*
 * Copyright 2014 Hans Leidekker for CodeWeavers
 * Copyright 2015 Michael Müller
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

#define COBJMACROS

#include <stdarg.h>
#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winsock2.h"
#include "ws2ipdef.h"
#include "iphlpapi.h"
#include "ifdef.h"
#include "netioapi.h"
#include "netiodef.h"
#include "objbase.h"
#include "ocidl.h"
#include "olectl.h"
#include "cguid.h"
#include "initguid.h"
#include "netlistmgr.h"

#include "wine/debug.h"
#include "wine/list.h"
#include "wine/nsi.h"
#include "wine/unixlib.h"
#include "netprofm_private.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(netprofm);

static const NPI_MODULEID npi_ndis_module =
    {sizeof(NPI_MODULEID), MIT_GUID, {{0xeb004a11, 0x9b1a, 0x11d4, {0x91, 0x23, 0x00, 0x50, 0x04, 0x77, 0x59, 0xbc}}}};
static const NPI_MODULEID npi_ipv4_module =
    {sizeof(NPI_MODULEID), MIT_GUID, {{0xeb004a00, 0x9b1a, 0x11d4, {0x91, 0x23, 0x00, 0x50, 0x04, 0x77, 0x59, 0xbc}}}};
static const NPI_MODULEID npi_ipv6_module =
    {sizeof(NPI_MODULEID), MIT_GUID, {{0xeb004a01, 0x9b1a, 0x11d4, {0x91, 0x23, 0x00, 0x50, 0x04, 0x77, 0x59, 0xbc}}}};

struct network
{
    INetwork             INetwork_iface;
    LONG                 refs;
    struct list          entry;
    GUID                 id;
    INetworkListManager *mgr;
    VARIANT_BOOL         connected_to_internet_v4;
    VARIANT_BOOL         connected_to_internet_v6;
    VARIANT_BOOL         connected_v4;
    VARIANT_BOOL         connected_v6;
};

struct connection
{
    INetworkConnection     INetworkConnection_iface;
    INetworkConnectionCost INetworkConnectionCost_iface;
    LONG                   refs;
    struct list            entry;
    GUID                   id;
    INetwork              *network;
    INetworkListManager   *mgr;
    VARIANT_BOOL           connected_to_internet_v4;
    VARIANT_BOOL           connected_to_internet_v6;
    VARIANT_BOOL           connected_v4;
    VARIANT_BOOL           connected_v6;
};

struct connection_point
{
    IConnectionPoint IConnectionPoint_iface;
    IConnectionPointContainer *container;
    IID iid;
    struct list sinks;
    DWORD cookie;
};

struct list_manager
{
    INetworkListManager INetworkListManager_iface;
    INetworkCostManager INetworkCostManager_iface;
    IConnectionPointContainer IConnectionPointContainer_iface;
    LONG                refs;
    struct list         networks;
    struct list         connections;
    struct connection_point list_mgr_cp;
    struct connection_point cost_mgr_cp;
    struct connection_point conn_mgr_cp;
    struct connection_point events_cp;
    CRITICAL_SECTION    cs;
    CRITICAL_SECTION    notify_cs;
    HANDLE              stop_event;
    HANDLE              monitor_ready_event;
    HANDLE              worker;
    DWORD               worker_tid;
    HANDLE              reachability_stop_event;
    HANDLE              reachability_worker;
    DWORD               reachability_worker_tid;
    UINT64              reachability_handle;
    enum reachability_state reachability;
    LONG                destroy_pending;
    DWORD               destroy_tid;
};

struct sink_entry
{
    struct list entry;
    DWORD cookie;
    DWORD git_cookie;
};

static HRESULT start_monitor( struct list_manager *mgr );
static void stop_monitor( struct list_manager *mgr );
static void start_reachability_monitor( struct list_manager *mgr );
static void stop_reachability_monitor( struct list_manager *mgr );
static NLM_CONNECTIVITY get_connectivity( struct list_manager *mgr );

static inline struct list_manager *impl_from_IConnectionPointContainer(IConnectionPointContainer *iface)
{
    return CONTAINING_RECORD(iface, struct list_manager, IConnectionPointContainer_iface);
}

static inline struct list_manager *impl_from_INetworkCostManager(
    INetworkCostManager *iface )
{
    return CONTAINING_RECORD( iface, struct list_manager, INetworkCostManager_iface );
}

static inline struct connection_point *impl_from_IConnectionPoint(
    IConnectionPoint *iface )
{
    return CONTAINING_RECORD( iface, struct connection_point, IConnectionPoint_iface );
}

static HRESULT WINAPI connection_point_QueryInterface(
    IConnectionPoint *iface,
    REFIID riid,
    void **obj )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %s, %p\n", cp, debugstr_guid(riid), obj );

    if (IsEqualGUID( riid, &IID_IConnectionPoint ) ||
        IsEqualGUID( riid, &IID_IUnknown ))
    {
        *obj = iface;
    }
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
    IConnectionPoint_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI connection_point_AddRef(
    IConnectionPoint *iface )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    return IConnectionPointContainer_AddRef( cp->container );
}

static ULONG WINAPI connection_point_Release(
    IConnectionPoint *iface )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    return IConnectionPointContainer_Release( cp->container );
}

static HRESULT WINAPI connection_point_GetConnectionInterface(
    IConnectionPoint *iface,
    IID *iid )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %p\n", cp, iid );

    if (!iid)
        return E_POINTER;

    memcpy( iid, &cp->iid, sizeof(*iid) );
    return S_OK;
}

static HRESULT WINAPI connection_point_GetConnectionPointContainer(
    IConnectionPoint *iface,
    IConnectionPointContainer **container )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %p\n", cp, container );

    if (!container)
        return E_POINTER;

    IConnectionPointContainer_AddRef( cp->container );
    *container = cp->container;
    return S_OK;
}

static HRESULT WINAPI connection_point_Advise(
    IConnectionPoint *iface,
    IUnknown *sink,
    DWORD *cookie )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    struct sink_entry *sink_entry;
    IGlobalInterfaceTable *git;
    IUnknown *unk;
    HRESULT hr;

    TRACE( "%p, %p, %p\n", cp, sink, cookie );

    if (!sink || !cookie)
        return E_POINTER;

    sink_entry = malloc( sizeof(*sink_entry) );
    if (!sink_entry) return E_OUTOFMEMORY;

    hr = IUnknown_QueryInterface( sink, &cp->iid, (void **)&unk );
    if (FAILED(hr))
    {
        WARN( "iface %s not implemented by sink\n", debugstr_guid(&cp->iid) );
        free( sink_entry );
        return CONNECT_E_CANNOTCONNECT;
    }

    hr = CoCreateInstance( &CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IGlobalInterfaceTable, (void **)&git );
    if (SUCCEEDED(hr))
    {
        hr = IGlobalInterfaceTable_RegisterInterfaceInGlobal( git, unk, &cp->iid,
                                                              &sink_entry->git_cookie );
        IGlobalInterfaceTable_Release( git );
    }
    IUnknown_Release( unk );
    if (FAILED(hr))
    {
        WARN( "failed to marshal sink for %s, hr %#lx\n", debugstr_guid(&cp->iid), hr );
        free( sink_entry );
        return hr;
    }

    EnterCriticalSection( &impl_from_IConnectionPointContainer( cp->container )->cs );
    *cookie = sink_entry->cookie = ++cp->cookie;
    list_add_tail( &cp->sinks, &sink_entry->entry );
    LeaveCriticalSection( &impl_from_IConnectionPointContainer( cp->container )->cs );
    return S_OK;
}

static void sink_entry_release( struct sink_entry *entry )
{
    IGlobalInterfaceTable *git;
    HRESULT hr;

    hr = CoCreateInstance( &CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IGlobalInterfaceTable, (void **)&git );
    if (SUCCEEDED(hr))
    {
        hr = IGlobalInterfaceTable_RevokeInterfaceFromGlobal( git, entry->git_cookie );
        IGlobalInterfaceTable_Release( git );
    }
    if (FAILED(hr)) WARN( "failed to revoke sink %#lx, hr %#lx\n", entry->git_cookie, hr );
    free( entry );
}

static HRESULT WINAPI connection_point_Unadvise(
    IConnectionPoint *iface,
    DWORD cookie )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    struct list_manager *mgr = impl_from_IConnectionPointContainer( cp->container );
    struct sink_entry *iter;

    TRACE( "%p, %ld\n", cp, cookie );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( iter, &cp->sinks, struct sink_entry, entry )
    {
        if (iter->cookie != cookie) continue;
        list_remove( &iter->entry );
        LeaveCriticalSection( &mgr->cs );
        sink_entry_release( iter );
        return S_OK;
    }
    LeaveCriticalSection( &mgr->cs );

    WARN( "invalid cookie\n" );
    return CONNECT_E_NOCONNECTION;
}

static HRESULT WINAPI connection_point_EnumConnections(
    IConnectionPoint *iface,
    IEnumConnections **connections )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    FIXME( "%p, %p - stub\n", cp, connections );

    return E_NOTIMPL;
}

static const IConnectionPointVtbl connection_point_vtbl =
{
    connection_point_QueryInterface,
    connection_point_AddRef,
    connection_point_Release,
    connection_point_GetConnectionInterface,
    connection_point_GetConnectionPointContainer,
    connection_point_Advise,
    connection_point_Unadvise,
    connection_point_EnumConnections
};

static void connection_point_init(
    struct connection_point *cp,
    REFIID riid,
    IConnectionPointContainer *container )
{
    cp->IConnectionPoint_iface.lpVtbl = &connection_point_vtbl;
    cp->container = container;
    cp->cookie = 0;
    cp->iid = *riid;
    list_init( &cp->sinks );
}

static void connection_point_release( struct connection_point *cp )
{
    while (!list_empty( &cp->sinks ))
    {
        struct sink_entry *entry = LIST_ENTRY( list_head( &cp->sinks ), struct sink_entry, entry );
        list_remove( &entry->entry );
        sink_entry_release( entry );
    }
}

static inline struct network *impl_from_INetwork(
    INetwork *iface )
{
    return CONTAINING_RECORD( iface, struct network, INetwork_iface );
}

static HRESULT WINAPI network_QueryInterface(
    INetwork *iface, REFIID riid, void **obj )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %s, %p\n", network, debugstr_guid(riid), obj );

    if (IsEqualIID( riid, &IID_INetwork ) ||
        IsEqualIID( riid, &IID_IDispatch ) ||
        IsEqualIID( riid, &IID_IUnknown ))
    {
        *obj = iface;
        INetwork_AddRef( iface );
        return S_OK;
    }
    else
    {
        WARN( "interface not supported %s\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI network_AddRef(
    INetwork *iface )
{
    struct network *network = impl_from_INetwork( iface );
    ULONG refs;

    TRACE( "%p\n", network );
    refs = InterlockedIncrement( &network->refs );
    if (refs == 2) INetworkListManager_AddRef( network->mgr );
    return refs;
}

static ULONG WINAPI network_Release(
    INetwork *iface )
{
    struct network *network = impl_from_INetwork( iface );
    LONG refs;

    TRACE( "%p\n", network );

    refs = InterlockedDecrement( &network->refs );
    if (refs == 1 && !INetworkListManager_Release( network->mgr )) refs = 0;
    else if (!refs)
    {
        list_remove( &network->entry );
        free( network );
    }
    return refs;
}

static HRESULT WINAPI network_GetTypeInfoCount(
    INetwork *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI network_GetTypeInfo(
    INetwork *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI network_GetIDsOfNames(
    INetwork *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI network_Invoke(
    INetwork *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI network_GetName(
    INetwork *iface,
    BSTR *pszNetworkName )
{
    FIXME( "%p, %p\n", iface, pszNetworkName );
    return E_NOTIMPL;
}

static HRESULT WINAPI network_SetName(
    INetwork *iface,
    BSTR szNetworkNewName )
{
    FIXME( "%p, %s\n", iface, debugstr_w(szNetworkNewName) );
    return E_NOTIMPL;
}

static HRESULT WINAPI network_GetDescription(
    INetwork *iface,
    BSTR *pszDescription )
{
    FIXME( "%p, %p\n", iface, pszDescription );
    return E_NOTIMPL;
}

static HRESULT WINAPI network_SetDescription(
    INetwork *iface,
    BSTR szDescription )
{
    FIXME( "%p, %s\n", iface, debugstr_w(szDescription) );
    return E_NOTIMPL;
}

static HRESULT WINAPI network_GetNetworkId(
    INetwork *iface,
    GUID *pgdGuidNetworkId )
{
    struct network *network = impl_from_INetwork( iface );

    TRACE( "%p, %p\n", iface, pgdGuidNetworkId );

    *pgdGuidNetworkId = network->id;
    return S_OK;
}

static HRESULT WINAPI network_GetDomainType(
    INetwork *iface,
    NLM_DOMAIN_TYPE *pDomainType )
{
    FIXME( "%p, %p\n", iface, pDomainType );

    *pDomainType = NLM_DOMAIN_TYPE_NON_DOMAIN_NETWORK;
    return S_OK;
}

static inline struct list_manager *impl_from_INetworkListManager(
    INetworkListManager *iface )
{
    return CONTAINING_RECORD( iface, struct list_manager, INetworkListManager_iface );
}

static HRESULT create_connections_enum(
    struct list_manager *, IEnumNetworkConnections** );

static HRESULT WINAPI network_GetNetworkConnections(
    INetwork *iface,
    IEnumNetworkConnections **ppEnum )
{
    struct network *network = impl_from_INetwork( iface );
    struct list_manager *mgr = impl_from_INetworkListManager( network->mgr );

    TRACE( "%p, %p\n", iface, ppEnum );
    return create_connections_enum( mgr, ppEnum );
}

static HRESULT WINAPI network_GetTimeCreatedAndConnected(
    INetwork *iface,
    DWORD *pdwLowDateTimeCreated,
    DWORD *pdwHighDateTimeCreated,
    DWORD *pdwLowDateTimeConnected,
    DWORD *pdwHighDateTimeConnected )
{
    FIXME( "%p, %p, %p, %p, %p\n", iface, pdwLowDateTimeCreated, pdwHighDateTimeCreated,
        pdwLowDateTimeConnected, pdwHighDateTimeConnected );
    return E_NOTIMPL;
}

static HRESULT WINAPI network_get_IsConnectedToInternet(
    INetwork *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct network *network = impl_from_INetwork( iface );
    struct list_manager *mgr = impl_from_INetworkListManager( network->mgr );

    TRACE( "%p, %p\n", iface, pbIsConnected );

    EnterCriticalSection( &mgr->cs );
    *pbIsConnected = mgr->reachability == REACHABILITY_OFFLINE ? VARIANT_FALSE :
            network->connected_to_internet_v4 | network->connected_to_internet_v6;
    LeaveCriticalSection( &mgr->cs );
    TRACE( "<- %#x\n", *pbIsConnected );
    return S_OK;
}

static HRESULT WINAPI network_get_IsConnected(
    INetwork *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct network *network = impl_from_INetwork( iface );
    struct list_manager *mgr = impl_from_INetworkListManager( network->mgr );

    TRACE( "%p, %p\n", iface, pbIsConnected );

    EnterCriticalSection( &mgr->cs );
    *pbIsConnected = network->connected_v4 | network->connected_v6;
    LeaveCriticalSection( &mgr->cs );
    TRACE( "<- %#x\n", *pbIsConnected );
    return S_OK;
}

static HRESULT WINAPI network_GetConnectivity(
    INetwork *iface,
    NLM_CONNECTIVITY *pConnectivity )
{
    struct network *network = impl_from_INetwork( iface );
    struct list_manager *mgr = impl_from_INetworkListManager( network->mgr );

    TRACE( "%p, %p\n", iface, pConnectivity );

    EnterCriticalSection( &mgr->cs );
    *pConnectivity = NLM_CONNECTIVITY_DISCONNECTED;

    if (network->connected_to_internet_v4 && mgr->reachability != REACHABILITY_OFFLINE)
        *pConnectivity |= NLM_CONNECTIVITY_IPV4_INTERNET;
    else if (network->connected_v4)
        *pConnectivity |= NLM_CONNECTIVITY_IPV4_LOCALNETWORK;

    if (network->connected_to_internet_v6 && mgr->reachability != REACHABILITY_OFFLINE)
        *pConnectivity |= NLM_CONNECTIVITY_IPV6_INTERNET;
    else if (network->connected_v6)
        *pConnectivity |= NLM_CONNECTIVITY_IPV6_LOCALNETWORK;
    LeaveCriticalSection( &mgr->cs );

    TRACE( "<- %#x\n", *pConnectivity );
    return S_OK;
}

static HRESULT WINAPI network_GetCategory(
    INetwork *iface,
    NLM_NETWORK_CATEGORY *pCategory )
{
    FIXME( "%p, %p\n", iface, pCategory );

    *pCategory = NLM_NETWORK_CATEGORY_PUBLIC;
    return S_OK;
}

static HRESULT WINAPI network_SetCategory(
    INetwork *iface,
    NLM_NETWORK_CATEGORY NewCategory )
{
    FIXME( "%p, %u\n", iface, NewCategory );
    return E_NOTIMPL;
}

static const struct INetworkVtbl network_vtbl =
{
    network_QueryInterface,
    network_AddRef,
    network_Release,
    network_GetTypeInfoCount,
    network_GetTypeInfo,
    network_GetIDsOfNames,
    network_Invoke,
    network_GetName,
    network_SetName,
    network_GetDescription,
    network_SetDescription,
    network_GetNetworkId,
    network_GetDomainType,
    network_GetNetworkConnections,
    network_GetTimeCreatedAndConnected,
    network_get_IsConnectedToInternet,
    network_get_IsConnected,
    network_GetConnectivity,
    network_GetCategory,
    network_SetCategory
};

static struct network *create_network( const GUID *id )
{
    struct network *ret;

    if (!(ret = calloc( 1, sizeof(*ret) ))) return NULL;

    ret->INetwork_iface.lpVtbl = &network_vtbl;
    ret->refs = 1;
    ret->id   = *id;
    list_init( &ret->entry );

    return ret;
}

static HRESULT WINAPI cost_manager_QueryInterface(
    INetworkCostManager *iface,
    REFIID riid,
    void **obj )
{
    struct list_manager *mgr = impl_from_INetworkCostManager( iface );
    return INetworkListManager_QueryInterface( &mgr->INetworkListManager_iface, riid, obj );
}

static ULONG WINAPI cost_manager_AddRef(
    INetworkCostManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkCostManager( iface );
    return INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
}

static ULONG WINAPI cost_manager_Release(
    INetworkCostManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkCostManager( iface );
    return INetworkListManager_Release( &mgr->INetworkListManager_iface );
}

static HRESULT WINAPI cost_manager_GetCost(
    INetworkCostManager *iface, DWORD *pCost, NLM_SOCKADDR *pDestIPAddr)
{
    FIXME( "%p, %p, %p\n", iface, pCost, pDestIPAddr );

    if (!pCost) return E_POINTER;

    *pCost = NLM_CONNECTION_COST_UNRESTRICTED;
    return S_OK;
}

static BOOL map_address_6to4( const SOCKADDR_IN6 *addr6, SOCKADDR_IN *addr4 )
{
    ULONG i;

    if (addr6->sin6_family != AF_INET6) return FALSE;

    for (i = 0; i < 5; i++)
        if (addr6->sin6_addr.u.Word[i]) return FALSE;

    if (addr6->sin6_addr.u.Word[5] != 0xffff) return FALSE;

    addr4->sin_family = AF_INET;
    addr4->sin_port   = addr6->sin6_port;
    addr4->sin_addr.S_un.S_addr = addr6->sin6_addr.u.Word[6] << 16 | addr6->sin6_addr.u.Word[7];
    memset( &addr4->sin_zero, 0, sizeof(addr4->sin_zero) );

    return TRUE;
}

static HRESULT WINAPI cost_manager_GetDataPlanStatus(
    INetworkCostManager *iface, NLM_DATAPLAN_STATUS *pDataPlanStatus,
    NLM_SOCKADDR *pDestIPAddr)
{
    DWORD ret, index;
    NET_LUID luid;
    SOCKADDR *dst = (SOCKADDR *)pDestIPAddr;
    SOCKADDR_IN addr4, *dst4;

    FIXME( "%p, %p, %p\n", iface, pDataPlanStatus, pDestIPAddr );

    if (!pDataPlanStatus) return E_POINTER;

    if (dst && ((dst->sa_family == AF_INET && (dst4 = (SOCKADDR_IN *)dst)) ||
               ((dst->sa_family == AF_INET6 && map_address_6to4( (const SOCKADDR_IN6 *)dst, &addr4 )
                && (dst4 = &addr4)))))
    {
        if ((ret = GetBestInterface( dst4->sin_addr.S_un.S_addr, &index )))
            return HRESULT_FROM_WIN32( ret );

        if ((ret = ConvertInterfaceIndexToLuid( index, &luid )))
            return HRESULT_FROM_WIN32( ret );

        if ((ret = ConvertInterfaceLuidToGuid( &luid, &pDataPlanStatus->InterfaceGuid )))
            return HRESULT_FROM_WIN32( ret );
    }
    else
    {
        FIXME( "interface guid not found\n" );
        memset( &pDataPlanStatus->InterfaceGuid, 0, sizeof(pDataPlanStatus->InterfaceGuid) );
    }

    pDataPlanStatus->UsageData.UsageInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
    memset( &pDataPlanStatus->UsageData.LastSyncTime, 0, sizeof(pDataPlanStatus->UsageData.LastSyncTime) );
    pDataPlanStatus->DataLimitInMegabytes       = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->InboundBandwidthInKbps     = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->OutboundBandwidthInKbps    = NLM_UNKNOWN_DATAPLAN_STATUS;
    memset( &pDataPlanStatus->NextBillingCycle, 0, sizeof(pDataPlanStatus->NextBillingCycle) );
    pDataPlanStatus->MaxTransferSizeInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->Reserved                   = 0;

    return S_OK;
}

static HRESULT WINAPI cost_manager_SetDestinationAddresses(
    INetworkCostManager *iface, UINT32 length, NLM_SOCKADDR *pDestIPAddrList,
    VARIANT_BOOL bAppend)
{
    FIXME( "%p, %u, %p, %x\n", iface, length, pDestIPAddrList, bAppend );
    return E_NOTIMPL;
}

static const INetworkCostManagerVtbl cost_manager_vtbl =
{
    cost_manager_QueryInterface,
    cost_manager_AddRef,
    cost_manager_Release,
    cost_manager_GetCost,
    cost_manager_GetDataPlanStatus,
    cost_manager_SetDestinationAddresses
};

struct networks_enum
{
    IEnumNetworks        IEnumNetworks_iface;
    LONG                 refs;
    struct list_manager *mgr;
    struct list         *cursor;
    NLM_ENUM_NETWORK     flags;
};

static inline struct networks_enum *impl_from_IEnumNetworks(
    IEnumNetworks *iface )
{
    return CONTAINING_RECORD( iface, struct networks_enum, IEnumNetworks_iface );
}

static HRESULT WINAPI networks_enum_QueryInterface(
    IEnumNetworks *iface, REFIID riid, void **obj )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p, %s, %p\n", iter, debugstr_guid(riid), obj );

    if (IsEqualIID( riid, &IID_IEnumNetworks ) ||
        IsEqualIID( riid, &IID_IDispatch ) ||
        IsEqualIID( riid, &IID_IUnknown ))
    {
        *obj = iface;
        IEnumNetworks_AddRef( iface );
        return S_OK;
    }
    else
    {
        WARN( "interface not supported %s\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI networks_enum_AddRef(
    IEnumNetworks *iface )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p\n", iter );
    return InterlockedIncrement( &iter->refs );
}

static ULONG WINAPI networks_enum_Release(
    IEnumNetworks *iface )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );
    LONG refs;

    TRACE( "%p\n", iter );

    if (!(refs = InterlockedDecrement( &iter->refs )))
    {
        INetworkListManager_Release( &iter->mgr->INetworkListManager_iface );
        free( iter );
    }
    return refs;
}

static HRESULT WINAPI networks_enum_GetTypeInfoCount(
    IEnumNetworks *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI networks_enum_GetTypeInfo(
    IEnumNetworks *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI networks_enum_GetIDsOfNames(
    IEnumNetworks *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI networks_enum_Invoke(
    IEnumNetworks *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI networks_enum_get__NewEnum(
    IEnumNetworks *iface, IEnumVARIANT **ppEnumVar )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static BOOL match_enum_network_flags( NLM_ENUM_NETWORK flags, struct network *network )
{
    if (flags == NLM_ENUM_NETWORK_ALL) return TRUE;
    if (network->connected_v4 || network->connected_v6)
    {
        if (flags & NLM_ENUM_NETWORK_CONNECTED) return TRUE;
    }
    else if (flags & NLM_ENUM_NETWORK_DISCONNECTED) return TRUE;
    return FALSE;
}

static HRESULT WINAPI networks_enum_Next(
    IEnumNetworks *iface, ULONG count, INetwork **ret, ULONG *fetched )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );
    ULONG i = 0;

    TRACE( "%p, %lu %p %p\n", iter, count, ret, fetched );

    if (!ret) return E_POINTER;
    *ret = NULL;
    if (fetched) *fetched = 0;
    if (!count) return S_OK;

    EnterCriticalSection( &iter->mgr->cs );
    while (iter->cursor && i < count)
    {
        struct network *network = LIST_ENTRY( iter->cursor, struct network, entry );
        if (match_enum_network_flags( iter->flags, network ))
        {
            ret[i] = &network->INetwork_iface;
            INetwork_AddRef( ret[i] );
            i++;
        }
        iter->cursor = list_next( &iter->mgr->networks, iter->cursor );
    }
    if (fetched) *fetched = i;
    LeaveCriticalSection( &iter->mgr->cs );

    return i < count ? S_FALSE : S_OK;
}

static HRESULT WINAPI networks_enum_Skip(
    IEnumNetworks *iface, ULONG count )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p, %lu\n", iter, count);

    if (!count) return S_OK;
    if (!iter->cursor) return S_FALSE;

    EnterCriticalSection( &iter->mgr->cs );
    for (;;)
    {
        struct network *network;
        iter->cursor = list_next( &iter->mgr->networks, iter->cursor );
        if (!iter->cursor) break;
        network = LIST_ENTRY( iter->cursor, struct network, entry );
        if (match_enum_network_flags( iter->flags, network )) count--;
        if (!count) break;
    }
    LeaveCriticalSection( &iter->mgr->cs );

    return count ? S_FALSE : S_OK;
}

static HRESULT WINAPI networks_enum_Reset(
    IEnumNetworks *iface )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p\n", iter );

    EnterCriticalSection( &iter->mgr->cs );
    iter->cursor = list_head( &iter->mgr->networks );
    LeaveCriticalSection( &iter->mgr->cs );
    return S_OK;
}

static HRESULT create_networks_enum(
    struct list_manager *, NLM_ENUM_NETWORK, IEnumNetworks ** );

static HRESULT WINAPI networks_enum_Clone(
    IEnumNetworks *iface, IEnumNetworks **ret )
{
    struct networks_enum *iter = impl_from_IEnumNetworks( iface );

    TRACE( "%p, %p\n", iter, ret );
    return create_networks_enum( iter->mgr, iter->flags, ret );
}

static const IEnumNetworksVtbl networks_enum_vtbl =
{
    networks_enum_QueryInterface,
    networks_enum_AddRef,
    networks_enum_Release,
    networks_enum_GetTypeInfoCount,
    networks_enum_GetTypeInfo,
    networks_enum_GetIDsOfNames,
    networks_enum_Invoke,
    networks_enum_get__NewEnum,
    networks_enum_Next,
    networks_enum_Skip,
    networks_enum_Reset,
    networks_enum_Clone
};

static HRESULT create_networks_enum(
    struct list_manager *mgr, NLM_ENUM_NETWORK flags, IEnumNetworks **ret )
{
    struct networks_enum *iter;

    *ret = NULL;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;

    iter->IEnumNetworks_iface.lpVtbl = &networks_enum_vtbl;
    EnterCriticalSection( &mgr->cs );
    iter->cursor = list_head( &mgr->networks );
    LeaveCriticalSection( &mgr->cs );
    iter->mgr    = mgr;
    INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
    iter->flags  = flags;
    iter->refs   = 1;

    *ret = &iter->IEnumNetworks_iface;
    return S_OK;
}

struct connections_enum
{
    IEnumNetworkConnections IEnumNetworkConnections_iface;
    LONG                    refs;
    struct list_manager    *mgr;
    struct list            *cursor;
};

static inline struct connections_enum *impl_from_IEnumNetworkConnections(
    IEnumNetworkConnections *iface )
{
    return CONTAINING_RECORD( iface, struct connections_enum, IEnumNetworkConnections_iface );
}

static HRESULT WINAPI connections_enum_QueryInterface(
    IEnumNetworkConnections *iface, REFIID riid, void **obj )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p, %s, %p\n", iter, debugstr_guid(riid), obj );

    if (IsEqualIID( riid, &IID_IEnumNetworkConnections ) ||
        IsEqualIID( riid, &IID_IDispatch ) ||
        IsEqualIID( riid, &IID_IUnknown ))
    {
        *obj = iface;
        IEnumNetworkConnections_AddRef( iface );
        return S_OK;
    }
    else
    {
        WARN( "interface not supported %s\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI connections_enum_AddRef(
    IEnumNetworkConnections *iface )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p\n", iter );
    return InterlockedIncrement( &iter->refs );
}

static ULONG WINAPI connections_enum_Release(
    IEnumNetworkConnections *iface )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );
    LONG refs;

    TRACE( "%p\n", iter );

    if (!(refs = InterlockedDecrement( &iter->refs )))
    {
        INetworkListManager_Release( &iter->mgr->INetworkListManager_iface );
        free( iter );
    }
    return refs;
}

static HRESULT WINAPI connections_enum_GetTypeInfoCount(
    IEnumNetworkConnections *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_GetTypeInfo(
    IEnumNetworkConnections *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_GetIDsOfNames(
    IEnumNetworkConnections *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_Invoke(
    IEnumNetworkConnections *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_get__NewEnum(
    IEnumNetworkConnections *iface, IEnumVARIANT **ppEnumVar )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connections_enum_Next(
    IEnumNetworkConnections *iface, ULONG count, INetworkConnection **ret, ULONG *fetched )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );
    ULONG i = 0;

    TRACE( "%p, %lu %p %p\n", iter, count, ret, fetched );

    if (!ret) return E_POINTER;
    *ret = NULL;
    if (fetched) *fetched = 0;
    if (!count) return S_OK;

    EnterCriticalSection( &iter->mgr->cs );
    while (iter->cursor && i < count)
    {
        struct connection *connection = LIST_ENTRY( iter->cursor, struct connection, entry );
        ret[i] = &connection->INetworkConnection_iface;
        INetworkConnection_AddRef( ret[i] );
        iter->cursor = list_next( &iter->mgr->connections, iter->cursor );
        i++;
    }
    if (fetched) *fetched = i;
    LeaveCriticalSection( &iter->mgr->cs );

    return i < count ? S_FALSE : S_OK;
}

static HRESULT WINAPI connections_enum_Skip(
    IEnumNetworkConnections *iface, ULONG count )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p, %lu\n", iter, count);

    if (!count) return S_OK;
    if (!iter->cursor) return S_FALSE;

    EnterCriticalSection( &iter->mgr->cs );
    while (count--)
    {
        iter->cursor = list_next( &iter->mgr->connections, iter->cursor );
        if (!iter->cursor) break;
    }
    LeaveCriticalSection( &iter->mgr->cs );

    return count ? S_FALSE : S_OK;
}

static HRESULT WINAPI connections_enum_Reset(
    IEnumNetworkConnections *iface )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p\n", iter );

    EnterCriticalSection( &iter->mgr->cs );
    iter->cursor = list_head( &iter->mgr->connections );
    LeaveCriticalSection( &iter->mgr->cs );
    return S_OK;
}

static HRESULT WINAPI connections_enum_Clone(
    IEnumNetworkConnections *iface, IEnumNetworkConnections **ret )
{
    struct connections_enum *iter = impl_from_IEnumNetworkConnections( iface );

    TRACE( "%p, %p\n", iter, ret );
    return create_connections_enum( iter->mgr, ret );
}

static const IEnumNetworkConnectionsVtbl connections_enum_vtbl =
{
    connections_enum_QueryInterface,
    connections_enum_AddRef,
    connections_enum_Release,
    connections_enum_GetTypeInfoCount,
    connections_enum_GetTypeInfo,
    connections_enum_GetIDsOfNames,
    connections_enum_Invoke,
    connections_enum_get__NewEnum,
    connections_enum_Next,
    connections_enum_Skip,
    connections_enum_Reset,
    connections_enum_Clone
};

static HRESULT create_connections_enum(
    struct list_manager *mgr, IEnumNetworkConnections **ret )
{
    struct connections_enum *iter;

    *ret = NULL;
    if (!(iter = calloc( 1, sizeof(*iter) ))) return E_OUTOFMEMORY;

    iter->IEnumNetworkConnections_iface.lpVtbl = &connections_enum_vtbl;
    iter->mgr         = mgr;
    INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
    EnterCriticalSection( &mgr->cs );
    iter->cursor      = list_head( &iter->mgr->connections );
    LeaveCriticalSection( &mgr->cs );
    iter->refs        = 1;

    *ret = &iter->IEnumNetworkConnections_iface;
    return S_OK;
}

static ULONG WINAPI list_manager_AddRef(
    INetworkListManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    return InterlockedIncrement( &mgr->refs );
}

static void list_manager_destroy( struct list_manager *mgr )
{
    struct network *network, *next_network;
    struct connection *connection, *next_connection;

    TRACE( "destroying %p\n", mgr );

    stop_reachability_monitor( mgr );
    stop_monitor( mgr );
    connection_point_release( &mgr->events_cp );
    connection_point_release( &mgr->conn_mgr_cp );
    connection_point_release( &mgr->cost_mgr_cp );
    connection_point_release( &mgr->list_mgr_cp );
    LIST_FOR_EACH_ENTRY_SAFE( connection, next_connection, &mgr->connections, struct connection, entry )
    {
        INetworkConnection_Release( &connection->INetworkConnection_iface );
    }
    LIST_FOR_EACH_ENTRY_SAFE( network, next_network, &mgr->networks, struct network, entry )
    {
        INetwork_Release( &network->INetwork_iface );
    }
    DeleteCriticalSection( &mgr->cs );
    DeleteCriticalSection( &mgr->notify_cs );
    free( mgr );
}

static ULONG WINAPI list_manager_Release(
    INetworkListManager *iface )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    LONG refs = InterlockedDecrement( &mgr->refs );
    if (!refs)
    {
        BOOL destroy_owner = !InterlockedCompareExchange( &mgr->destroy_pending, TRUE, FALSE );

        if (destroy_owner)
        {
            mgr->destroy_tid = GetCurrentThreadId();
            if (mgr->stop_event) SetEvent( mgr->stop_event );
            if (mgr->reachability_stop_event) SetEvent( mgr->reachability_stop_event );
        }
        if (destroy_owner && mgr->worker_tid != GetCurrentThreadId() &&
            mgr->reachability_worker_tid != GetCurrentThreadId())
            list_manager_destroy( mgr );
    }
    return refs;
}

static HRESULT WINAPI list_manager_QueryInterface(
    INetworkListManager *iface,
    REFIID riid,
    void **obj )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );

    TRACE( "%p, %s, %p\n", mgr, debugstr_guid(riid), obj );

    if (IsEqualGUID( riid, &IID_INetworkListManager ) ||
        IsEqualGUID( riid, &IID_IDispatch ) ||
        IsEqualGUID( riid, &IID_IUnknown ))
    {
        *obj = iface;
    }
    else if (IsEqualGUID( riid, &IID_INetworkCostManager ))
    {
        *obj = &mgr->INetworkCostManager_iface;
    }
    else if (IsEqualGUID( riid, &IID_IConnectionPointContainer ))
    {
        *obj = &mgr->IConnectionPointContainer_iface;
    }
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
    INetworkListManager_AddRef( iface );
    return S_OK;
}

static HRESULT WINAPI list_manager_GetTypeInfoCount(
    INetworkListManager *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI list_manager_GetTypeInfo(
    INetworkListManager *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI list_manager_GetIDsOfNames(
    INetworkListManager *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI list_manager_Invoke(
    INetworkListManager *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI list_manager_GetNetworks(
    INetworkListManager *iface,
    NLM_ENUM_NETWORK Flags,
    IEnumNetworks **ppEnumNetwork )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );

    TRACE( "%p, %x, %p\n", iface, Flags, ppEnumNetwork );

    return create_networks_enum( mgr, Flags, ppEnumNetwork );
}

static HRESULT WINAPI list_manager_GetNetwork(
    INetworkListManager *iface,
    GUID gdNetworkId,
    INetwork **ppNetwork )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct network *network;

    TRACE( "%p, %s, %p\n", iface, debugstr_guid(&gdNetworkId), ppNetwork );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (IsEqualGUID( &network->id, &gdNetworkId ))
        {
            *ppNetwork = &network->INetwork_iface;
            INetwork_AddRef( *ppNetwork );
            LeaveCriticalSection( &mgr->cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &mgr->cs );

    return S_FALSE;
}

static HRESULT WINAPI list_manager_GetNetworkConnections(
    INetworkListManager *iface,
    IEnumNetworkConnections **ppEnum )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );

    TRACE( "%p, %p\n", iface, ppEnum );
    return create_connections_enum( mgr, ppEnum );
}

static HRESULT WINAPI list_manager_GetNetworkConnection(
    INetworkListManager *iface,
    GUID gdNetworkConnectionId,
    INetworkConnection **ppNetworkConnection )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct connection *connection;

    TRACE( "%p, %s, %p\n", iface, debugstr_guid(&gdNetworkConnectionId),
            ppNetworkConnection );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( connection, &mgr->connections, struct connection, entry )
    {
        if (IsEqualGUID( &connection->id, &gdNetworkConnectionId ))
        {
            *ppNetworkConnection = &connection->INetworkConnection_iface;
            INetworkConnection_AddRef( *ppNetworkConnection );
            LeaveCriticalSection( &mgr->cs );
            return S_OK;
        }
    }
    LeaveCriticalSection( &mgr->cs );

    return S_FALSE;
}

static HRESULT WINAPI list_manager_IsConnectedToInternet(
    INetworkListManager *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct network *network;

    TRACE( "%p, %p\n", iface, pbIsConnected );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (mgr->reachability != REACHABILITY_OFFLINE &&
            (network->connected_to_internet_v4 || network->connected_to_internet_v6))
        {
            *pbIsConnected = VARIANT_TRUE;
            LeaveCriticalSection( &mgr->cs );
            return S_OK;
        }
    }

    *pbIsConnected = VARIANT_FALSE;
    LeaveCriticalSection( &mgr->cs );
    return S_OK;
}

static HRESULT WINAPI list_manager_IsConnected(
    INetworkListManager *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );
    struct network *network;

    TRACE( "%p, %p\n", iface, pbIsConnected );

    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (network->connected_v4 || network->connected_v6)
        {
            *pbIsConnected = VARIANT_TRUE;
            LeaveCriticalSection( &mgr->cs );
            return S_OK;
        }
    }

    *pbIsConnected = VARIANT_FALSE;
    LeaveCriticalSection( &mgr->cs );
    return S_OK;
}

static HRESULT WINAPI list_manager_GetConnectivity(
    INetworkListManager *iface,
    NLM_CONNECTIVITY *pConnectivity )
{
    struct list_manager *mgr = impl_from_INetworkListManager( iface );

    TRACE( "%p, %p\n", iface, pConnectivity );

    EnterCriticalSection( &mgr->cs );
    *pConnectivity = get_connectivity( mgr );
    LeaveCriticalSection( &mgr->cs );

    TRACE( "<- %#x\n", *pConnectivity );
    return S_OK;
}

static const INetworkListManagerVtbl list_manager_vtbl =
{
    list_manager_QueryInterface,
    list_manager_AddRef,
    list_manager_Release,
    list_manager_GetTypeInfoCount,
    list_manager_GetTypeInfo,
    list_manager_GetIDsOfNames,
    list_manager_Invoke,
    list_manager_GetNetworks,
    list_manager_GetNetwork,
    list_manager_GetNetworkConnections,
    list_manager_GetNetworkConnection,
    list_manager_IsConnectedToInternet,
    list_manager_IsConnected,
    list_manager_GetConnectivity
};

static HRESULT WINAPI ConnectionPointContainer_QueryInterface(IConnectionPointContainer *iface,
                                                              REFIID riid, void **ppv)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    return INetworkListManager_QueryInterface(&This->INetworkListManager_iface, riid, ppv);
}

static ULONG WINAPI ConnectionPointContainer_AddRef(IConnectionPointContainer *iface)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    return INetworkListManager_AddRef(&This->INetworkListManager_iface);
}

static ULONG WINAPI ConnectionPointContainer_Release(IConnectionPointContainer *iface)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    return INetworkListManager_Release(&This->INetworkListManager_iface);
}

static HRESULT WINAPI ConnectionPointContainer_EnumConnectionPoints(IConnectionPointContainer *iface,
        IEnumConnectionPoints **ppEnum)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    FIXME("(%p)->(%p): stub\n", This, ppEnum);
    return E_NOTIMPL;
}

static HRESULT WINAPI ConnectionPointContainer_FindConnectionPoint(IConnectionPointContainer *iface,
        REFIID riid, IConnectionPoint **cp)
{
    struct list_manager *This = impl_from_IConnectionPointContainer( iface );
    struct connection_point *ret;

    TRACE( "%p, %s, %p\n", This, debugstr_guid(riid), cp );

    if (!riid || !cp)
        return E_POINTER;

    if (IsEqualGUID( riid, &IID_INetworkListManagerEvents ))
        ret = &This->list_mgr_cp;
    else if (IsEqualGUID( riid, &IID_INetworkCostManagerEvents ))
        ret = &This->cost_mgr_cp;
    else if (IsEqualGUID( riid, &IID_INetworkConnectionEvents ))
        ret = &This->conn_mgr_cp;
    else if (IsEqualGUID( riid, &IID_INetworkEvents))
        ret = &This->events_cp;
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        *cp = NULL;
        return E_NOINTERFACE;
    }

    IConnectionPoint_AddRef( *cp = &ret->IConnectionPoint_iface );
    return S_OK;
}

static const struct IConnectionPointContainerVtbl cpc_vtbl =
{
    ConnectionPointContainer_QueryInterface,
    ConnectionPointContainer_AddRef,
    ConnectionPointContainer_Release,
    ConnectionPointContainer_EnumConnectionPoints,
    ConnectionPointContainer_FindConnectionPoint
};

static inline struct connection *impl_from_INetworkConnection(
    INetworkConnection *iface )
{
    return CONTAINING_RECORD( iface, struct connection, INetworkConnection_iface );
}

static HRESULT WINAPI connection_QueryInterface(
    INetworkConnection *iface, REFIID riid, void **obj )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %s, %p\n", connection, debugstr_guid(riid), obj );

    if (IsEqualIID( riid, &IID_INetworkConnection ) ||
        IsEqualIID( riid, &IID_IDispatch ) ||
        IsEqualIID( riid, &IID_IUnknown ))
    {
        *obj = iface;
    }
    else if (IsEqualIID( riid, &IID_INetworkConnectionCost ))
    {
        *obj = &connection->INetworkConnectionCost_iface;
    }
    else
    {
        WARN( "interface not supported %s\n", debugstr_guid(riid) );
        *obj = NULL;
        return E_NOINTERFACE;
    }
    INetworkConnection_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI connection_AddRef(
    INetworkConnection  *iface )
{
    struct connection *connection = impl_from_INetworkConnection( iface );
    ULONG refs;

    TRACE( "%p\n", connection );
    refs = InterlockedIncrement( &connection->refs );
    if (refs == 2) INetworkListManager_AddRef( connection->mgr );
    return refs;
}

static ULONG WINAPI connection_Release(
    INetworkConnection  *iface )
{
    struct connection *connection = impl_from_INetworkConnection( iface );
    LONG refs;

    TRACE( "%p\n", connection );

    refs = InterlockedDecrement( &connection->refs );
    if (refs == 1 && !INetworkListManager_Release( connection->mgr )) refs = 0;
    else if (!refs)
    {
        list_remove( &connection->entry );
        free( connection );
    }
    return refs;
}

static HRESULT WINAPI connection_GetTypeInfoCount(
    INetworkConnection *iface,
    UINT *count )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connection_GetTypeInfo(
    INetworkConnection *iface,
    UINT index,
    LCID lcid,
    ITypeInfo **info )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connection_GetIDsOfNames(
    INetworkConnection *iface,
    REFIID riid,
    LPOLESTR *names,
    UINT count,
    LCID lcid,
    DISPID *dispid )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connection_Invoke(
    INetworkConnection *iface,
    DISPID member,
    REFIID riid,
    LCID lcid,
    WORD flags,
    DISPPARAMS *params,
    VARIANT *result,
    EXCEPINFO *excep_info,
    UINT *arg_err )
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI connection_GetNetwork(
    INetworkConnection *iface,
    INetwork **ppNetwork )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %p\n", iface, ppNetwork );

    *ppNetwork = connection->network;
    INetwork_AddRef( *ppNetwork );
    return S_OK;
}

static HRESULT WINAPI connection_get_IsConnectedToInternet(
    INetworkConnection *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct connection *connection = impl_from_INetworkConnection( iface );
    struct list_manager *mgr = impl_from_INetworkListManager( connection->mgr );

    TRACE( "%p, %p\n", iface, pbIsConnected );

    EnterCriticalSection( &mgr->cs );
    *pbIsConnected = mgr->reachability == REACHABILITY_OFFLINE ? VARIANT_FALSE :
            connection->connected_to_internet_v4 | connection->connected_to_internet_v6;
    LeaveCriticalSection( &mgr->cs );
    TRACE( "<- %#x\n", *pbIsConnected );
    return S_OK;
}

static HRESULT WINAPI connection_get_IsConnected(
    INetworkConnection *iface,
    VARIANT_BOOL *pbIsConnected )
{
    struct connection *connection = impl_from_INetworkConnection( iface );
    struct list_manager *mgr = impl_from_INetworkListManager( connection->mgr );

    TRACE( "%p, %p\n", iface, pbIsConnected );

    EnterCriticalSection( &mgr->cs );
    *pbIsConnected = connection->connected_v4 | connection->connected_v6;
    LeaveCriticalSection( &mgr->cs );
    TRACE( "<- %#x\n", *pbIsConnected );
    return S_OK;
}

static HRESULT WINAPI connection_GetConnectivity(
    INetworkConnection *iface,
    NLM_CONNECTIVITY *pConnectivity )
{
    struct connection *connection = impl_from_INetworkConnection( iface );
    struct list_manager *mgr = impl_from_INetworkListManager( connection->mgr );

    TRACE( "%p, %p\n", iface, pConnectivity );

    EnterCriticalSection( &mgr->cs );
    *pConnectivity = NLM_CONNECTIVITY_DISCONNECTED;

    if (connection->connected_to_internet_v4 && mgr->reachability != REACHABILITY_OFFLINE)
        *pConnectivity |= NLM_CONNECTIVITY_IPV4_INTERNET;
    else if (connection->connected_v4)
        *pConnectivity |= NLM_CONNECTIVITY_IPV4_LOCALNETWORK;

    if (connection->connected_to_internet_v6 && mgr->reachability != REACHABILITY_OFFLINE)
        *pConnectivity |= NLM_CONNECTIVITY_IPV6_INTERNET;
    else if (connection->connected_v6)
        *pConnectivity |= NLM_CONNECTIVITY_IPV6_LOCALNETWORK;
    LeaveCriticalSection( &mgr->cs );

    TRACE( "<- %#x\n", *pConnectivity );
    return S_OK;
}

static HRESULT WINAPI connection_GetConnectionId(
    INetworkConnection *iface,
    GUID *pgdConnectionId )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    TRACE( "%p, %p\n", iface, pgdConnectionId );

    *pgdConnectionId = connection->id;
    return S_OK;
}

static HRESULT WINAPI connection_GetAdapterId(
    INetworkConnection *iface,
    GUID *pgdAdapterId )
{
    struct connection *connection = impl_from_INetworkConnection( iface );

    FIXME( "%p, %p\n", iface, pgdAdapterId );

    *pgdAdapterId = connection->id;
    return S_OK;
}

static HRESULT WINAPI connection_GetDomainType(
    INetworkConnection *iface,
    NLM_DOMAIN_TYPE *pDomainType )
{
    FIXME( "%p, %p\n", iface, pDomainType );

    *pDomainType = NLM_DOMAIN_TYPE_NON_DOMAIN_NETWORK;
    return S_OK;
}

static const struct INetworkConnectionVtbl connection_vtbl =
{
    connection_QueryInterface,
    connection_AddRef,
    connection_Release,
    connection_GetTypeInfoCount,
    connection_GetTypeInfo,
    connection_GetIDsOfNames,
    connection_Invoke,
    connection_GetNetwork,
    connection_get_IsConnectedToInternet,
    connection_get_IsConnected,
    connection_GetConnectivity,
    connection_GetConnectionId,
    connection_GetAdapterId,
    connection_GetDomainType
};

static inline struct connection *impl_from_INetworkConnectionCost(
    INetworkConnectionCost *iface )
{
    return CONTAINING_RECORD( iface, struct connection, INetworkConnectionCost_iface );
}

static HRESULT WINAPI connection_cost_QueryInterface(
    INetworkConnectionCost *iface,
    REFIID riid,
    void **obj )
{
    struct connection *conn = impl_from_INetworkConnectionCost( iface );
    return INetworkConnection_QueryInterface( &conn->INetworkConnection_iface, riid, obj );
}

static ULONG WINAPI connection_cost_AddRef(
    INetworkConnectionCost *iface )
{
    struct connection *conn = impl_from_INetworkConnectionCost( iface );
    return INetworkConnection_AddRef( &conn->INetworkConnection_iface );
}

static ULONG WINAPI connection_cost_Release(
    INetworkConnectionCost *iface )
{
    struct connection *conn = impl_from_INetworkConnectionCost( iface );
    return INetworkConnection_Release( &conn->INetworkConnection_iface );
}

static HRESULT WINAPI connection_cost_GetCost(
    INetworkConnectionCost *iface, DWORD *pCost )
{
    FIXME( "%p, %p\n", iface, pCost );

    if (!pCost) return E_POINTER;

    *pCost = NLM_CONNECTION_COST_UNRESTRICTED;
    return S_OK;
}

static HRESULT WINAPI connection_cost_GetDataPlanStatus(
    INetworkConnectionCost *iface, NLM_DATAPLAN_STATUS *pDataPlanStatus )
{
    struct connection *conn = impl_from_INetworkConnectionCost( iface );

    FIXME( "%p, %p\n", iface, pDataPlanStatus );

    if (!pDataPlanStatus) return E_POINTER;

    memcpy( &pDataPlanStatus->InterfaceGuid, &conn->id, sizeof(conn->id) );
    pDataPlanStatus->UsageData.UsageInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
    memset( &pDataPlanStatus->UsageData.LastSyncTime, 0, sizeof(pDataPlanStatus->UsageData.LastSyncTime) );
    pDataPlanStatus->DataLimitInMegabytes       = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->InboundBandwidthInKbps     = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->OutboundBandwidthInKbps    = NLM_UNKNOWN_DATAPLAN_STATUS;
    memset( &pDataPlanStatus->NextBillingCycle, 0, sizeof(pDataPlanStatus->NextBillingCycle) );
    pDataPlanStatus->MaxTransferSizeInMegabytes = NLM_UNKNOWN_DATAPLAN_STATUS;
    pDataPlanStatus->Reserved                   = 0;

    return S_OK;
}

static const INetworkConnectionCostVtbl connection_cost_vtbl =
{
    connection_cost_QueryInterface,
    connection_cost_AddRef,
    connection_cost_Release,
    connection_cost_GetCost,
    connection_cost_GetDataPlanStatus
};

static struct connection *create_connection( const GUID *id )
{
    struct connection *ret;

    if (!(ret = calloc( 1, sizeof(*ret) ))) return NULL;

    ret->INetworkConnection_iface.lpVtbl     = &connection_vtbl;
    ret->INetworkConnectionCost_iface.lpVtbl = &connection_cost_vtbl;
    ret->refs = 1;
    ret->id   = *id;
    list_init( &ret->entry );

    return ret;
}

static IP_ADAPTER_ADDRESSES *get_network_adapters(void)
{
    ULONG err, size = 4096, flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                    GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    IP_ADAPTER_ADDRESSES *tmp, *ret;

    if (!(ret = malloc( size ))) return NULL;
    err = GetAdaptersAddresses( AF_UNSPEC, flags, NULL, ret, &size );
    while (err == ERROR_BUFFER_OVERFLOW)
    {
        if (!(tmp = realloc( ret, size ))) break;
        ret = tmp;
        err = GetAdaptersAddresses( AF_UNSPEC, flags, NULL, ret, &size );
    }
    if (err == ERROR_SUCCESS) return ret;
    free( ret );
    return NULL;
}

static void has_ipv6_address( const IP_ADAPTER_ADDRESSES *aa, BOOL *has_local, BOOL *has_global )
{
    const IP_ADAPTER_UNICAST_ADDRESS *addr = aa->FirstUnicastAddress;
    const struct in6_addr *sa6;

    *has_local = *has_global = FALSE;
    for (addr = aa->FirstUnicastAddress; addr; addr = addr->Next)
    {
        if (addr->Address.lpSockaddr->sa_family != AF_INET6) continue;
        sa6 = &((struct sockaddr_in6 *)addr->Address.lpSockaddr)->sin6_addr;
        if (IN6_IS_ADDR_LINKLOCAL(sa6) || IN6_IS_ADDR_SITELOCAL(sa6))
            *has_local = TRUE;
        else if (!IN6_IS_ADDR_UNSPECIFIED(sa6) && !IN6_IS_ADDR_MULTICAST(sa6) && !IN6_IS_ADDR_LOOPBACK(sa6))
            *has_global = TRUE;
    }
}

static BOOL has_ipv4_address( const IP_ADAPTER_ADDRESSES *aa )
{
    const IP_ADAPTER_UNICAST_ADDRESS *addr = aa->FirstUnicastAddress;
    while (addr)
    {
        if (addr->Address.lpSockaddr->sa_family == AF_INET)
            return TRUE;
        addr = addr->Next;
    }
    return FALSE;
}

static BOOL has_ipv4_gateway_address( const IP_ADAPTER_ADDRESSES *aa )
{
    const IP_ADAPTER_GATEWAY_ADDRESS *addr = aa->FirstGatewayAddress;
    while (addr)
    {
        if (addr->Address.lpSockaddr->sa_family == AF_INET)
            return TRUE;
        addr = addr->Next;
    }
    return FALSE;
}

static BOOL has_ipv6_default_route( const IP_ADAPTER_ADDRESSES *aa )
{
    struct nsi_ipv6_forward_key *keys;
    DWORD count, err, i;
    BOOL ret = FALSE;

    err = NsiAllocateAndGetTable( 1, &npi_ipv6_module, NSI_IP_FORWARD_TABLE,
                                  (void **)&keys, sizeof(*keys), NULL, 0, NULL, 0, NULL, 0, &count, 0 );
    if (err) return FALSE;
    for (i = 0; i < count; i++)
    {
        static const IN6_ADDR zero;

        if (keys[i].luid.Value == aa->Luid.Value && !keys[i].prefix_len &&
            memcmp( &keys[i].next_hop, &zero, sizeof(zero) ))
        {
            ret = TRUE;
            break;
        }
    }
    NsiFreeTable( keys, NULL, NULL, NULL );
    return ret;
}

static NLM_CONNECTIVITY get_connectivity( struct list_manager *mgr )
{
    NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED;
    struct network *network;

    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        if (network->connected_to_internet_v4 && mgr->reachability != REACHABILITY_OFFLINE)
            connectivity |= NLM_CONNECTIVITY_IPV4_INTERNET;
        else if (network->connected_v4)
            connectivity |= NLM_CONNECTIVITY_IPV4_LOCALNETWORK;

        if (network->connected_to_internet_v6 && mgr->reachability != REACHABILITY_OFFLINE)
            connectivity |= NLM_CONNECTIVITY_IPV6_INTERNET;
        else if (network->connected_v6)
            connectivity |= NLM_CONNECTIVITY_IPV6_LOCALNETWORK;
    }
    return connectivity;
}

static IP_ADAPTER_ADDRESSES *find_adapter( IP_ADAPTER_ADDRESSES *buf, const GUID *id )
{
    IP_ADAPTER_ADDRESSES *aa;

    for (aa = buf; aa; aa = aa->Next)
    {
        NET_LUID luid;
        GUID adapter_id;

        if (!wcscmp( aa->FriendlyName, L"lo" )) continue;
        if (ConvertInterfaceIndexToLuid( aa->IfIndex, &luid )) continue;
        if (ConvertInterfaceLuidToGuid( &luid, &adapter_id )) continue;
        if (IsEqualGUID( id, &adapter_id )) return aa;
    }
    return NULL;
}

static NLM_CONNECTIVITY refresh_networks( struct list_manager *mgr )
{
    IP_ADAPTER_ADDRESSES *buf, *aa;
    struct network *network;
    struct connection *connection;
    NET_LUID luid;
    GUID id;
    BOOL has_local, has_global, connected_v4, connected_v6, internet_v4, internet_v6;
    NLM_CONNECTIVITY old_connectivity, connectivity;

    if (!(buf = get_network_adapters())) return (NLM_CONNECTIVITY)-1;

    EnterCriticalSection( &mgr->cs );
    old_connectivity = get_connectivity( mgr );
    LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
    {
        connected_v4 = connected_v6 = internet_v4 = internet_v6 = FALSE;
        if ((aa = find_adapter( buf, &network->id )) && aa->OperStatus == IfOperStatusUp)
        {
            has_ipv6_address( aa, &has_local, &has_global );
            connected_v6 = has_local || has_global;
            internet_v6 = has_global && has_ipv6_default_route( aa );
            connected_v4 = has_ipv4_address( aa );
            internet_v4 = has_ipv4_gateway_address( aa );
        }

        network->connected_v4 = connected_v4 ? VARIANT_TRUE : VARIANT_FALSE;
        network->connected_v6 = connected_v6 ? VARIANT_TRUE : VARIANT_FALSE;
        network->connected_to_internet_v4 = internet_v4 ? VARIANT_TRUE : VARIANT_FALSE;
        network->connected_to_internet_v6 = internet_v6 ? VARIANT_TRUE : VARIANT_FALSE;

        LIST_FOR_EACH_ENTRY( connection, &mgr->connections, struct connection, entry )
        {
            if (!IsEqualGUID( &connection->id, &network->id )) continue;
            connection->connected_v4 = network->connected_v4;
            connection->connected_v6 = network->connected_v6;
            connection->connected_to_internet_v4 = network->connected_to_internet_v4;
            connection->connected_to_internet_v6 = network->connected_to_internet_v6;
            break;
        }
    }

    for (aa = buf; aa; aa = aa->Next)
    {
        if (!wcscmp( aa->FriendlyName, L"lo" )) continue;
        if (ConvertInterfaceIndexToLuid( aa->IfIndex, &luid )) continue;
        if (ConvertInterfaceLuidToGuid( &luid, &id )) continue;

        LIST_FOR_EACH_ENTRY( network, &mgr->networks, struct network, entry )
            if (IsEqualGUID( &network->id, &id )) break;
        if (&network->entry != &mgr->networks) continue;

        if (!(network = create_network( &id ))) continue;
        if (!(connection = create_connection( &id )))
        {
            INetwork_Release( &network->INetwork_iface );
            continue;
        }

        network->mgr = &mgr->INetworkListManager_iface;
        connection->network = &network->INetwork_iface;
        connection->mgr = &mgr->INetworkListManager_iface;
        if (aa->OperStatus == IfOperStatusUp)
        {
            has_ipv6_address( aa, &has_local, &has_global );
            network->connected_v6 = connection->connected_v6 =
                    has_local || has_global ? VARIANT_TRUE : VARIANT_FALSE;
            network->connected_to_internet_v6 = connection->connected_to_internet_v6 =
                    has_global && has_ipv6_default_route( aa ) ? VARIANT_TRUE : VARIANT_FALSE;
            network->connected_v4 = connection->connected_v4 =
                    has_ipv4_address( aa ) ? VARIANT_TRUE : VARIANT_FALSE;
            network->connected_to_internet_v4 = connection->connected_to_internet_v4 =
                    has_ipv4_gateway_address( aa ) ? VARIANT_TRUE : VARIANT_FALSE;
        }
        list_add_tail( &mgr->networks, &network->entry );
        list_add_tail( &mgr->connections, &connection->entry );
    }
    connectivity = get_connectivity( mgr );
    LeaveCriticalSection( &mgr->cs );
    free( buf );

    return connectivity == old_connectivity ? (NLM_CONNECTIVITY)-1 : connectivity;
}

static void notify_connectivity_changed( struct list_manager *mgr, NLM_CONNECTIVITY connectivity )
{
    IGlobalInterfaceTable *git;
    DWORD *cookies;
    struct sink_entry *entry;
    unsigned int count = 0, i = 0;
    HRESULT hr;

    INetworkListManager_AddRef( &mgr->INetworkListManager_iface );
    EnterCriticalSection( &mgr->cs );
    LIST_FOR_EACH_ENTRY( entry, &mgr->list_mgr_cp.sinks, struct sink_entry, entry ) count++;
    if (!(cookies = calloc( count, sizeof(*cookies) ))) count = 0;
    else LIST_FOR_EACH_ENTRY( entry, &mgr->list_mgr_cp.sinks, struct sink_entry, entry )
        cookies[i++] = entry->git_cookie;
    LeaveCriticalSection( &mgr->cs );

    hr = CoCreateInstance( &CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IGlobalInterfaceTable, (void **)&git );
    if (SUCCEEDED(hr))
    {
        for (i = 0; i < count && !InterlockedCompareExchange( &mgr->destroy_pending, 0, 0 ); i++)
        {
            INetworkListManagerEvents *sink;

            hr = IGlobalInterfaceTable_GetInterfaceFromGlobal( git, cookies[i],
                    &IID_INetworkListManagerEvents, (void **)&sink );
            if (FAILED(hr)) continue;
            INetworkListManagerEvents_ConnectivityChanged( sink, connectivity );
            INetworkListManagerEvents_Release( sink );
        }
        IGlobalInterfaceTable_Release( git );
    }
    free( cookies );
    INetworkListManager_Release( &mgr->INetworkListManager_iface );
}

static void update_reachability( struct list_manager *mgr, enum reachability_state state )
{
    NLM_CONNECTIVITY old_connectivity, connectivity;

    EnterCriticalSection( &mgr->notify_cs );
    EnterCriticalSection( &mgr->cs );
    old_connectivity = get_connectivity( mgr );
    mgr->reachability = state;
    connectivity = get_connectivity( mgr );
    LeaveCriticalSection( &mgr->cs );

    if (connectivity != old_connectivity) notify_connectivity_changed( mgr, connectivity );
    LeaveCriticalSection( &mgr->notify_cs );
}

static DWORD WINAPI reachability_monitor_proc( void *param )
{
    struct list_manager *mgr = param;
    struct reachability_wait_params params = {mgr->reachability_handle};
    NTSTATUS status;

    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    while (WaitForSingleObject( mgr->reachability_stop_event, 0 ) != WAIT_OBJECT_0)
    {
        status = UNIX_CALL( reachability_wait, &params );
        if (params.changed) update_reachability( mgr, params.state );
        if (status) break;
    }
    if (InterlockedCompareExchange( &mgr->destroy_pending, 0, 0 ) && mgr->destroy_tid == GetCurrentThreadId())
        list_manager_destroy( mgr );
    CoUninitialize();
    return 0;
}

static void start_reachability_monitor( struct list_manager *mgr )
{
    struct reachability_start_params params = {0};

    mgr->reachability = REACHABILITY_INDETERMINATE;
    if (UNIX_CALL( reachability_start, &params )) return;
    mgr->reachability = params.state;
    mgr->reachability_handle = params.handle;

    if (!(mgr->reachability_stop_event = CreateEventW( NULL, TRUE, FALSE, NULL )) ||
        !(mgr->reachability_worker = CreateThread( NULL, 0, reachability_monitor_proc, mgr, 0,
                                                   &mgr->reachability_worker_tid )))
    {
        struct reachability_stop_params stop_params = {mgr->reachability_handle};
        if (mgr->reachability_stop_event) CloseHandle( mgr->reachability_stop_event );
        mgr->reachability_stop_event = NULL;
        UNIX_CALL( reachability_stop, &stop_params );
        mgr->reachability_handle = 0;
        mgr->reachability = REACHABILITY_INDETERMINATE;
    }
}

static void stop_reachability_monitor( struct list_manager *mgr )
{
    struct reachability_stop_params params;

    if (!mgr->reachability_handle) return;
    SetEvent( mgr->reachability_stop_event );
    if (mgr->reachability_worker_tid != GetCurrentThreadId())
        WaitForSingleObject( mgr->reachability_worker, INFINITE );
    CloseHandle( mgr->reachability_worker );
    CloseHandle( mgr->reachability_stop_event );
    params.handle = mgr->reachability_handle;
    UNIX_CALL( reachability_stop, &params );
    mgr->reachability_worker = mgr->reachability_stop_event = NULL;
    mgr->reachability_handle = 0;
}

struct monitor_subscription
{
    const NPI_MODULEID *module;
    UINT table;
    OVERLAPPED overlapped;
    HANDLE handle;
    BOOL pending;
};

static void arm_subscription( struct monitor_subscription *subscription )
{
    DWORD err;

    ResetEvent( subscription->overlapped.hEvent );
    err = NsiRequestChangeNotification( 0, subscription->module, subscription->table,
                                        &subscription->overlapped, &subscription->handle );
    subscription->pending = err == ERROR_IO_PENDING;
    if (err && err != ERROR_IO_PENDING)
        WARN( "failed to subscribe to NSI table %u, error %lu\n", subscription->table, err );
}

static DWORD WINAPI monitor_proc( void *param )
{
    struct list_manager *mgr = param;
    struct monitor_subscription subscriptions[] =
    {
        { &npi_ndis_module, NSI_NDIS_IFINFO_TABLE },
        { &npi_ipv4_module, NSI_IP_UNICAST_TABLE },
        { &npi_ipv6_module, NSI_IP_UNICAST_TABLE },
        { &npi_ipv4_module, NSI_IP_FORWARD_TABLE },
        { &npi_ipv6_module, NSI_IP_FORWARD_TABLE },
    };
    HANDLE events[ARRAY_SIZE(subscriptions) + 1];
    NLM_CONNECTIVITY connectivity;
    DWORD bytes, ret;
    unsigned int i;

    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    events[0] = mgr->stop_event;
    for (i = 0; i < ARRAY_SIZE(subscriptions); i++)
    {
        subscriptions[i].overlapped.hEvent = events[i + 1] = CreateEventW( NULL, TRUE, FALSE, NULL );
        arm_subscription( &subscriptions[i] );
    }
    refresh_networks( mgr );
    SetEvent( mgr->monitor_ready_event );

    for (;;)
    {
        ret = WaitForMultipleObjects( ARRAY_SIZE(events), events, FALSE, INFINITE );
        if (ret == WAIT_OBJECT_0) break;
        if (ret < WAIT_OBJECT_0 + 1 || ret >= WAIT_OBJECT_0 + ARRAY_SIZE(events)) break;

        if (WaitForSingleObject( mgr->stop_event, 150 ) == WAIT_OBJECT_0) break;
        for (i = 0; i < ARRAY_SIZE(subscriptions); i++)
        {
            if (WaitForSingleObject( subscriptions[i].overlapped.hEvent, 0 ) != WAIT_OBJECT_0) continue;
            GetOverlappedResult( subscriptions[i].handle, &subscriptions[i].overlapped, &bytes, FALSE );
            subscriptions[i].pending = FALSE;
            arm_subscription( &subscriptions[i] );
        }

        EnterCriticalSection( &mgr->notify_cs );
        connectivity = refresh_networks( mgr );
        if (connectivity != (NLM_CONNECTIVITY)-1) notify_connectivity_changed( mgr, connectivity );
        LeaveCriticalSection( &mgr->notify_cs );
        if (InterlockedCompareExchange( &mgr->destroy_pending, 0, 0 )) break;
    }

    for (i = 0; i < ARRAY_SIZE(subscriptions); i++)
    {
        if (subscriptions[i].pending)
        {
            NsiCancelChangeNotification( &subscriptions[i].overlapped );
            GetOverlappedResult( subscriptions[i].handle, &subscriptions[i].overlapped, &bytes, TRUE );
        }
        CloseHandle( subscriptions[i].overlapped.hEvent );
    }
    if (InterlockedCompareExchange( &mgr->destroy_pending, 0, 0 ) && mgr->destroy_tid == GetCurrentThreadId())
        list_manager_destroy( mgr );
    CoUninitialize();
    return 0;
}

static HRESULT start_monitor( struct list_manager *mgr )
{
    if (!(mgr->stop_event = CreateEventW( NULL, TRUE, FALSE, NULL )) ||
        !(mgr->monitor_ready_event = CreateEventW( NULL, TRUE, FALSE, NULL )))
    {
        DWORD err = GetLastError();
        if (mgr->stop_event) CloseHandle( mgr->stop_event );
        mgr->stop_event = NULL;
        return HRESULT_FROM_WIN32( err );
    }
    if (!(mgr->worker = CreateThread( NULL, 0, monitor_proc, mgr, 0, &mgr->worker_tid )))
    {
        DWORD err = GetLastError();
        CloseHandle( mgr->stop_event );
        CloseHandle( mgr->monitor_ready_event );
        mgr->stop_event = mgr->monitor_ready_event = NULL;
        return HRESULT_FROM_WIN32( err );
    }
    WaitForSingleObject( mgr->monitor_ready_event, INFINITE );
    CloseHandle( mgr->monitor_ready_event );
    mgr->monitor_ready_event = NULL;
    return S_OK;
}

static void stop_monitor( struct list_manager *mgr )
{
    if (!mgr->worker) return;
    SetEvent( mgr->stop_event );
    if (mgr->worker_tid != GetCurrentThreadId()) WaitForSingleObject( mgr->worker, INFINITE );
    CloseHandle( mgr->worker );
    CloseHandle( mgr->stop_event );
    mgr->worker = mgr->stop_event = NULL;
}

static void init_networks( struct list_manager *mgr )
{
    BOOL has_local, has_global;
    IP_ADAPTER_ADDRESSES *buf, *aa;
    GUID id;

    list_init( &mgr->networks );
    list_init( &mgr->connections );

    if (!(buf = get_network_adapters())) return;

    memset( &id, 0, sizeof(id) );
    for (aa = buf; aa; aa = aa->Next)
    {
        struct network *network;
        struct connection *connection;
        NET_LUID luid;

        if (!wcscmp( aa->FriendlyName, L"lo" )) continue;

        ConvertInterfaceIndexToLuid(aa->IfIndex, &luid);
        ConvertInterfaceLuidToGuid(&luid, &id);

        /* assume a one-to-one mapping between networks and connections */
        if (!(network = create_network( &id ))) goto done;
        if (!(connection = create_connection( &id )))
        {
            INetwork_Release( &network->INetwork_iface );
            goto done;
        }

        if (aa->OperStatus != IfOperStatusUp) has_local = has_global = FALSE;
        else has_ipv6_address( aa, &has_local, &has_global );
        if (has_local || has_global)
        {
            network->connected_v6 = VARIANT_TRUE;
            connection->connected_v6 = VARIANT_TRUE;
        }
        if (has_global && has_ipv6_default_route( aa ))
        {
            network->connected_to_internet_v6 = VARIANT_TRUE;
            connection->connected_to_internet_v6 = VARIANT_TRUE;
        }
        if (aa->OperStatus == IfOperStatusUp && has_ipv4_address( aa ))
        {
            network->connected_v4 = VARIANT_TRUE;
            connection->connected_v4 = VARIANT_TRUE;
        }
        if (aa->OperStatus == IfOperStatusUp && has_ipv4_gateway_address( aa ))
        {
            network->connected_to_internet_v4 = VARIANT_TRUE;
            connection->connected_to_internet_v4 = VARIANT_TRUE;
        }

        network->mgr = &mgr->INetworkListManager_iface;
        connection->network = &network->INetwork_iface;
        connection->mgr = &mgr->INetworkListManager_iface;

        list_add_tail( &mgr->networks, &network->entry );
        list_add_tail( &mgr->connections, &connection->entry );
    }

done:
    free( buf );
}

HRESULT list_manager_create( void **obj )
{
    struct list_manager *mgr;
    HRESULT hr;

    TRACE( "%p\n", obj );

    if (!(mgr = calloc( 1, sizeof(*mgr) ))) return E_OUTOFMEMORY;
    mgr->INetworkListManager_iface.lpVtbl = &list_manager_vtbl;
    mgr->INetworkCostManager_iface.lpVtbl = &cost_manager_vtbl;
    mgr->IConnectionPointContainer_iface.lpVtbl = &cpc_vtbl;
    mgr->refs = 1;
    InitializeCriticalSection( &mgr->cs );
    InitializeCriticalSection( &mgr->notify_cs );
    init_networks( mgr );

    connection_point_init( &mgr->list_mgr_cp, &IID_INetworkListManagerEvents,
                           &mgr->IConnectionPointContainer_iface );
    connection_point_init( &mgr->cost_mgr_cp, &IID_INetworkCostManagerEvents,
                           &mgr->IConnectionPointContainer_iface);
    connection_point_init( &mgr->conn_mgr_cp, &IID_INetworkConnectionEvents,
                           &mgr->IConnectionPointContainer_iface );
    connection_point_init( &mgr->events_cp, &IID_INetworkEvents,
                           &mgr->IConnectionPointContainer_iface );

    start_reachability_monitor( mgr );
    if (FAILED(hr = start_monitor( mgr )))
        WARN( "failed to start network monitor, hr %#lx\n", hr );

    *obj = &mgr->INetworkListManager_iface;
    TRACE( "returning iface %p\n", *obj );
    return S_OK;
}
