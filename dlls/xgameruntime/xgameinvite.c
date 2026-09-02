/*
 * Xbox Game Invite activation relay
 *
 * Copyright 2026 Olivia Ryan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(xgameruntime);

#define MAX_INVITE_REGISTRATIONS 4
#define MAX_INVITE_URI 4096

/* Appended after the existing conn_socket/poll_socket/send_frame unix calls.
 * Keep these values in sync with GDKComponent/Xodus/Unix/socket.c. */
enum invite_unix_funcs
{
    invite_connect_socket = 3,
    invite_poll_socket,
    invite_close_socket,
};

typedef void (CALLBACK *XGameInviteEventCallback)( void *context, const char *invite_uri );

typedef struct IXGameInviteImpl IXGameInviteImpl;
typedef struct IXGameInviteImplVtbl
{
    HRESULT (WINAPI *QueryInterface)( IXGameInviteImpl *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( IXGameInviteImpl *iface );
    ULONG (WINAPI *Release)( IXGameInviteImpl *iface );
    HRESULT (WINAPI *RegisterForEvent)( IXGameInviteImpl *iface, XTaskQueueHandle queue,
                                       void *context, XGameInviteEventCallback callback,
                                       XTaskQueueRegistrationToken *token );
    BOOLEAN (WINAPI *UnregisterForEvent)( IXGameInviteImpl *iface,
                                         XTaskQueueRegistrationToken token, BOOLEAN wait );
} IXGameInviteImplVtbl;

struct IXGameInviteImpl
{
    const IXGameInviteImplVtbl *lpVtbl;
};

struct invite_poll_args
{
    UINT32 length;
    char uri[MAX_INVITE_URI + 1];
};

struct invite_registration
{
    XTaskQueueHandle queue;
    void *context;
    XGameInviteEventCallback callback;
    UINT64 token;
};

struct invite_post
{
    void *context;
    XGameInviteEventCallback callback;
    char uri[1];
};

const GUID CLSID_XGameInviteImpl =
    {0x0651aae2, 0x4012, 0x4077, {0xbf, 0x84, 0x8b, 0x90, 0x97, 0x09, 0x0e, 0x2c}};

static struct invite_registration invite_regs[MAX_INVITE_REGISTRATIONS];
static SRWLOCK invite_regs_lock = SRWLOCK_INIT;
static LONG invite_worker_started;
static UINT64 invite_next_token = 1;
static LONG invite_ref = 1;

static void CALLBACK invite_trampoline( void *context, BOOLEAN canceled )
{
    struct invite_post *post = context;

    if (!canceled) post->callback( post->context, post->uri );
    free( post );
}

static void invite_deliver( const struct invite_registration *reg, const char *uri )
{
    IXThreadingImpl *threading;
    struct invite_post *post;
    size_t length = strlen( uri );
    HRESULT hr;

    if (!(post = malloc( offsetof(struct invite_post, uri) + length + 1 ))) return;
    post->context = reg->context;
    post->callback = reg->callback;
    memcpy( post->uri, uri, length + 1 );

    if (!reg->queue)
    {
        invite_trampoline( post, FALSE );
        return;
    }
    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl,
                                  (void **)&threading )))
    {
        WARN( "could not get XThreading for invite delivery, hr %#lx.\n", hr );
        free( post );
        return;
    }
    hr = IXThreadingImpl_XTaskQueueSubmitCallback( threading, reg->queue,
                                                   XTaskQueuePort_Completion, post,
                                                   invite_trampoline );
    IXThreadingImpl_Release( threading );
    if (FAILED(hr))
    {
        WARN( "could not queue invite callback, hr %#lx.\n", hr );
        free( post );
    }
}

static DWORD WINAPI invite_worker( void *unused )
{
    struct invite_poll_args poll;
    NTSTATUS status;
    UINT32 i;

    status = __wine_unix_call( unixhandle, invite_connect_socket, (void *)XODUS_SOCKET_SUFFIX );
    if (status)
    {
        WARN( "could not subscribe to Xodus invite activations, status %#lx.\n", status );
        InterlockedExchange( &invite_worker_started, 0 );
        return 0;
    }

    for (;;)
    {
        memset( &poll, 0, sizeof(poll) );
        status = __wine_unix_call( unixhandle, invite_poll_socket, &poll );
        if (status) break;

        TRACE( "received Xbox invite activation %s.\n", debugstr_a(poll.uri) );
        AcquireSRWLockShared( &invite_regs_lock );
        for (i = 0; i < MAX_INVITE_REGISTRATIONS; ++i)
            if (invite_regs[i].callback) invite_deliver( &invite_regs[i], poll.uri );
        ReleaseSRWLockShared( &invite_regs_lock );
    }

    __wine_unix_call( unixhandle, invite_close_socket, NULL );
    InterlockedExchange( &invite_worker_started, 0 );
    return 0;
}

