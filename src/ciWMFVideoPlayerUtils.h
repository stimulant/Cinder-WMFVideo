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
	Closed = 0,			// No session.
	Ready,				// Session was created, ready to open a file.
	OpenAsyncPending,	// Session is creating URL resource
	OpenAsyncComplete,	// Session finished opening URL
	OpenPending,		// Session is opening a file.
	Started,			// Session is playing a file.
	Paused,				// Session is paused.
	Stopped,			// Session is stopped (ready to play).
	Closing				// Application has closed the session, but is waiting for MESessionClosed.
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
		PlayerState GetState() const { return m_state; }
		BOOL HasVideo() const { return ( m_pVideoDisplay != NULL );  }

		BOOL canRewind()
		{
			DWORD m_caps;
			m_pSession->GetSessionCapabilities( &m_caps );
			return ( ( m_caps & MFSESSIONCAP_RATE_REVERSE ) == MFSESSIONCAP_RATE_REVERSE );
		}

		float getDuration();
		float getPosition();

		HRESULT SetPlaybackRate( BOOL bThin, float rateRequested );
		float GetPlaybackRate();

		float getWidth() { return _width; }
		float getHeight() { return _height; }

		HRESULT setPosition( float pos );

		bool isLooping() { return _isLooping; }
		void setLooping( bool isLooping ) { _isLooping = isLooping; }

		HRESULT setVolume( float vol );
		float getVolume() { return _currentVolume; }

		float getFrameRate();
		int getCurrentFrame();
		int getTotalNumFrames() { return numFrames; }

		void firstFrame() { setPosition( 0 ); }
		// void nextFrame();
		// void previousFrame();

		BOOL getIsDone() { return isDone; }
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
		long m_nRefCount;	// Reference count.

		IMFSequencerSource* m_pSequencerSource;
		IMFSourceResolver* m_pSourceResolver;
		IMFMediaSource* m_pSource;
		IMFVideoDisplayControl* m_pVideoDisplay;
		MFSequencerElementId _previousTopoID;
		HWND m_hwndVideo;	// Video window.
		HWND m_hwndEvent;	// App window to receive events.
		PlayerState m_state;	// Current state of the media session.
		HANDLE m_hCloseEvent;	// Event to wait on while closing.
		PresentationEndedSignal mPresentationEndedSignal; // Signal when presentation ends
		IMFAudioStreamVolume* m_pVolumeControl;

		bool isDone;
		bool _isLooping;
		int	numFrames;

	public:
		EVRCustomPresenter*	m_pEVRPresenter; // Custom EVR for texture sharing
		IMFMediaSession* m_pSession;

		std::vector<EVRCustomPresenter*> v_EVRPresenters;  //if you want to load multiple sources in one go
		std::vector<IMFMediaSource*> v_sources;        //for doing frame symc... this is experimental

	protected:
		int _width;
		int	_height;
		float _currentVolume;
};

#endif PLAYER_H
