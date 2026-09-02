/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XGameUI
 *
 * Copyright 2026 Olivia Ryan
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

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

/* Appended after Xodus IPC and invite relay calls in the Unix module. */
enum game_ui_unix_funcs
{
    show_forza_social_ui = 6,
};

struct x_game_ui
{
    IXGameUiImpl4 IXGameUiImpl4_iface;
    LONG ref;
};

static inline struct x_game_ui *impl_from_IXGameUiImpl4( IXGameUiImpl4 *iface )
{
    return CONTAINING_RECORD( iface, struct x_game_ui, IXGameUiImpl4_iface );
}

static HRESULT WINAPI x_game_ui_QueryInterface( IXGameUiImpl4 *iface, REFIID iid, void **out )
{
    struct x_game_ui *impl = impl_from_IXGameUiImpl4( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown      ) ||
        IsEqualGUID( iid, &IID_IXGameUiImpl  ) ||
        IsEqualGUID( iid, &IID_IXGameUiImpl2 ) ||
        IsEqualGUID( iid, &IID_IXGameUiImpl3 ) ||
        IsEqualGUID( iid, &IID_IXGameUiImpl4 ))
    {
        IXGameUiImpl4_AddRef( *out = &impl->IXGameUiImpl4_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_game_ui_AddRef( IXGameUiImpl4 *iface )
{
    struct x_game_ui *impl = impl_from_IXGameUiImpl4( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI x_game_ui_Release( IXGameUiImpl4 *iface )
{
    struct x_game_ui *impl = impl_from_IXGameUiImpl4( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI forza_social_ui_provider( XAsyncOp op, const XAsyncProviderData *data )
{
    IXThreadingImpl *threading;
    NTSTATUS status;
    HRESULT hr;

    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl,
                                  (void **)&threading )))
        return hr;

    switch (op)
    {
        case XAsyncOp_Begin:
            hr = IXThreadingImpl_XAsyncSchedule( threading, data->async, 0 );
            break;
        case XAsyncOp_DoWork:
            status = __wine_unix_call( unixhandle, show_forza_social_ui,
                                       (void *)XODUS_SOCKET_SUFFIX );
            hr = status ? HRESULT_FROM_NT( status ) : S_OK;
            IXThreadingImpl_XAsyncComplete( threading, data->async, hr, 0 );
            hr = S_OK;
            break;
        default:
            hr = S_OK;
            break;
    }

    IXThreadingImpl_Release( threading );
    return hr;
}

static HRESULT start_forza_social_ui( XAsyncBlock *async, const char *operation )
{
    IXThreadingImpl *threading;
    HRESULT hr;

    if (!async) return E_POINTER;
    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl,
                                  (void **)&threading )))
        return hr;
    hr = IXThreadingImpl_XAsyncBegin( threading, async, NULL, NULL, operation,
                                      forza_social_ui_provider );
    IXThreadingImpl_Release( threading );
    return hr;
}

static HRESULT finish_forza_social_ui( XAsyncBlock *async )
{
    IXThreadingImpl *threading;
    HRESULT hr;

    if (!async) return E_POINTER;
    if (FAILED(hr = QueryApiImpl( &CLSID_XThreadingImpl, &IID_IXThreadingImpl,
                                  (void **)&threading )))
        return hr;
    hr = IXThreadingImpl_XAsyncGetResult( threading, async, NULL, 0, NULL, NULL );
    IXThreadingImpl_Release( threading );
    return hr;
}