static HRESULT WINAPI game_invite_QueryInterface( IXGameInviteImpl *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &CLSID_XGameInviteImpl ))
    {
        *out = iface;
        InterlockedIncrement( &invite_ref );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI game_invite_AddRef( IXGameInviteImpl *iface )
{
    return InterlockedIncrement( &invite_ref );
}

static ULONG WINAPI game_invite_Release( IXGameInviteImpl *iface )
{
    return InterlockedDecrement( &invite_ref );
}

static HRESULT WINAPI game_invite_RegisterForEvent( IXGameInviteImpl *iface,
        XTaskQueueHandle queue, void *context, XGameInviteEventCallback callback,
        XTaskQueueRegistrationToken *token )
{
    IXThreadingImpl *threading;
    HANDLE thread;
    UINT32 i;
    HRESULT hr;

    TRACE( "iface %p, queue %p, context %p, callback %p, token %p.\n",
           iface, queue, context, callback, token );
    if (!callback || !token) return E_POINTER;

    AcquireSRWLockExclusive( &invite_regs_lock );
    for (i = 0; i < MAX_INVITE_REGISTRATIONS; ++i) if (!invite_regs[i].callback) break;
    if (i == MAX_INVITE_REGISTRATIONS)
    {
        ReleaseSRWLockExclusive( &invite_regs_lock );
        return E_OUTOFMEMORY;
    }

    invite_regs[i].queue = NULL;
    if (queue)
    {
        if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl,
                                      (void **)&threading )))
        {
            ReleaseSRWLockExclusive( &invite_regs_lock );
            return hr;
        }
        hr = IXThreadingImpl_XTaskQueueDuplicateHandle( threading, queue, &invite_regs[i].queue );
        IXThreadingImpl_Release( threading );
        if (FAILED(hr))
        {
            ReleaseSRWLockExclusive( &invite_regs_lock );
            return hr;
        }
    }
    invite_regs[i].context = context;
    invite_regs[i].callback = callback;
    invite_regs[i].token = invite_next_token++;
    token->token = invite_regs[i].token;
    ReleaseSRWLockExclusive( &invite_regs_lock );

    if (InterlockedCompareExchange( &invite_worker_started, 1, 0 ) == 0)
    {
        if (!(thread = CreateThread( NULL, 0, invite_worker, NULL, 0, NULL )))
        {
            InterlockedExchange( &invite_worker_started, 0 );
            return HRESULT_FROM_WIN32( GetLastError() );
        }
        CloseHandle( thread );
    }
    return S_OK;
}

static BOOLEAN WINAPI game_invite_UnregisterForEvent( IXGameInviteImpl *iface,
        XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    IXThreadingImpl *threading;
    UINT32 i;

    TRACE( "iface %p, token %llu, wait %d.\n", iface,
           (unsigned long long)token.token, wait );
    AcquireSRWLockExclusive( &invite_regs_lock );
    for (i = 0; i < MAX_INVITE_REGISTRATIONS; ++i)
    {
        if (!invite_regs[i].callback || invite_regs[i].token != token.token) continue;
        if (invite_regs[i].queue &&
            SUCCEEDED(QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl,
                                    (void **)&threading )))
        {
            IXThreadingImpl_XTaskQueueCloseHandle( threading, invite_regs[i].queue );
            IXThreadingImpl_Release( threading );
        }
        memset( &invite_regs[i], 0, sizeof(invite_regs[i]) );
        ReleaseSRWLockExclusive( &invite_regs_lock );
        return TRUE;
    }
    ReleaseSRWLockExclusive( &invite_regs_lock );
    return FALSE;
}

static const IXGameInviteImplVtbl game_invite_vtbl =
{
    game_invite_QueryInterface,
    game_invite_AddRef,
    game_invite_Release,
    game_invite_RegisterForEvent,
    game_invite_UnregisterForEvent,
};

static IXGameInviteImpl game_invite = { &game_invite_vtbl };

HRESULT x_game_invite_query( REFIID iid, void **out )
{
    return game_invite_QueryInterface( &game_invite, iid, out );
}
