/*
 * Xbox Game runtime Library Tests
 * 
 * Written by Weather
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

#include <stdlib.h>
#include <stdarg.h>
#define COBJMACROS
#include <initguid.h>
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <roapi.h>
#include <activation.h>
#include <unknwn.h>
#include <xgameerr.h>
#include <xasync.h>
#include <xtaskqueue.h>

#include "wine/test.h"

typedef HRESULT (WINAPI *query_api_impl_fn)( const GUID *runtime_class_id,
                                             REFIID interface_id, void **out );
typedef HRESULT (WINAPI *initialize_api_impl_ex2_fn)( ULONG gdk_version, ULONG game_services_version,
                                                       CHAR mode, void *options );

typedef struct invite_iface invite_iface;
typedef struct threading_iface threading_iface;
typedef void (CALLBACK *invite_callback)( void *context, const char *invite_uri );

struct invite_vtbl
{
    HRESULT (WINAPI *QueryInterface)( invite_iface *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( invite_iface *iface );
    ULONG (WINAPI *Release)( invite_iface *iface );
    HRESULT (WINAPI *RegisterForEvent)( invite_iface *iface, XTaskQueueHandle queue,
                                       void *context, invite_callback callback,
                                       XTaskQueueRegistrationToken *token );
    BOOLEAN (WINAPI *UnregisterForEvent)( invite_iface *iface,
                                         XTaskQueueRegistrationToken token, BOOLEAN wait );
};

struct invite_iface
{
    const struct invite_vtbl *lpVtbl;
};

struct threading_vtbl
{
    HRESULT (WINAPI *QueryInterface)( threading_iface *iface, REFIID iid, void **out );
    ULONG (WINAPI *AddRef)( threading_iface *iface );
    ULONG (WINAPI *Release)( threading_iface *iface );
    HRESULT (WINAPI *XAsyncGetStatus)( threading_iface *iface,
                                      XAsyncBlock *async, BOOLEAN wait );
    void *XAsyncGetResultSize;
    void *XAsyncCancel;
    HRESULT (WINAPI *XAsyncRun)( threading_iface *iface,
                                XAsyncBlock *async, XAsyncWork *work );
    void *XAsyncBegin;
    void *__PADDING__;
    void *XAsyncSchedule;
    void *XAsyncComplete;
    void *XAsyncGetResult;
    HRESULT (WINAPI *XTaskQueueCreate)( threading_iface *iface,
                                       XTaskQueueDispatchMode work_mode,
                                       XTaskQueueDispatchMode completion_mode,
                                       XTaskQueueHandle *queue );
    void *XTaskQueueCreateComposite;
    void *XTaskQueueGetPort;
    HRESULT (WINAPI *XTaskQueueDuplicateHandle)( threading_iface *iface,
                                                 XTaskQueueHandle queue,
                                                 XTaskQueueHandle *duplicate );
    BOOLEAN (WINAPI *XTaskQueueDispatch)( threading_iface *iface,
                                          XTaskQueueHandle queue,
                                          XTaskQueuePort port, UINT32 timeout );
    void (WINAPI *XTaskQueueCloseHandle)( threading_iface *iface,
                                          XTaskQueueHandle queue );
};

struct threading_iface
{
    const struct threading_vtbl *lpVtbl;
};

struct initialize_options
{
    UINT32 unknown;
    BOOLEAN is_inline_config;
    const char *game_config;
};

struct invite_test_state
{
    HANDLE event;
    HANDLE release;
    invite_iface *invite;
    XTaskQueueRegistrationToken token;
    BOOLEAN unregister_result;
    LONG calls;
    char uri[512];
};

struct unregister_test_state
{
    invite_iface *invite;
    XTaskQueueRegistrationToken token;
    HANDLE done;
    BOOLEAN result;
};

struct dispatch_test_state
{
    threading_iface *threading;
    XTaskQueueHandle queue;
    HANDLE done;
    BOOLEAN result;
};

DEFINE_GUID( CLSID_XGameUiImpl, 0xdfcd4649, 0x4ff8, 0x4043, 0xba, 0x07,
             0x35, 0xd6, 0x07, 0xdf, 0x98, 0xb0 );
DEFINE_GUID( CLSID_XGameInviteImpl, 0x0651aae2, 0x4012, 0x4077, 0xbf, 0x84,
             0x8b, 0x90, 0x97, 0x09, 0x0e, 0x2c );
DEFINE_GUID( IID_IXGameUiImpl2, 0x36a03122, 0x9ea3, 0x4a3a, 0xa8, 0xa4,
             0x89, 0x9c, 0xfd, 0x85, 0xd7, 0xdb );
DEFINE_GUID( CLSID_XThreadingImpl, 0x073b7dcb, 0x1fcf, 0x4030, 0x94, 0xbe,
             0xe3, 0xc9, 0xeb, 0x62, 0x34, 0x28 );

static void test_xgameui_query(void)
{
    query_api_impl_fn query_api;
    IUnknown *xgameui = NULL;
    WCHAR module_path[MAX_PATH];
    HMODULE module;
    HRESULT hr;

    if (!GetEnvironmentVariableW( L"XGAMERUNTIME_TEST_DLL", module_path, ARRAY_SIZE(module_path) ))
        lstrcpyW( module_path, L"xgameruntime.dll" );
    module = LoadLibraryW( module_path );
    ok( !!module, "failed to load xgameruntime.dll, error %lu\n", GetLastError() );
    if (!module) return;

    query_api = (query_api_impl_fn)GetProcAddress( module, "QueryApiImpl" );
    ok( !!query_api, "QueryApiImpl export is missing\n" );
    if (!query_api) return;
    trace( "loaded %s at %p, QueryApiImpl at %p\n", wine_dbgstr_w(module_path), module, query_api );

    hr = query_api( &CLSID_XGameUiImpl, &IID_IUnknown, (void **)&xgameui );
    ok( hr == S_OK, "QueryApiImpl(XGameUi) returned %#lx\n", hr );
    ok( !!xgameui, "QueryApiImpl(XGameUi) returned a null interface\n" );
    if (xgameui)
    {
        hr = IUnknown_QueryInterface( xgameui, &IID_IUnknown, NULL );
        ok( hr == E_POINTER, "XGameUi QueryInterface(NULL) returned %#lx\n", hr );
        IUnknown_Release( xgameui );
    }

    xgameui = NULL;
    hr = query_api( &CLSID_XGameUiImpl, &IID_IXGameUiImpl2, (void **)&xgameui );
    ok( hr == S_OK, "QueryApiImpl(XGameUi, IXGameUiImpl2) returned %#lx\n", hr );
    ok( !!xgameui, "QueryApiImpl(XGameUi, IXGameUiImpl2) returned a null interface\n" );
    if (xgameui) IUnknown_Release( xgameui );
    FreeLibrary( module );
}

static void test_xgameinvite_query(void)
{
    query_api_impl_fn query_api;
    IUnknown *invite = NULL;
    WCHAR module_path[MAX_PATH];
    HMODULE module;
    HRESULT hr;

    if (!GetEnvironmentVariableW( L"XGAMERUNTIME_TEST_DLL", module_path, ARRAY_SIZE(module_path) ))
        lstrcpyW( module_path, L"xgameruntime.dll" );
    module = LoadLibraryW( module_path );
    ok( !!module, "failed to load xgameruntime.dll, error %lu\n", GetLastError() );
    if (!module) return;

    query_api = (query_api_impl_fn)GetProcAddress( module, "QueryApiImpl" );
    ok( !!query_api, "QueryApiImpl export is missing\n" );
    if (!query_api) return;

    hr = query_api( &CLSID_XGameInviteImpl, &IID_IUnknown, (void **)&invite );
    ok( hr == S_OK, "QueryApiImpl(XGameInvite) returned %#lx\n", hr );
    ok( !!invite, "QueryApiImpl(XGameInvite) returned a null interface\n" );
    if (invite) IUnknown_Release( invite );

    invite = NULL;
    hr = query_api( &CLSID_XGameInviteImpl, &CLSID_XGameInviteImpl, (void **)&invite );
    ok( hr == S_OK, "QueryApiImpl(XGameInvite, official interface id) returned %#lx\n", hr );
    ok( !!invite, "QueryApiImpl(XGameInvite, official interface id) returned a null interface\n" );
    if (invite) IUnknown_Release( invite );
    FreeLibrary( module );
}

static void CALLBACK invite_received( void *context, const char *invite_uri )
{
    struct invite_test_state *state = context;

    lstrcpynA( state->uri, invite_uri, ARRAY_SIZE(state->uri) );
    SetEvent( state->event );
}

static void CALLBACK invite_received_delayed( void *context, const char *invite_uri )
{
    struct invite_test_state *state = context;

    lstrcpynA( state->uri, invite_uri, ARRAY_SIZE(state->uri) );
    InterlockedIncrement( &state->calls );
    SetEvent( state->event );
    WaitForSingleObject( state->release, 30000 );
}

static void CALLBACK invite_received_suppressed( void *context, const char *invite_uri )
{
    struct invite_test_state *state = context;

    InterlockedIncrement( &state->calls );
    SetEvent( state->event );
}

static void CALLBACK invite_received_self_unregister( void *context, const char *invite_uri )
{
    struct invite_test_state *state = context;

    InterlockedIncrement( &state->calls );
    state->unregister_result = state->invite->lpVtbl->UnregisterForEvent(
            state->invite, state->token, TRUE );
    SetEvent( state->event );
}

static DWORD WINAPI unregister_thread( void *context )
{
    struct unregister_test_state *state = context;

    state->result = state->invite->lpVtbl->UnregisterForEvent(
            state->invite, state->token, TRUE );
    SetEvent( state->done );
    return 0;
}

static DWORD WINAPI dispatch_thread( void *context )
{
    struct dispatch_test_state *state = context;

    state->result = state->threading->lpVtbl->XTaskQueueDispatch(
            state->threading, state->queue, XTaskQueuePort_Completion, 30000 );
    SetEvent( state->done );
    return 0;
}

static HKEY suppress_threading_prompt( BOOL *had_value, DWORD *previous )
{
    static const char key_name[] = "Software\\Wine\\WineGDK";
    static const char value_name[] = "LoadOtherRuntimeAsked";
    DWORD value = 1, size = sizeof(*previous), type;
    HKEY key = NULL;

    if (RegCreateKeyExA( HKEY_LOCAL_MACHINE, key_name, 0, NULL, 0,
                         KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL ))
        return NULL;
    *had_value = !RegQueryValueExA( key, value_name, NULL, &type,
                                    (BYTE *)previous, &size );
    RegSetValueExA( key, value_name, 0, REG_DWORD,
                    (const BYTE *)&value, sizeof(value) );
    return key;
}

static void restore_threading_prompt( HKEY key, BOOL had_value, DWORD previous )
{
    static const char value_name[] = "LoadOtherRuntimeAsked";

    if (!key) return;
    if (had_value)
        RegSetValueExA( key, value_name, 0, REG_DWORD,
                        (const BYTE *)&previous, sizeof(previous) );
    else
        RegDeleteValueA( key, value_name );
    RegCloseKey( key );
}

struct async_test_state
{
    LONG work_calls;
    LONG completion_calls;
};

static HRESULT WINAPI async_work( XAsyncBlock *async )
{
    struct async_test_state *state = async->context;

    ++state->work_calls;
    return S_OK;
}

static void WINAPI async_completed( XAsyncBlock *async )
{
    struct async_test_state *state = async->context;

    ++state->completion_calls;
}

static void test_xasync_schedule(void)
{
    struct async_test_state state = {0};
    XAsyncBlock async = {0};
    threading_iface *threading = NULL;
    query_api_impl_fn query_api;
    BOOL had_value = FALSE;
    DWORD previous = 0;
    WCHAR module_path[MAX_PATH];
    HMODULE module;
    HKEY key;
    HRESULT hr;

    key = suppress_threading_prompt( &had_value, &previous );
    ok( !!key, "failed to suppress the legacy threading prompt\n" );
    if (!key) return;
    if (!GetEnvironmentVariableW( L"XGAMERUNTIME_TEST_DLL", module_path, ARRAY_SIZE(module_path) ))
        lstrcpyW( module_path, L"xgameruntime.dll" );
    module = LoadLibraryW( module_path );
    ok( !!module, "failed to load xgameruntime.dll, error %lu\n", GetLastError() );
    if (!module) goto done;
    query_api = (query_api_impl_fn)GetProcAddress( module, "QueryApiImpl" );
    ok( !!query_api, "QueryApiImpl export is missing\n" );
    if (!query_api) goto done;
    hr = query_api( &CLSID_XThreadingImpl, &CLSID_XThreadingImpl, (void **)&threading );
    ok( hr == S_OK && !!threading, "QueryApiImpl(XThreading) returned %#lx, %p\n", hr, threading );
    if (!threading) goto done;
    hr = threading->lpVtbl->XTaskQueueCreate( threading, XTaskQueueDispatchMode_Manual,
                                              XTaskQueueDispatchMode_Manual, &async.queue );
    ok( hr == S_OK, "XTaskQueueCreate returned %#lx\n", hr );
    if (FAILED(hr)) goto done;

    async.context = &state;
    async.callback = async_completed;
    /* Run with WINEDEBUG=+gdkc too: scheduling must not trace a detached state. */
    hr = threading->lpVtbl->XAsyncRun( threading, &async, async_work );
    ok( hr == S_OK, "XAsyncRun returned %#lx\n", hr );
    if (FAILED(hr)) goto done;
    hr = threading->lpVtbl->XAsyncGetStatus( threading, &async, FALSE );
    ok( hr == E_PENDING, "unscheduled work status %#lx\n", hr );
    ok( !state.work_calls && !state.completion_calls, "manual queue ran early\n" );
    ok( threading->lpVtbl->XTaskQueueDispatch( threading, async.queue, XTaskQueuePort_Work, 1000 ),
        "work dispatch failed\n" );
    ok( state.work_calls == 1 && !state.completion_calls, "unexpected work/completion count %ld/%ld\n",
        state.work_calls, state.completion_calls );
    ok( threading->lpVtbl->XTaskQueueDispatch( threading, async.queue, XTaskQueuePort_Completion, 1000 ),
        "completion dispatch failed\n" );
    hr = threading->lpVtbl->XAsyncGetStatus( threading, &async, FALSE );
    ok( hr == S_OK, "completed work status %#lx\n", hr );
    ok( state.work_calls == 1 && state.completion_calls == 1, "unexpected final counts %ld/%ld\n",
        state.work_calls, state.completion_calls );

