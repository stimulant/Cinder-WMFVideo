//ofxWMFVideoPlayer addon written by Philippe Laulheret for Second Story (secondstory.com)
//Based upon Windows SDK samples
//MIT Licensing


// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#ifndef PLAYER_H
#define PLAYER_H

//#include "ofMain.h"
#include <new>
#include <windows.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <assert.h>
#include <tchar.h>
#include <strsafe.h>

// Media Foundation headers
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <evr.h>
#include <vector>

#include "presenter/EVRPresenter.h"
#include "cinder/Signals.h"

template <class T> void SafeRelease( T** ppT )
{
	if( *ppT ) {
		( *ppT )->Release();
		*ppT = NULL;
	}
}

const UINT WM_APP_PLAYER_EVENT = WM_APP + 1;

// WPARAM = IMFMediaEvent*, WPARAM = MediaEventType

enum PlayerState {
	CLOSED = 0,			// No session.
	READY,				// Session was created, ready to open a file.
	OPEN_ASYNC_PENDING,	// Session is creating URL resource
	OPEN_ASYNC_COMPLETE,	// Session finished opening URL
	OPEN_PENDING,		// Session is opening a file.
	STARTED,			// Session is playing a file.
	PAUSED,				// Session is paused.
	STOPPED,			// Session is stopped (ready to play).
	CLOSING				// Application has closed the session, but is waiting for MESessionClosed.
};

const std::string& GetPlayerStateString( const PlayerState p );

typedef cinder::signals::Signal<void()> PresentationEndedSignal;

class CPlayer : public IMFAsyncCallback
{
	public:
		static HRESULT CreateInstance( HWND hVideo, HWND hEvent, CPlayer** ppPlayer );

		// IUnknown methods
		STDMETHODIMP QueryInterface( REFIID iid, void** ppv );
		STDMETHODIMP_( ULONG ) AddRef();
		STDMETHODIMP_( ULONG ) Release();

		// IMFAsyncCallback methods
		STDMETHODIMP GetParameters( DWORD*, DWORD* )
		{
			// Implementation of this method is optional.
			return E_NOTIMPL;
		}
		STDMETHODIMP Invoke( IMFAsyncResult* pAsyncResult );

		// Playback
		HRESULT OpenURL( const WCHAR* sURL, const WCHAR* audioDeviceId = 0 );

		HRESULT	OpenURLAsync( const WCHAR* sURL );
		HRESULT EndOpenURL( const WCHAR* audioDeviceId = 0 );

		//Open multiple url in a same topology... Play with that of you want to do some video syncing
		HRESULT OpenMultipleURL( std::vector<const WCHAR*>& sURL );
		HRESULT Play();
		HRESULT Pause();
		HRESULT Stop();
		HRESULT Shutdown();
		HRESULT HandleEvent( UINT_PTR pUnkPtr );
		HRESULT GetBufferProgress( DWORD* pProgress );
		PlayerState GetState() const { return mState; }
		BOOL HasVideo() const { return ( mVideoDisplay != NULL );  }

		BOOL canRewind()
		{
			DWORD m_caps;
			mSession->GetSessionCapabilities( &m_caps );
			return ( ( m_caps & MFSESSIONCAP_RATE_REVERSE ) == MFSESSIONCAP_RATE_REVERSE );
		}

		float getDuration();
		float getPosition();

		HRESULT SetPlaybackRate( BOOL bThin, float rateRequested );
		float GetPlaybackRate();

		float getWidth() { return mWidth; }
		float getHeight() { return mHeight; }

		HRESULT setPosition( float pos );

		bool isLooping() { return mIsLooping; }
		void setLooping( bool isLooping ) { mIsLooping = isLooping; }

		HRESULT setVolume( float vol );
		float getVolume() { return mCurrentVolume; }

		float getFrameRate();
		int getCurrentFrame();
		int getTotalNumFrames() { return mNumFrames; }

		void firstFrame() { setPosition( 0 ); }
		// void nextFrame();
		// void previousFrame();

		PresentationEndedSignal& getPresentationEndedSignal() { return mPresentationEndedSignal; }

	protected:

		// Constructor is private. Use static CreateInstance method to instantiate.
		CPlayer( HWND hVideo, HWND hEvent );

		// Destructor is private. Caller should call Release.
		virtual ~CPlayer();

		HRESULT Initialize();
		HRESULT CreateSession();
		HRESULT CloseSession();
		HRESULT StartPlayback();

		HRESULT SetMediaInfo( IMFPresentationDescriptor* pPD );

		// Media event handlers
		virtual HRESULT OnTopologyStatus( IMFMediaEvent* pEvent );
		virtual HRESULT OnPresentationEnded( IMFMediaEvent* pEvent );
		virtual HRESULT OnNewPresentation( IMFMediaEvent* pEvent );

		// Override to handle additional session events.
		virtual HRESULT OnSessionEvent( IMFMediaEvent*, MediaEventType )
		{
			return S_OK;
		}

	protected:
		long mRefCount;	// Reference count.

		IMFSequencerSource* mSequencerSource;
		IMFSourceResolver* mSourceResolver;
		IMFMediaSource* mSource;
		IMFVideoDisplayControl* mVideoDisplay;
		MFSequencerElementId mPreviousTopoID;
		HWND mHWNDVideo;	// Video window.
		HWND mHWNDEvent;	// App window to receive events.
		PlayerState mState;	// Current state of the media session.
		HANDLE mCloseEvent;	// Event to wait on while closing.
		PresentationEndedSignal mPresentationEndedSignal; // Signal when presentation ends
		IMFAudioStreamVolume* mVolumeControl;

		bool mIsLooping;
		int mNumFrames;

	public:
		EVRCustomPresenter*	mEVRPresenter; // Custom EVR for texture sharing
		IMFMediaSession* mSession;

		std::vector<EVRCustomPresenter*> mEVRPresenters;  //if you want to load multiple sources in one go
		std::vector<IMFMediaSource*> mSources;        //for doing frame symc... this is experimental

	protected:
		int mWidth;
		int mHeight;
		float mCurrentVolume;
};

#endif PLAYER_H
