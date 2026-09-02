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
#include <xtaskqueue.h>

#include "wine/test.h"

typedef HRESULT (WINAPI *query_api_impl_fn)( const GUID *runtime_class_id,
                                             REFIID interface_id, void **out );
typedef HRESULT (WINAPI *initialize_api_impl_ex2_fn)( ULONG gdk_version, ULONG game_services_version,
                                                       CHAR mode, void *options );

typedef struct invite_iface invite_iface;
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

struct initialize_options
{
    UINT32 unknown;
    BOOLEAN is_inline_config;
    const char *game_config;
};

struct invite_test_state
{
    HANDLE event;
    char uri[512];
};

DEFINE_GUID( CLSID_XGameUiImpl, 0xdfcd4649, 0x4ff8, 0x4043, 0xba, 0x07,
             0x35, 0xd6, 0x07, 0xdf, 0x98, 0xb0 );
DEFINE_GUID( CLSID_XGameInviteImpl, 0x0651aae2, 0x4012, 0x4077, 0xbf, 0x84,
             0x8b, 0x90, 0x97, 0x09, 0x0e, 0x2c );

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

static void test_xgameinvite_relay(void)
{
    static const char expected_uri[] =
        "ms-xbl-multiplayer://inviteAccept?invitedUser=1&sender=2&connectionString=test%20relay";
    static const char config[] = "<Game><TitleId>6DD4E56D</TitleId></Game>";
    struct initialize_options options = {0, TRUE, config};
    struct invite_test_state state = {0};
    initialize_api_impl_ex2_fn initialize;
    query_api_impl_fn query_api;
    XTaskQueueRegistrationToken token;
    invite_iface *invite = NULL;
    HMODULE module;
    HRESULT hr;
    DWORD wait;

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

    state.event = CreateEventW( NULL, TRUE, FALSE, NULL );
    ok( !!state.event, "CreateEvent failed, error %lu\n", GetLastError() );
    if (!state.event) goto done;

    hr = invite->lpVtbl->RegisterForEvent( invite, NULL, &state, invite_received, &token );
    ok( hr == S_OK, "XGameInviteRegisterForEvent returned %#lx\n", hr );
    if (FAILED(hr)) goto done;

    trace( "invite relay ready; publish %s\n", expected_uri );
    wait = WaitForSingleObject( state.event, 30000 );
    ok( wait == WAIT_OBJECT_0, "timed out waiting for invite activation, wait %#lx\n", wait );
    if (wait == WAIT_OBJECT_0)
        ok( !strcmp( state.uri, expected_uri ), "got unexpected URI %s\n", state.uri );
    ok( invite->lpVtbl->UnregisterForEvent( invite, token, TRUE ),
        "XGameInviteUnregisterForEvent failed\n" );

done:
    if (state.event) CloseHandle( state.event );
    if (invite) invite->lpVtbl->Release( invite );
    FreeLibrary( module );
}

START_TEST(xgameruntime)
{
    test_xgameui_query();
    test_xgameinvite_query();
    test_xgameinvite_relay();
}