done:
    if (threading && async.queue) threading->lpVtbl->XTaskQueueCloseHandle( threading, async.queue );
    if (threading) threading->lpVtbl->Release( threading );
    if (module) FreeLibrary( module );
    restore_threading_prompt( key, had_value, previous );
}

static void test_xgameinvite_relay(void)
{
    static const char expected_uri[] =
        "ms-xbl-multiplayer://inviteAccept?"
        "invitedUser=123456789&sender=987654321&connectionString=fixture";
    static const char config[] = "<Game><TitleId>6DD4E56D</TitleId></Game>";
    struct initialize_options options = {0, TRUE, config};
    struct invite_test_state state = {0}, suppressed_state = {0}, self_state = {0};
    struct unregister_test_state unregister_state = {0};
    struct dispatch_test_state dispatch_state = {0};
    initialize_api_impl_ex2_fn initialize;
    query_api_impl_fn query_api;
    XTaskQueueRegistrationToken token, suppressed_token, reuse_token, previous_token = {0};
    XTaskQueueHandle manual_queue = NULL, dispatch_queue = NULL, undispatched_queue = NULL;
    XTaskQueueHandle immediate_queue = NULL;
    threading_iface *threading = NULL;
    invite_iface *invite = NULL;
    HANDLE unregister_handle = NULL, dispatch_handle = NULL;
    BOOL had_prompt_value = FALSE;
    DWORD previous_prompt_value = 0;
    HKEY prompt_key = NULL;
    HMODULE module;
    HRESULT hr;
    DWORD wait;
    UINT32 i;

    if (!GetEnvironmentVariableA( "XGAMERUNTIME_TEST_INVITE_RELAY", NULL, 0 ))
    {
        skip( "XGAMERUNTIME_TEST_INVITE_RELAY is not set\n" );
        return;
    }

    module = LoadLibraryW( L"xgameruntime.dll" );
    ok( !!module, "failed to load xgameruntime.dll, error %lu\n", GetLastError() );
    if (!module) return;

    initialize = (initialize_api_impl_ex2_fn)GetProcAddress( module, "InitializeApiImplEx2" );
    query_api = (query_api_impl_fn)GetProcAddress( module, "QueryApiImpl" );
    ok( !!initialize, "InitializeApiImplEx2 export is missing\n" );
    ok( !!query_api, "QueryApiImpl export is missing\n" );
    if (!initialize || !query_api) goto done;

    hr = initialize( 10001, 3229, 0, &options );
    ok( hr == S_OK, "InitializeApiImplEx2 returned %#lx\n", hr );
    if (FAILED(hr)) goto done;

    hr = query_api( &CLSID_XGameInviteImpl, &CLSID_XGameInviteImpl, (void **)&invite );
    ok( hr == S_OK, "QueryApiImpl(XGameInvite) returned %#lx\n", hr );
    ok( !!invite, "QueryApiImpl(XGameInvite) returned a null interface\n" );
    if (!invite) goto done;

    prompt_key = suppress_threading_prompt( &had_prompt_value, &previous_prompt_value );
    ok( !!prompt_key, "failed to suppress the legacy threading prompt, error %lu\n",
        GetLastError() );
    hr = query_api( &CLSID_XThreadingImpl, &CLSID_XThreadingImpl, (void **)&threading );
    ok( hr == S_OK, "QueryApiImpl(XThreading) returned %#lx\n", hr );
    ok( !!threading, "QueryApiImpl(XThreading) returned a null interface\n" );
    if (!threading) goto done;

    hr = threading->lpVtbl->XTaskQueueCreate( threading, XTaskQueueDispatchMode_Manual,
                                              XTaskQueueDispatchMode_Manual, &manual_queue );
    ok( hr == S_OK, "XTaskQueueCreate(manual) returned %#lx\n", hr );
    if (FAILED(hr)) goto done;
    hr = threading->lpVtbl->XTaskQueueDuplicateHandle( threading, manual_queue,
                                                       &dispatch_queue );
    ok( hr == S_OK, "XTaskQueueDuplicateHandle returned %#lx\n", hr );
    if (FAILED(hr)) goto done;
    hr = threading->lpVtbl->XTaskQueueCreate( threading, XTaskQueueDispatchMode_Manual,
                                              XTaskQueueDispatchMode_Immediate,
                                              &immediate_queue );
    ok( hr == S_OK, "XTaskQueueCreate(immediate) returned %#lx\n", hr );
    if (FAILED(hr)) goto done;
    hr = threading->lpVtbl->XTaskQueueCreate( threading, XTaskQueueDispatchMode_Manual,
                                              XTaskQueueDispatchMode_Manual,
                                              &undispatched_queue );
    ok( hr == S_OK, "XTaskQueueCreate(undispatched manual) returned %#lx\n", hr );
    if (FAILED(hr)) goto done;

    state.event = CreateEventW( NULL, TRUE, FALSE, NULL );
    state.release = CreateEventW( NULL, TRUE, FALSE, NULL );
    suppressed_state.event = CreateEventW( NULL, TRUE, FALSE, NULL );
    self_state.event = CreateEventW( NULL, TRUE, FALSE, NULL );
    ok( !!state.event, "CreateEvent failed, error %lu\n", GetLastError() );
    ok( !!state.release, "CreateEvent failed, error %lu\n", GetLastError() );
    ok( !!suppressed_state.event, "CreateEvent failed, error %lu\n", GetLastError() );
    ok( !!self_state.event, "CreateEvent failed, error %lu\n", GetLastError() );
    if (!state.event || !state.release || !suppressed_state.event || !self_state.event)
        goto done;

    hr = invite->lpVtbl->RegisterForEvent( invite, manual_queue, &state,
                                           invite_received_delayed, &token );
    ok( hr == S_OK, "XGameInviteRegisterForEvent returned %#lx\n", hr );
    if (FAILED(hr)) goto done;
    hr = invite->lpVtbl->RegisterForEvent( invite, undispatched_queue, &suppressed_state,
                                           invite_received_suppressed, &suppressed_token );
    ok( hr == S_OK, "undispatched XGameInviteRegisterForEvent returned %#lx\n", hr );
    if (FAILED(hr)) goto done;
    self_state.invite = invite;
    hr = invite->lpVtbl->RegisterForEvent( invite, immediate_queue, &self_state,
                                           invite_received_self_unregister,
                                           &self_state.token );
    ok( hr == S_OK, "self XGameInviteRegisterForEvent returned %#lx\n", hr );
    if (FAILED(hr)) goto done;

    threading->lpVtbl->XTaskQueueCloseHandle( threading, manual_queue );
    manual_queue = NULL;

    trace( "invite relay ready for synthetic fixture\n" );
    wait = WaitForSingleObject( self_state.event, 30000 );
    ok( wait == WAIT_OBJECT_0, "self-unregister callback did not complete, wait %#lx\n", wait );
    ok( self_state.unregister_result, "callback self-unregister failed\n" );
    ok( self_state.calls == 1, "self-unregister callback ran %ld times\n", self_state.calls );
    if (wait != WAIT_OBJECT_0) goto done;

    threading->lpVtbl->XTaskQueueCloseHandle( threading, undispatched_queue );
    undispatched_queue = NULL;
    ok( invite->lpVtbl->UnregisterForEvent( invite, suppressed_token, TRUE ),
        "undispatched manual queue unregister failed\n" );
    wait = WaitForSingleObject( suppressed_state.event, 200 );
    ok( wait == WAIT_TIMEOUT, "undispatched callback was not suppressed, wait %#lx\n", wait );
    ok( !suppressed_state.calls, "undispatched callback ran %ld times\n",
        suppressed_state.calls );

    dispatch_state.threading = threading;
    dispatch_state.queue = dispatch_queue;
    dispatch_state.done = CreateEventW( NULL, TRUE, FALSE, NULL );
    dispatch_handle = CreateThread( NULL, 0, dispatch_thread, &dispatch_state, 0, NULL );
    ok( !!dispatch_state.done && !!dispatch_handle,
        "failed to create dispatch synchronization, error %lu\n", GetLastError() );
    if (!dispatch_state.done || !dispatch_handle) goto done;

    wait = WaitForSingleObject( state.event, 30000 );
    ok( wait == WAIT_OBJECT_0, "delayed callback did not start, wait %#lx\n", wait );
    if (wait != WAIT_OBJECT_0) goto done;

    unregister_state.invite = invite;
    unregister_state.token = token;
    unregister_state.done = CreateEventW( NULL, TRUE, FALSE, NULL );
    unregister_handle = CreateThread( NULL, 0, unregister_thread, &unregister_state, 0, NULL );
    ok( !!unregister_state.done && !!unregister_handle,
        "failed to create unregister synchronization, error %lu\n", GetLastError() );
    if (!unregister_state.done || !unregister_handle) goto done;
    wait = WaitForSingleObject( unregister_state.done, 200 );
    ok( wait == WAIT_TIMEOUT, "wait=TRUE returned while a callback was in flight\n" );
    SetEvent( state.release );
    wait = WaitForSingleObject( unregister_state.done, 30000 );
    ok( wait == WAIT_OBJECT_0, "wait=TRUE did not finish after callback release\n" );
    ok( unregister_state.result, "wait=TRUE unregister failed\n" );
    wait = WaitForSingleObject( dispatch_state.done, 30000 );
    ok( wait == WAIT_OBJECT_0 && dispatch_state.result,
        "manual completion dispatch failed, wait %#lx, result %d\n",
        wait, dispatch_state.result );
    ok( state.calls == 1, "delayed callback ran %ld times\n", state.calls );
    ok( !strcmp( state.uri, expected_uri), "got an unexpected synthetic URI\n" );

    for (i = 0; i < 4; ++i)
    {
        hr = invite->lpVtbl->RegisterForEvent( invite, dispatch_queue, &state,
                                               invite_received, &reuse_token );
        ok( hr == S_OK, "slot reuse registration %u returned %#lx\n", i, hr );
        if (FAILED(hr)) break;
        ok( reuse_token.token && reuse_token.token != previous_token.token,
            "slot reuse returned an invalid or repeated token\n" );
        ok( invite->lpVtbl->UnregisterForEvent( invite, reuse_token, TRUE ),
            "slot reuse unregister %u failed\n", i );
        previous_token = reuse_token;
    }

    threading->lpVtbl->XTaskQueueCloseHandle( threading, immediate_queue );
    immediate_queue = NULL;
    threading->lpVtbl->XTaskQueueCloseHandle( threading, dispatch_queue );
    dispatch_queue = NULL;
    i = threading->lpVtbl->Release( threading );
    threading = NULL;
    ok( i == 1, "invite teardown retained %u threading references\n", i - 1 );

done:
    SetEvent( state.release );
    if (unregister_handle) CloseHandle( unregister_handle );
    if (dispatch_handle) CloseHandle( dispatch_handle );
    if (unregister_state.done) CloseHandle( unregister_state.done );
    if (dispatch_state.done) CloseHandle( dispatch_state.done );
    if (state.event) CloseHandle( state.event );
    if (state.release) CloseHandle( state.release );
    if (suppressed_state.event) CloseHandle( suppressed_state.event );
    if (self_state.event) CloseHandle( self_state.event );
    if (threading && immediate_queue)
        threading->lpVtbl->XTaskQueueCloseHandle( threading, immediate_queue );
    if (threading && dispatch_queue)
        threading->lpVtbl->XTaskQueueCloseHandle( threading, dispatch_queue );
    if (threading && undispatched_queue)
        threading->lpVtbl->XTaskQueueCloseHandle( threading, undispatched_queue );
    if (threading && manual_queue)
        threading->lpVtbl->XTaskQueueCloseHandle( threading, manual_queue );
    if (threading) threading->lpVtbl->Release( threading );
    restore_threading_prompt( prompt_key, had_prompt_value, previous_prompt_value );
    if (invite) invite->lpVtbl->Release( invite );
    FreeLibrary( module );
}

START_TEST(xgameruntime)
{
    test_xasync_schedule();
    test_xgameui_query();
    test_xgameinvite_query();
    test_xgameinvite_relay();
}