static HRESULT WINAPI x_game_ui_XGameUiShowMessageDialogAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, const char *titleText, const char *contentText, const char *firstButtonText, const char *secondButtonText, const char *thirdButtonText, XGameUiMessageDialogButton defaultButton, XGameUiMessageDialogButton cancelButton )
{
    FIXME( "iface %p, async %p, titleText %s, contentText %s, firstButtonText %s, secondButtonText %s, thirdButtonText %s, defaultButton %d, cancelButton %d stub!\n", iface, async, debugstr_a( titleText ), debugstr_a( contentText ), debugstr_a( firstButtonText ), debugstr_a( secondButtonText ), debugstr_a( thirdButtonText ), defaultButton, cancelButton );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowMessageDialogResult( IXGameUiImpl4 *iface, XAsyncBlock *async, XGameUiMessageDialogButton *resultButton )
{
    FIXME( "iface %p, async %p, resultButton %p stub!\n", iface, async, resultButton );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowSendGameInviteAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, XUserHandle requestingUser, const char *sessionConfigurationId, const char *sessionTemplateName, const char *sessionId, const char *invitationText, const char *customActivationContext )
{
    TRACE( "iface %p, async %p.\n", iface, async );
    return start_forza_social_ui( async, "XGameUiShowSendGameInviteAsync" );
}

static HRESULT WINAPI x_game_ui_XGameUiShowSendGameInviteResult( IXGameUiImpl4 *iface, XAsyncBlock *async )
{
    TRACE( "iface %p async %p.\n", iface, async );
    return finish_forza_social_ui( async );
}

static HRESULT WINAPI x_game_ui_XGameUiShowPlayerProfileCardAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, XUserHandle requestingUser, UINT64 targetPlayer )
{
    FIXME( "iface %p, async %p, requestingUser %p, targetPlayer %llu stub!\n", iface, async, requestingUser, targetPlayer );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowPlayerProfileCardResult( IXGameUiImpl4 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowAchievementsAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, XUserHandle requestingUser, UINT32 titleId )
{
    FIXME( "iface %p, async %p, requestingUser %p, titleId %u stub!\n", iface, async, requestingUser, titleId );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowAchievementsResult( IXGameUiImpl4 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowPlayerPickerAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, XUserHandle requestingUser, const char *promptText, UINT32 selectFromPlayersCount, const UINT64 *selectFromPlayers, UINT32 preSelectedPlayersCount, UINT64 *preSelectedPlayers, UINT32 minSelectionCount, UINT32 maxSelectionCount )
{
    FIXME( "iface %p, async %p, requestingUser %p, promptText %s, selectFromPlayersCount %u, selectFromPlayers %p, preSelectedPlayersCount %u, preSelectedPlayers %p, minSelectionCount %u, maxSelectionCount %u stub!\n", iface, async, requestingUser, debugstr_a( promptText ), selectFromPlayersCount, selectFromPlayers, preSelectedPlayersCount, preSelectedPlayers, minSelectionCount, maxSelectionCount );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowPlayerPickerResultCount( IXGameUiImpl4 *iface, XAsyncBlock *async, UINT32 *resultPlayersCount )
{
    FIXME( "iface %p, async %p, resultPlayersCount %p stub!\n", iface, async, resultPlayersCount );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowPlayerPickerResult( IXGameUiImpl4 *iface, XAsyncBlock *async, UINT32 resultPlayersCount, UINT64 *resultPlayers, UINT32 *resultPlayersUsed )
{
    FIXME( "iface %p, async %p, resultPlayersCount %u, resultPlayers %p, resultPlayersUsed %p stub!\n", iface, async, resultPlayersCount, resultPlayers, resultPlayersUsed );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowErrorDialogAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, HRESULT errorCode, const char *context )
{
    FIXME( "iface %p, async %p, errorCode %#lx, context %s stub!\n", iface, async, errorCode, debugstr_a( context ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowErrorDialogResult( IXGameUiImpl4 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetNotificationPositionHint( IXGameUiImpl4 *iface, XGameUiNotificationPositionHint position )
{
    FIXME( "iface %p, position %d stub!\n", iface, position );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowTextEntryAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, const char *titleText, const char *descriptionText, const char *defaultText, XGameUiTextEntryInputScope inputScope, UINT32 maxTextLength )
{
    FIXME( "iface %p, async %p, titleText %s, descriptionText %s, defaultText %s, inputScope %d, maxTextLength %u stub!\n", iface, async, debugstr_a( titleText ), debugstr_a( descriptionText ), debugstr_a( defaultText ), inputScope, maxTextLength );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowTextEntryResultSize( IXGameUiImpl4 *iface, XAsyncBlock *async, UINT32 *resultTextBufferSize )
{
    FIXME( "iface %p, async %p, resultTextBufferSize %p stub!\n", iface, async, resultTextBufferSize );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowTextEntryResult( IXGameUiImpl4 *iface, XAsyncBlock *async, UINT32 resultTextBufferSize, char *resultTextBuffer, UINT32 *resultTextBufferUsed )
{
    FIXME( "iface %p, async %p, resultTextBufferSize %u, resultTextBuffer %p, resultTextBufferUsed %p stub!\n", iface, async, resultTextBufferSize, resultTextBuffer, resultTextBufferUsed );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING__( IXGameUiImpl4 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING_2__( IXGameUiImpl4 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING_3__( IXGameUiImpl4 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING_4__( IXGameUiImpl4 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowWebAuthenticationAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, XUserHandle requestingUser, const char *requestUri, const char *completionUri )
{
    FIXME( "iface %p, async %p, requestingUser %p, requestUri %s, completionUri %s stub!\n", iface, async, requestingUser, debugstr_a( requestUri ), debugstr_a( completionUri ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowWebAuthenticationResultSize( IXGameUiImpl4 *iface, XAsyncBlock *async, SIZE_T *bufferSize )
{
    FIXME( "iface %p, async %p, bufferSize %p stub!\n", iface, async, bufferSize );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowWebAuthenticationResult( IXGameUiImpl4 *iface, XAsyncBlock *async, SIZE_T bufferSize, void *buffer, XGameUiWebAuthenticationResultData **ptrToBuffer, SIZE_T *bufferUsed )
{
    FIXME( "iface %p, async %p, bufferSize %Iu, buffer %p, ptrToBuffer %p, bufferUsed %p stub!\n", iface, async, bufferSize, buffer, ptrToBuffer, bufferUsed );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowWebAuthenticationWithOptionsAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, XUserHandle requestingUser, const char *requestUri, const char *completionUri, XGameUiWebAuthenticationOptions options )
{
    FIXME( "iface %p, async %p, requestingUser %p, requestUri %s, completionUri %s, options %d stub!\n", iface, async, requestingUser, debugstr_a( requestUri ), debugstr_a( completionUri ), options );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING_5__( IXGameUiImpl4 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING_6__( IXGameUiImpl4 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowMultiplayerActivityGameInviteAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, XUserHandle requestingUser )
{
    TRACE( "iface %p, async %p.\n", iface, async );
    return start_forza_social_ui( async,
                                 "XGameUiShowMultiplayerActivityGameInviteAsync" );
}

static HRESULT WINAPI x_game_ui_XGameUiShowMultiplayerActivityGameInviteResult( IXGameUiImpl4 *iface, XAsyncBlock *async )
{
    TRACE( "iface %p, async %p.\n", iface, async );
    return finish_forza_social_ui( async );
}

static HRESULT WINAPI __PADDING_7__( IXGameUiImpl4 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING_8__( IXGameUiImpl4 *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiTextEntryOpen( IXGameUiImpl4 *iface, const XGameUiTextEntryOptions *options, UINT32 maxLength, const char *initialText, UINT32 initialCursorIndex, XGameUiTextEntryHandle *handle )
{
    FIXME( "iface %p, options %p, maxLength %u, initialText %s, initialCursorIndex %u, handle %p stub!\n", iface, options, maxLength, debugstr_a( initialText ), initialCursorIndex, handle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiTextEntryClose( IXGameUiImpl4 *iface, XGameUiTextEntryHandle handle )
{
    FIXME( "iface %p, handle %p stub!\n", iface, handle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiTextEntryGetState( IXGameUiImpl4 *iface, XGameUiTextEntryHandle handle, XGameUiTextEntryChangeTypeFlags *changeType, UINT32 *cursorIndex, UINT32 *imeClauseStartIndex, UINT32 *imeClauseEndIndex, UINT32 bufferSize, char *buffer )
{
    FIXME( "iface %p, handle %p, changeType %p, cursorIndex %p, imeClauseStartIndex %p, imeClauseEndIndex %p, bufferSize %u, buffer %p stub!\n", iface, handle, changeType, cursorIndex, imeClauseStartIndex, imeClauseEndIndex, bufferSize, buffer );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiTextEntryGetExtents( IXGameUiImpl4 *iface, XGameUiTextEntryHandle handle, XGameUiTextEntryExtents *extents )
{
    FIXME( "iface %p, handle %p, extents %p stub!\n", iface, handle, extents );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiTextEntryUpdatePositionHint( IXGameUiImpl4 *iface, XGameUiTextEntryHandle handle, XGameUiTextEntryPositionHint positionHint )
{
    FIXME( "iface %p, handle %p, positionHint %d stub!\n", iface, handle, positionHint );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiTextEntryUpdateVisibility( IXGameUiImpl4 *iface, XGameUiTextEntryHandle handle, XGameUiTextEntryVisibilityFlags visibilityFlags )
{
    FIXME( "iface %p, handle %p, visibilityFlags %d stub!\n", iface, handle, visibilityFlags );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowStateShareAsync( IXGameUiImpl4 *iface, XAsyncBlock *async, XUserHandle requestingUser, const char *linkToken )
{
    FIXME( "iface %p, async %p, requestingUser %p, linkToken %s stub!\n", iface, async, requestingUser, debugstr_a( linkToken ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiShowStateShareResult( IXGameUiImpl4 *iface, XAsyncBlock *async )
{
    FIXME( "iface %p, async %p stub!\n", iface, async );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetUiCallbacks( IXGameUiImpl4 *iface, const XGameUiUiCallbacks *callbacks, BOOLEAN useSystemUiIfAvailable )
{
    FIXME( "iface %p, callbacks %p, useSystemUiIfAvailable %d stub!\n", iface, callbacks, useSystemUiIfAvailable );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetMessageDialogUiResponse( IXGameUiImpl4 *iface, XGameUiCallbackHandle callbackHandle, XGameUiMessageDialogButton response )
{
    FIXME( "iface %p, callbackHandle %p, response %d stub!\n", iface, callbackHandle, response );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetPlayerPickerUiResponse( IXGameUiImpl4 *iface, XGameUiCallbackHandle callbackHandle, UINT32 playerCount, const UINT64 *players )
{
    FIXME( "iface %p, callbackHandle %p, playerCount %u, players %p stub!\n", iface, callbackHandle, playerCount, players );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetTextEntryUiResponse( IXGameUiImpl4 *iface, XGameUiCallbackHandle callbackHandle, const char *response )
{
    FIXME( "iface %p, callbackHandle %p, response %s stub!\n", iface, callbackHandle, debugstr_a( response ) );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetPlayerProfileCardUiResponse( IXGameUiImpl4 *iface, XGameUiCallbackHandle callbackHandle )
{
    FIXME( "iface %p, callbackHandle %p stub!\n", iface, callbackHandle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetSendGameInviteUiResponse( IXGameUiImpl4 *iface, XGameUiCallbackHandle callbackHandle )
{
    FIXME( "iface %p, callbackHandle %p stub!\n", iface, callbackHandle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetAchievementsUiResponse( IXGameUiImpl4 *iface, XGameUiCallbackHandle callbackHandle )
{
    FIXME( "iface %p, callbackHandle %p stub!\n", iface, callbackHandle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetMultiplayerActivityGameInviteUiResponse( IXGameUiImpl4 *iface, XGameUiCallbackHandle callbackHandle )
{
    FIXME( "iface %p, callbackHandle %p stub!\n", iface, callbackHandle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_game_ui_XGameUiSetErrorDialogUiResponse( IXGameUiImpl4 *iface, XGameUiCallbackHandle callbackHandle )
{
    FIXME( "iface %p, callbackHandle %p stub!\n", iface, callbackHandle );
    return E_NOTIMPL;
}

static const struct IXGameUiImpl4Vtbl x_game_ui_vtbl =
{
    x_game_ui_QueryInterface,
    x_game_ui_AddRef,
    x_game_ui_Release,
    /* IXGameUiImpl methods */
    x_game_ui_XGameUiShowMessageDialogAsync,
    x_game_ui_XGameUiShowMessageDialogResult,
    x_game_ui_XGameUiShowSendGameInviteAsync,
    x_game_ui_XGameUiShowSendGameInviteResult,
    x_game_ui_XGameUiShowPlayerProfileCardAsync,
    x_game_ui_XGameUiShowPlayerProfileCardResult,
    x_game_ui_XGameUiShowAchievementsAsync,
    x_game_ui_XGameUiShowAchievementsResult,
    x_game_ui_XGameUiShowPlayerPickerAsync,
    x_game_ui_XGameUiShowPlayerPickerResultCount,
    x_game_ui_XGameUiShowPlayerPickerResult,
    x_game_ui_XGameUiShowErrorDialogAsync,
    x_game_ui_XGameUiShowErrorDialogResult,
    x_game_ui_XGameUiSetNotificationPositionHint,
    x_game_ui_XGameUiShowTextEntryAsync,
    x_game_ui_XGameUiShowTextEntryResultSize,
    x_game_ui_XGameUiShowTextEntryResult,
    __PADDING__,
    __PADDING_2__,
    __PADDING_3__,
    __PADDING_4__,
    x_game_ui_XGameUiShowWebAuthenticationAsync,
    x_game_ui_XGameUiShowWebAuthenticationResultSize,
    x_game_ui_XGameUiShowWebAuthenticationResult,
    x_game_ui_XGameUiShowWebAuthenticationWithOptionsAsync,
    __PADDING_5__,
    __PADDING_6__,
    /* IXGameUiImpl2 methods */
    x_game_ui_XGameUiShowMultiplayerActivityGameInviteAsync,
    x_game_ui_XGameUiShowMultiplayerActivityGameInviteResult,
    __PADDING_7__,
    __PADDING_8__,
    x_game_ui_XGameUiTextEntryOpen,
    x_game_ui_XGameUiTextEntryClose,
    x_game_ui_XGameUiTextEntryGetState,
    x_game_ui_XGameUiTextEntryGetExtents,
    x_game_ui_XGameUiTextEntryUpdatePositionHint,
    x_game_ui_XGameUiTextEntryUpdateVisibility,
    /* IXGameUiImpl3 methods */
    x_game_ui_XGameUiShowStateShareAsync,
    x_game_ui_XGameUiShowStateShareResult,
    /* IXGameUiImpl4 methods */
    x_game_ui_XGameUiSetUiCallbacks,
    x_game_ui_XGameUiSetMessageDialogUiResponse,
    x_game_ui_XGameUiSetPlayerPickerUiResponse,
    x_game_ui_XGameUiSetTextEntryUiResponse,
    x_game_ui_XGameUiSetPlayerProfileCardUiResponse,
    x_game_ui_XGameUiSetSendGameInviteUiResponse,
    x_game_ui_XGameUiSetAchievementsUiResponse,
    x_game_ui_XGameUiSetMultiplayerActivityGameInviteUiResponse,
    x_game_ui_XGameUiSetErrorDialogUiResponse,
};

static struct x_game_ui x_game_ui =
{
    {&x_game_ui_vtbl},
    0,
};

IXGameUiImpl *x_game_ui_impl = (IXGameUiImpl *)&x_game_ui.IXGameUiImpl4_iface;
