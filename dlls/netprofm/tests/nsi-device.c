/* Deterministic cancellation/startup interleavings in the real device queue.
 * Complements nsi-faults.c, which uses the real Unix backend and device host.
 * Copyright 2026 Peter Min
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#define STANDALONE
#include <stdarg.h>
#include <stdlib.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wincon.h"
#include "winternl.h"
#include "winioctl.h"
#include "ddk/wdm.h"
#include "ifdef.h"
#include "netiodef.h"
#include "wine/nsi.h"
#include "wine/unixlib.h"
#include "wine/test.h"

static HANDLE poll_gate, cancel_entered, cancel_gate;
static BOOL hold_cancel;
static NTSTATUS poll_status;
static LONG completions, allocations;

static NTSTATUS test_call( unsigned int code, void *args );

static void *test_calloc( size_t count, size_t size )
{
    void *ptr = calloc( count, size );
    if (ptr) InterlockedIncrement( &allocations );
    return ptr;
}

static void test_free( void *ptr )
{
    if (ptr) InterlockedDecrement( &allocations );
    free( ptr );
}

static void WINAPI test_complete( IRP *irp, UCHAR priority )
{
    LONG *count = irp->Tail.Overlay.DriverContext[1];
    ok( InterlockedIncrement( count ) == 1, "IRP completed twice\n" );
    InterlockedIncrement( &completions );
}

static void WINAPI test_release_cancel( KIRQL irql )
{
    IoReleaseCancelSpinLock( irql );
    if (hold_cancel)
    {
        SetEvent( cancel_entered );
        WaitForSingleObject( cancel_gate, INFINITE );
    }
}

static HANDLE WINAPI test_thread( SECURITY_ATTRIBUTES *sa, SIZE_T stack, LPTHREAD_START_ROUTINE proc,
                                  void *arg, DWORD flags, DWORD *tid )
{
    SetLastError( ERROR_NOT_ENOUGH_MEMORY );
    return NULL;
}

static NTSTATUS WINAPI test_device( DRIVER_OBJECT *driver, ULONG size, UNICODE_STRING *name,
                                    DEVICE_TYPE type, ULONG flags, BOOLEAN exclusive, DEVICE_OBJECT **device )
{
    static DEVICE_OBJECT object;
    *device = &object;
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI test_link( UNICODE_STRING *link, UNICODE_STRING *name ) { return STATUS_SUCCESS; }

#undef WINE_UNIX_CALL
#define WINE_UNIX_CALL(code, args) test_call(code, args)
#define __wine_init_unix_call() STATUS_SUCCESS
#define IoCompleteRequest test_complete
#define IoReleaseCancelSpinLock test_release_cancel
#define IoCreateDevice test_device
#define IoCreateSymbolicLink test_link
#define CreateThread test_thread
#define calloc test_calloc
#define free test_free
#ifndef DEVICE_SOURCE
#define DEVICE_SOURCE "../../nsiproxy.sys/device.c"
#endif
#include DEVICE_SOURCE
#undef CreateThread

static NTSTATUS test_call( unsigned int code, void *args )
{
    struct nsi_get_notification_params *params = args;
    WaitForSingleObject( poll_gate, INFINITE );
    params->module = NPI_MS_NDIS_MODULEID;
    params->table = NSI_NDIS_IFINFO_TABLE;
    return poll_status;
}

struct request
{
    IRP irp;
    IO_STACK_LOCATION stack;
    struct nsiproxy_request_notification input;
    LONG completed;
};

static void init_request( struct request *request )
{
    memset( request, 0, sizeof(*request) );
    request->input.module = NPI_MS_NDIS_MODULEID;
    request->input.table = NSI_NDIS_IFINFO_TABLE;
    request->irp.AssociatedIrp.SystemBuffer = &request->input;
    request->irp.Tail.Overlay.CurrentStackLocation = &request->stack;
    request->irp.Tail.Overlay.DriverContext[1] = &request->completed;
    request->stack.Parameters.DeviceIoControl.InputBufferLength = sizeof(request->input);
    request->stack.Parameters.DeviceIoControl.IoControlCode = IOCTL_NSIPROXY_WINE_CHANGE_NOTIFICATION;
}

static DWORD WINAPI cancel_request( void *arg )
{
    IoCancelIrp( arg );
    return 0;
}

START_TEST(nsi_device)
{
    struct request a, b, future;
    DRIVER_OBJECT driver = {0};
    UNICODE_STRING path = RTL_CONSTANT_STRING( L"test" );
    HANDLE worker, canceller;
    unsigned int i;
    NTSTATUS status;

    poll_gate = CreateEventW( NULL, FALSE, FALSE, NULL );
    cancel_entered = CreateEventW( NULL, FALSE, FALSE, NULL );
    cancel_gate = CreateEventW( NULL, FALSE, FALSE, NULL );
    /* Both possible owners, on successful notification and terminal error. */
    for (i = 0; i < 64; i++)
    {
        notification_status = 0;
        poll_status = i & 1 ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
        completions = 0;
        init_request( &a );
        init_request( &b );
        init_request( &future );
        status = nsi_ioctl( NULL, &a.irp );
        ok( status == STATUS_PENDING, "queue a %#lx\n", status );
        status = nsi_ioctl( NULL, &b.irp );
        ok( status == STATUS_PENDING, "queue b %#lx\n", status );
        worker = CreateThread( NULL, 0, notification_thread_proc, NULL, 0, NULL );
        hold_cancel = TRUE;
        canceller = CreateThread( NULL, 0, cancel_request, &a.irp, 0, NULL );
        ok( WaitForSingleObject( cancel_entered, 10000 ) == WAIT_OBJECT_0, "cancel did not claim IRP\n" );
        SetEvent( poll_gate );
        if (!poll_status)
        {
            /* Wait for b before asking the worker to terminate. */
            unsigned int j;
            for (j = 0; j < 1000 && !InterlockedCompareExchange( &completions, 0, 0 ); j++) Sleep( 1 );
            ok( b.completed == 1, "successful notification did not complete b\n" );
            poll_status = STATUS_UNSUCCESSFUL;
            SetEvent( poll_gate );
        }
        ok( WaitForSingleObject( worker, 10000 ) == WAIT_OBJECT_0, "worker stuck\n" );
        ok( !a.completed && b.completed == 1, "ownership a %ld b %ld\n", a.completed, b.completed );
        ok( b.irp.IoStatus.Status == (i & 1 ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS), "b status %#lx\n", b.irp.IoStatus.Status );
        ok( !IoCancelIrp( &b.irp ), "completed IRP retained cancel routine\n" );
        status = nsi_ioctl( NULL, &future.irp );
        ok( status == STATUS_UNSUCCESSFUL && future.completed == 1, "future request %#lx/%ld\n", status, future.completed );
        SetEvent( cancel_gate );
        ok( WaitForSingleObject( canceller, 10000 ) == WAIT_OBJECT_0, "canceller stuck\n" );
        ok( a.completed == 1 && a.irp.IoStatus.Status == STATUS_CANCELLED, "cancelled IRP %#lx/%ld\n", a.irp.IoStatus.Status, a.completed );
        ok( IsListEmpty( &notification_queue ), "queue not drained\n" );
        ok( !allocations, "leaked %ld notification allocations\n", allocations );
        CloseHandle( worker );
        CloseHandle( canceller );
        hold_cancel = FALSE;
    }
    /* The real DriverEntry takes the failed CreateThread branch. */
    notification_status = 0;
    status = DriverEntry( &driver, &path );
    ok( status == STATUS_SUCCESS, "driver initialization %#lx\n", status );
    init_request( &future );
    status = nsi_ioctl( NULL, &future.irp );
    ok( status == STATUS_NO_MEMORY && future.completed == 1, "thread creation failure %#lx/%ld\n", status, future.completed );
    ok( IsListEmpty( &notification_queue ), "startup failure queued a request\n" );
    ok( !allocations, "startup failure leaked %ld allocations\n", allocations );
    notification_status = 0;
    init_request( &future );
    future.irp.Cancel = TRUE;
    status = nsi_ioctl( NULL, &future.irp );
    ok( status == STATUS_CANCELLED && future.completed == 1, "pre-cancelled request %#lx/%ld\n", status, future.completed );
    ok( IsListEmpty( &notification_queue ) && !allocations, "pre-cancelled request leaked\n" );
    CloseHandle( poll_gate );
    CloseHandle( cancel_entered );
    CloseHandle( cancel_gate );
}
