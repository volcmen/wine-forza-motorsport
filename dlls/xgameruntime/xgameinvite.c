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
#include "wine/list.h"

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
    IXThreadingImpl *threading;
    XTaskQueueHandle queue;
    void *context;
    XGameInviteEventCallback callback;
    UINT64 token;
    LONG refs;
    UINT32 in_flight;
    BOOL removed;
    HANDLE idle_event;
    struct list executing;
};

struct invite_execution
{
    struct list entry;
    DWORD thread;
};

struct invite_post
{
    struct list entry;
    struct invite_registration *registration;
    LONG refs;
    ULONG_PTR token;
    char uri[1];
};

const GUID CLSID_XGameInviteImpl =
    {0x0651aae2, 0x4012, 0x4077, {0xbf, 0x84, 0x8b, 0x90, 0x97, 0x09, 0x0e, 0x2c}};

static struct invite_registration *invite_regs[MAX_INVITE_REGISTRATIONS];
static struct list invite_posts = LIST_INIT( invite_posts );
static SRWLOCK invite_regs_lock = SRWLOCK_INIT;
static LONG invite_worker_started;
static UINT64 invite_next_token = 1;
static ULONG_PTR invite_next_post_token = 1;
static LONG invite_ref = 1;

static void invite_registration_addref( struct invite_registration *registration )
{
    InterlockedIncrement( &registration->refs );
}

static void invite_registration_release( struct invite_registration *registration )
{
    if (InterlockedDecrement( &registration->refs )) return;

    if (registration->queue)
        IXThreadingImpl_XTaskQueueCloseHandle( registration->threading,
                                               registration->queue );
    if (registration->threading) IXThreadingImpl_Release( registration->threading );
    CloseHandle( registration->idle_event );
    free( registration );
}

static void invite_post_release( struct invite_post *post )
{
    struct invite_registration *registration = post->registration;

    if (InterlockedDecrement( &post->refs )) return;
    free( post );
    invite_registration_release( registration );
}

/* Must be called with invite_regs_lock held exclusively. */
static struct invite_post *invite_post_claim( ULONG_PTR token )
{
    struct invite_post *post;

    LIST_FOR_EACH_ENTRY( post, &invite_posts, struct invite_post, entry )
    {
        if (post->token != token) continue;
        list_remove( &post->entry );
        return post;
    }
    return NULL;
}

static void CALLBACK invite_trampoline( void *context, BOOLEAN canceled )
{
    struct invite_registration *registration;
    struct invite_execution execution;
    XGameInviteEventCallback callback = NULL;
    void *callback_context = NULL;
    struct invite_post *post;
    ULONG_PTR token = (ULONG_PTR)context;

    AcquireSRWLockExclusive( &invite_regs_lock );
    if (!(post = invite_post_claim( token )))
    {
        ReleaseSRWLockExclusive( &invite_regs_lock );
        return;
    }
    registration = post->registration;
    if (!canceled && !registration->removed)
    {
        execution.thread = GetCurrentThreadId();
        list_add_tail( &registration->executing, &execution.entry );
        if (!registration->in_flight++) ResetEvent( registration->idle_event );
        callback = registration->callback;
        callback_context = registration->context;
    }
    ReleaseSRWLockExclusive( &invite_regs_lock );

    if (callback) callback( callback_context, post->uri );

    AcquireSRWLockExclusive( &invite_regs_lock );
    if (callback)
    {
        list_remove( &execution.entry );
        if (!--registration->in_flight) SetEvent( registration->idle_event );
    }
    ReleaseSRWLockExclusive( &invite_regs_lock );
    invite_post_release( post );
}

static struct invite_post *invite_post_create( struct invite_registration *registration,
                                               const char *uri )
{
    struct invite_post *post;
    size_t length = strlen( uri );

    /* The caller holds invite_regs_lock, so a token is never reused after wrap. */
    if (!invite_next_post_token) return NULL;
    if (!(post = malloc( offsetof(struct invite_post, uri) + length + 1 ))) return NULL;
    post->registration = registration;
    post->refs = 2; /* pending-list ownership plus the submitting worker */
    post->token = invite_next_post_token++;
    memcpy( post->uri, uri, length + 1 );
    invite_registration_addref( registration );
    list_add_tail( &invite_posts, &post->entry );
    return post;
}

static void invite_deliver( struct invite_post *post )
{
    struct invite_registration *registration = post->registration;
    struct invite_post *pending;
    ULONG_PTR token = post->token;
    HRESULT hr;

    if (!registration->queue)
    {
        invite_trampoline( (void *)token, FALSE );
        invite_post_release( post );
        return;
    }
    hr = IXThreadingImpl_XTaskQueueSubmitCallback( registration->threading,
                                                   registration->queue,
                                                   XTaskQueuePort_Completion,
                                                   (void *)token,
                                                   invite_trampoline );
    if (FAILED(hr))
    {
        WARN( "could not queue invite callback, hr %#lx.\n", hr );
        AcquireSRWLockExclusive( &invite_regs_lock );
        pending = invite_post_claim( token );
        ReleaseSRWLockExclusive( &invite_regs_lock );
        if (pending) invite_post_release( pending );
    }
    invite_post_release( post );
}

static DWORD WINAPI invite_worker( void *unused )
{
    struct invite_post *posts[MAX_INVITE_REGISTRATIONS] = {0};
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

        TRACE( "received Xbox invite activation (%u bytes).\n", poll.length );
        AcquireSRWLockExclusive( &invite_regs_lock );
        for (i = 0; i < MAX_INVITE_REGISTRATIONS; ++i)
            if (invite_regs[i] && !invite_regs[i]->removed)
                posts[i] = invite_post_create( invite_regs[i], poll.uri );
        ReleaseSRWLockExclusive( &invite_regs_lock );

        for (i = 0; i < MAX_INVITE_REGISTRATIONS; ++i)
            if (posts[i])
            {
                invite_deliver( posts[i] );
                posts[i] = NULL;
            }
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
    struct invite_registration *registration;
    IXThreadingImpl *threading;
    HANDLE thread;
    UINT32 i;
    HRESULT hr;

    TRACE( "iface %p, queue %p, context %p, callback %p, token %p.\n",
           iface, queue, context, callback, token );
    if (!callback || !token) return E_POINTER;
    token->token = 0;

    if (!(registration = calloc( 1, sizeof(*registration) ))) return E_OUTOFMEMORY;
    registration->refs = 1;
    registration->context = context;
    registration->callback = callback;
    list_init( &registration->executing );
    if (!(registration->idle_event = CreateEventW( NULL, TRUE, TRUE, NULL )))
    {
        free( registration );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    if (queue)
    {
        if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl,
                                      (void **)&threading )))
        {
            invite_registration_release( registration );
            return hr;
        }
        registration->threading = threading;
        hr = IXThreadingImpl_XTaskQueueDuplicateHandle( threading, queue,
                                                        &registration->queue );
        if (FAILED(hr))
        {
            invite_registration_release( registration );
            return hr;
        }
    }

    AcquireSRWLockExclusive( &invite_regs_lock );
    for (i = 0; i < MAX_INVITE_REGISTRATIONS; ++i) if (!invite_regs[i]) break;
    if (i == MAX_INVITE_REGISTRATIONS)
    {
        ReleaseSRWLockExclusive( &invite_regs_lock );
        invite_registration_release( registration );
        return E_OUTOFMEMORY;
    }

    do
    {
        UINT32 j;

        registration->token = invite_next_token++;
        if (!registration->token) continue;
        for (j = 0; j < MAX_INVITE_REGISTRATIONS; ++j)
            if (invite_regs[j] && invite_regs[j]->token == registration->token) break;
        if (j == MAX_INVITE_REGISTRATIONS) break;
    } while (TRUE);
    invite_regs[i] = registration;
    token->token = registration->token;
    ReleaseSRWLockExclusive( &invite_regs_lock );

    if (InterlockedCompareExchange( &invite_worker_started, 1, 0 ) == 0)
    {
        if (!(thread = CreateThread( NULL, 0, invite_worker, NULL, 0, NULL )))
        {
            DWORD error = GetLastError();

            InterlockedExchange( &invite_worker_started, 0 );
            AcquireSRWLockExclusive( &invite_regs_lock );
            if (invite_regs[i] == registration) invite_regs[i] = NULL;
            registration->removed = TRUE;
            ReleaseSRWLockExclusive( &invite_regs_lock );
            token->token = 0;
            invite_registration_release( registration );
            return HRESULT_FROM_WIN32( error );
        }
        CloseHandle( thread );
    }
    return S_OK;
}

static BOOLEAN WINAPI game_invite_UnregisterForEvent( IXGameInviteImpl *iface,
        XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    struct invite_execution *execution;
    struct invite_post *post, *post_next;
    struct list canceled = LIST_INIT( canceled );
    struct invite_registration *registration = NULL;
    BOOL drained, self = FALSE;
    UINT32 i;

    TRACE( "iface %p, token %llu, wait %d.\n", iface,
           (unsigned long long)token.token, wait );
    AcquireSRWLockExclusive( &invite_regs_lock );
    for (i = 0; i < MAX_INVITE_REGISTRATIONS; ++i)
    {
        if (!invite_regs[i] || invite_regs[i]->token != token.token) continue;
        registration = invite_regs[i];
        invite_regs[i] = NULL;
        registration->removed = TRUE;
        LIST_FOR_EACH_ENTRY_SAFE( post, post_next, &invite_posts,
                                  struct invite_post, entry )
        {
            if (post->registration != registration) continue;
            list_remove( &post->entry );
            list_add_tail( &canceled, &post->entry );
        }
        LIST_FOR_EACH_ENTRY( execution, &registration->executing,
                             struct invite_execution, entry )
            if (execution->thread == GetCurrentThreadId())
            {
                self = TRUE;
                break;
            }
        break;
    }
    drained = registration && !registration->in_flight;
    ReleaseSRWLockExclusive( &invite_regs_lock );
    if (!registration) return FALSE;

    LIST_FOR_EACH_ENTRY_SAFE( post, post_next, &canceled,
                              struct invite_post, entry )
    {
        list_remove( &post->entry );
        invite_post_release( post );
    }

    if (wait && !self && !drained)
        drained = WaitForSingleObject( registration->idle_event, INFINITE ) == WAIT_OBJECT_0;
    invite_registration_release( registration );
    return !wait || self || drained;
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
