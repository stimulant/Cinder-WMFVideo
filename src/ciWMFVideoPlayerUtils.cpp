// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.

#include "ciWMFVideoPlayerUtils.h"
#include <assert.h>
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <shobjidl.h>

#pragma comment(lib, "shlwapi")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Propsys.lib")

#include "presenter/Presenter.h"

#include "cinder/Log.h"
#include "cinder/app/App.h"

using namespace std;

const std::string g_PlayerStateString[] = { "Closed", "Ready", "OpenAsyncPending", "OpenAsyncComplete", "OpenPending", "Started", "Paused", "Stopped", "Closing" };
const std::string& GetPlayerStateString( const PlayerState p ) { return g_PlayerStateString[p]; };

template <class Q>
HRESULT GetEventObject( IMFMediaEvent* pEvent, Q** ppObject )
{
	*ppObject = NULL;   // zero output

	PROPVARIANT var;
	HRESULT hr = pEvent->GetValue( &var );

	if( SUCCEEDED( hr ) ) {
		if( var.vt == VT_UNKNOWN ) {
			hr = var.punkVal->QueryInterface( ppObject );
		}
		else {
			hr = MF_E_INVALIDTYPE;
		}

		PropVariantClear( &var );
	}

	return hr;
}

//HRESULT CreateMediaSource(PCWSTR pszURL, IMFMediaSource **ppSource);

HRESULT CreatePlaybackTopology( IMFMediaSource* pSource,
                                IMFPresentationDescriptor* pPD, HWND hVideoWnd, IMFTopology** ppTopology, IMFVideoPresenter* pVideoPresenter, const WCHAR* audioDeviceId = 0 );

HRESULT AddToPlaybackTopology( IMFMediaSource* pSource,
                               IMFPresentationDescriptor* pPD, HWND hVideoWnd, IMFTopology* pTopology, IMFVideoPresenter* pVideoPresenter );

//  Static class method to create the CPlayer object.

HRESULT CPlayer::CreateInstance(
    HWND hVideo,                  // Video window.
    HWND hEvent,                  // Window to receive notifications.
    CPlayer** ppPlayer )          // Receives a pointer to the CPlayer object.
{
	if( ppPlayer == NULL ) {
		return E_POINTER;
	}

	CPlayer* pPlayer = new( std::nothrow ) CPlayer( hVideo, hEvent );

	if( pPlayer == NULL ) {
		return E_OUTOFMEMORY;
	}

	HRESULT hr = pPlayer->Initialize();

	if( SUCCEEDED( hr ) ) {
		*ppPlayer = pPlayer;
	}
	else {
		pPlayer->Release();
	}

	return hr;
}

HRESULT CPlayer::Initialize()
{
	HRESULT hr = 0;

	mCloseEvent = CreateEventA( NULL, FALSE, FALSE, NULL );

	if( mCloseEvent == NULL ) {
		hr = HRESULT_FROM_WIN32( GetLastError() );
	}

	if( !mEVRPresenter )  {
		mEVRPresenter = new EVRCustomPresenter( hr );
		mEVRPresenter->SetVideoWindow( mHWNDVideo );
	}

	return hr;
}

CPlayer::CPlayer( HWND hVideo, HWND hEvent ) :
	mSession( NULL ),
	mSource( NULL ),
	mSourceResolver( NULL ),
	mVideoDisplay( NULL ),
	mHWNDVideo( hVideo ),
	mHWNDEvent( hEvent ),
	mState( CLOSED ),
	mCloseEvent( NULL ),
	mRefCount( 1 ),
	mEVRPresenter( NULL ),
	mSequencerSource( NULL ),
	mVolumeControl( NULL ),
	mPreviousTopoID( 0 ),
	mIsLooping( false )
{

}

CPlayer::~CPlayer()
{
	assert( mSession == NULL );
	// If FALSE, the app did not call Shutdown().

	// When CPlayer calls IMediaEventGenerator::BeginGetEvent on the
	// media session, it causes the media session to hold a reference
	// count on the CPlayer.

	// This creates a circular reference count between CPlayer and the
	// media session. Calling Shutdown breaks the circular reference
	// count.

	// If CreateInstance fails, the application will not call
	// Shutdown. To handle that case, call Shutdown in the destructor.

	if( mEVRPresenters.size() > 1 ) {
		SAFE_RELEASE( mEVRPresenters[0] );
		SAFE_RELEASE( mEVRPresenters[1] );
	}

	Shutdown();
	//SAFE_RELEASE(mEVRPresenter);
	SafeRelease( &mSequencerSource );


}

// IUnknown methods

HRESULT CPlayer::QueryInterface( REFIID riid, void** ppv )
{
	static const QITAB qit[] = {
		QITABENT( CPlayer, IMFAsyncCallback ),
		{ 0 }
	};
	return QISearch( this, qit, riid, ppv );
}

ULONG CPlayer::AddRef()
{
	return InterlockedIncrement( &mRefCount );
}

ULONG CPlayer::Release()
{
	ULONG uCount = InterlockedDecrement( &mRefCount );

	if( uCount == 0 ) {
		delete this;
	}

	return uCount;
}

HRESULT CPlayer::OpenMultipleURL( vector<const WCHAR*>& urls )
{
	if( mState == OPEN_PENDING ) { return S_FALSE; }

	IMFTopology* pTopology = NULL;
	IMFPresentationDescriptor* pSourcePD = NULL;

	//Some lolilol for the sequencer that's coming from the outerspace (see topoEdit src code)
	IMFMediaSource* spSrc = NULL;
	IMFPresentationDescriptor* spPD = NULL;
	IMFMediaSourceTopologyProvider* spSrcTopoProvider = NULL;
	HRESULT hr = S_OK;

	if( mPreviousTopoID != 0 ) {
		hr = mSequencerSource->DeleteTopology( mPreviousTopoID ) ;
		mPreviousTopoID = 0;
	}

	SafeRelease( &mSequencerSource );

	if( !mSequencerSource ) {
		hr = MFCreateSequencerSource( NULL, &mSequencerSource );
		CHECK_HR( hr );

		hr = CreateSession();
		CHECK_HR( hr );

		hr = mSequencerSource->QueryInterface( IID_PPV_ARGS( &mSource ) );
		CHECK_HR( hr );
	}

	int nUrl = urls.size();
	int nPresenters = mEVRPresenters.size();

	for( int i = nPresenters; i < nUrl; i ++ ) {
		EVRCustomPresenter* presenter = new EVRCustomPresenter( hr );
		presenter->SetVideoWindow( mHWNDVideo );
		mEVRPresenters.push_back( presenter );
	}

	// Create the media session.
	//SafeRelease(&mSource);

	for( int i = 0; i < nUrl; i++ ) {
		IMFMediaSource* source = NULL;

		const WCHAR* sURL = urls[i];
		// Create the media source.
		hr = CreateMediaSource( sURL, &source );
		CHECK_HR( hr );

		// Other source-code returns here - why?
		//return hr;
		//All the following code will never be reached...

		// Create the presentation descriptor for the media source.
		hr = source->CreatePresentationDescriptor( &pSourcePD );
		CHECK_HR( hr );

		if( i == 0 ) { hr = CreatePlaybackTopology( source, pSourcePD, mHWNDVideo, &pTopology, mEVRPresenters[i] ); }
		else { hr =  AddToPlaybackTopology( source, pSourcePD, mHWNDVideo, pTopology, mEVRPresenters[i] ); }

		CHECK_HR( hr );

		//v_sources.push_back(source);

		/*if (i==0) mSource = source; //keep one source for time tracking
		else */ SafeRelease( &source );
		SetMediaInfo( pSourcePD );

		SafeRelease( &pSourcePD );
	}

	MFSequencerElementId NewID;
	hr = mSequencerSource->AppendTopology( pTopology, SequencerTopologyFlags_Last, &NewID ) ;
	CHECK_HR( hr );
	mPreviousTopoID = NewID;
	hr = mSequencerSource->QueryInterface( IID_IMFMediaSource, ( void** ) &spSrc ) ;
	CHECK_HR( hr );
	hr = spSrc->CreatePresentationDescriptor( &spPD ) ;
	CHECK_HR( hr );
	hr = mSequencerSource->QueryInterface( IID_IMFMediaSourceTopologyProvider, ( void** ) &spSrcTopoProvider ) ;
	CHECK_HR( hr );
	SafeRelease( &pTopology );
	hr = spSrcTopoProvider->GetMediaSourceTopology( spPD, &pTopology ) ;
	CHECK_HR( hr );

	//Now that we're done, we set the topolgy as it should be....
	hr = mSession->SetTopology( 0, pTopology );
	CHECK_HR( hr );

	mState = OPEN_PENDING;
	mCurrentVolume = 1.0f;

	// If SetTopology succeeds, the media session will queue an
	// MESessionTopologySet event.

done:

	if( FAILED( hr ) ) {
		mState = CLOSED;
	}

	SafeRelease( &pSourcePD );
	SafeRelease( &pTopology );
	//SafeRelease(&spPD);
	//SafeRelease(&spSrc);
	//SafeRelease(&spSrcTopoProvider);  //Uncoment this and get a crash in D3D shared texture..
	return hr;
}

//  Open a URL for playback.
HRESULT CPlayer::OpenURL( const WCHAR* sURL, const WCHAR* audioDeviceId )
{
	// 1. Create a new media session.
	// 2. Create the media source.

	// Create the media session.
	HRESULT hr = CreateSession();
	CHECK_HR( hr );

	// Create the media source.
	hr = CreateMediaSource( sURL, &mSource );
	CHECK_HR( hr );

	EndOpenURL( audioDeviceId );

done:

	if( FAILED( hr ) ) {
		mState = CLOSED;

		switch( hr ) {
			case MF_E_SOURCERESOLVER_MUTUALLY_EXCLUSIVE_FLAGS:	// 	The dwFlags parameter contains mutually exclusive flags.
				CI_LOG_E( "MF_E_SOURCERESOLVER_MUTUALLY_EXCLUSIVE_FLAGS" );
				break;

			case MF_E_UNSUPPORTED_SCHEME:						//	The URL scheme is not supported.
				CI_LOG_E( "MF_E_UNSUPPORTED_SCHEME" );
				break;

			case MF_E_UNSUPPORTED_BYTESTREAM_TYPE:
				CI_LOG_E( "MF_E_UNSUPPORTED_BYTESTREAM_TYPE" );
				break;

			default:
				CI_LOG_E( "Unknown eror" );
				break;
				break;
		}
	}

	return hr;
}

HRESULT CPlayer::OpenURLAsync( const WCHAR* sURL )
{
	// 1. Create a new media session.
	// 2. Create the media source.

	// Create the media session.
	HRESULT hr = S_OK;
	hr = CreateSession();
	CHECK_HR( hr );

	// Create the media source.
	hr = BeginCreateMediaSource( sURL, this, &mSourceResolver );
	CHECK_HR( hr );

	/////MADE ASYNCHRONOUS
	mState = OPEN_ASYNC_PENDING;
done:

	if( FAILED( hr ) ) {
		mState = CLOSED;
	}

	return hr;
}

HRESULT CPlayer::EndOpenURL( const WCHAR* audioDeviceId )
{
	HRESULT hr;

	// 3. Create the topology.
	// 4. Queue the topology [asynchronous]
	// 5. Start playback [asynchronous - does not happen in this method.]

	IMFTopology* pTopology = NULL;
	IMFPresentationDescriptor* pSourcePD = NULL;

	// Create the presentation descriptor for the media source.
	hr = mSource->CreatePresentationDescriptor( &pSourcePD );
	CHECK_HR( hr );

	// Create a partial topology.
	hr = CreatePlaybackTopology( mSource, pSourcePD, mHWNDVideo, &pTopology, mEVRPresenter, audioDeviceId );
	CHECK_HR( hr );

	SetMediaInfo( pSourcePD );

	// Set the topology on the media session.
	hr = mSession->SetTopology( 0, pTopology );
	CHECK_HR( hr );

	mState = OPEN_PENDING;
	mCurrentVolume = 1.0f;

done:

	if( FAILED( hr ) ) {
		mState = CLOSED;
	}

	SafeRelease( &pSourcePD );
	SafeRelease( &pTopology );
	return hr;
}

//  Pause playback.
HRESULT CPlayer::Pause()
{
	if( mState != STARTED ) {
		return MF_E_INVALIDREQUEST;
	}

	if( mSession == NULL || mSource == NULL ) {
		return E_UNEXPECTED;
	}

	HRESULT hr = mSession->Pause();

	if( SUCCEEDED( hr ) ) {
		mState = PAUSED;
	}

	return hr;
}

// Stop playback.
HRESULT CPlayer::Stop()
{
	if( mState != STARTED && mState != PAUSED ) {
		return MF_E_INVALIDREQUEST;
	}

	if( mSession == NULL ) {
		return E_UNEXPECTED;
	}

	HRESULT hr = mSession->Stop();

	if( SUCCEEDED( hr ) ) {
		mState = STOPPED;
	}

	return hr;
}

HRESULT CPlayer::setPosition( float pos )
{
	if( mState == OPEN_PENDING ) {
		CI_LOG_E( "Error cannot seek during opening" );
		return S_FALSE;
	}

	//Create variant for seeking information
	PROPVARIANT varStart;
	PlayerState curState = mState;
	PropVariantInit( &varStart );
	varStart.vt = VT_I8;
	varStart.hVal.QuadPart = pos * 10000000.0; //i.e. seeking to pos // should be MFTIME and not float :(

	HRESULT hr = mSession->Start( &GUID_NULL, &varStart );

	if( SUCCEEDED( hr ) ) {
		mState = STARTED; //setting the rate automatically sets it to play

		if( curState == STOPPED ) { hr = Stop(); }

		if( curState == PAUSED ) { Pause(); }
	}
	else {
		CI_LOG_E( "Error while seeking" );
		return S_FALSE;
	}

	PropVariantClear( &varStart );
	return S_OK;
}

HRESULT CPlayer::setVolume( float vol )
{
	//Should we lock here as well ?
	if( mSession == NULL ) {
		CI_LOG_E( "Error session is null" );
		return E_FAIL;
	}

	if( mVolumeControl == NULL ) {
		HRESULT hr = MFGetService( mSession, MR_STREAM_VOLUME_SERVICE, __uuidof( IMFAudioStreamVolume ), ( void** ) &mVolumeControl );
		mCurrentVolume = vol;

		if( FAILED( hr ) ) {
			CI_LOG_W( "Error while getting sound control interface" );
			//ofLogError( "ofxWMFVideoPlayer", "setVolume: Error while getting sound control interface" );
			return E_FAIL;
		}
	}

	UINT32 nChannels;
	mVolumeControl->GetChannelCount( &nChannels );

	for( int i = 0; i < nChannels; i++ ) {
		mVolumeControl->SetChannelVolume( i, vol );
	}

	mCurrentVolume = vol;

	return S_OK;
}

//  Callback for the asynchronous BeginGetEvent method.
HRESULT CPlayer::Invoke( IMFAsyncResult* pResult )
{
	MediaEventType meType = MEUnknown;  // Event type
	IMFMediaEvent* pEvent = NULL;

	HRESULT hr;

	if( !mSession ) {
		CI_LOG_W( "Called with a null session" );
		return -1; //Sometimes Invoke is called but mSession is closed
	}

	// Handle async-loading
	if( mState == OPEN_ASYNC_PENDING ) {
		if( !&mSourceResolver ) {
			CI_LOG_E( "Async request returned with NULL session" );
			//ofLogError( "CPlayer::Invoke" ) << "Async request returned with NULL session";
			return -1;
		}

		MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;
		IUnknown*  	pSourceUnk = NULL;
		//CheckPointer(mSource, E_POINTER);

		hr = mSourceResolver->EndCreateObjectFromURL(
		         pResult,					// Invoke result
		         &ObjectType,                // Receives the created object type.
		         &pSourceUnk                  // Receives a pointer to the media source.
		     );

		// Get the IMFMediaSource interface from the media source.
		if( SUCCEEDED( hr ) ) {
			hr = pSourceUnk->QueryInterface( __uuidof( IMFMediaSource ), ( void** )( &mSource ) );
			mState = OPEN_ASYNC_COMPLETE; // Session finished opening URL
		}

		SafeRelease( &pSourceUnk );
		return hr;
	}

	// Get the event from the event queue.
	hr = mSession->EndGetEvent( pResult, &pEvent );
	CHECK_HR( hr );

	// Get the event type.
	hr = pEvent->GetType( &meType );
	CHECK_HR( hr );

	if( meType == MESessionClosed ) {
		// The session was closed.
		// The application is waiting on the mCloseEvent event handle.
		SetEvent( mCloseEvent );
	}
	else {
		// For all other events, get the next event in the queue.
		hr = mSession->BeginGetEvent( this, NULL );
		CHECK_HR( hr );
	}

	// Check the application state.

	// If a call to IMFMediaSession::Close is pending, it means the
	// application is waiting on the mCloseEvent event and
	// the application's message loop is blocked.

	// Otherwise, post a private window message to the application.

	if( mState != CLOSING ) {
		// Leave a reference count on the event.
		pEvent->AddRef();

		PostMessage( mHWNDEvent, WM_APP_PLAYER_EVENT,
		             ( WPARAM )pEvent, ( LPARAM )meType );
	}

done:
	SafeRelease( &pEvent );
	return S_OK;
}

HRESULT CPlayer::HandleEvent( UINT_PTR pEventPtr )
{
	HRESULT hrStatus = S_OK;
	HRESULT hr = S_OK;
	MediaEventType meType = MEUnknown;

	IMFMediaEvent* pEvent = ( IMFMediaEvent* )pEventPtr;

	if( pEvent == NULL ) {
		return E_POINTER;
	}

	// Get the event type.
	hr = pEvent->GetType( &meType );
	CHECK_HR( hr );

	// Get the event status. If the operation that triggered the event
	// did not succeed, the status is a failure code.
	hr = pEvent->GetStatus( &hrStatus );

	// Check if the async operation succeeded.
	if( SUCCEEDED( hr ) && FAILED( hrStatus ) ) {
		hr = hrStatus;
	}

	CHECK_HR( hr );

	switch( meType ) {
		case MESessionTopologyStatus:
			hr = OnTopologyStatus( pEvent );
			break;

		case MEEndOfPresentation:
			hr = OnPresentationEnded( pEvent );
			//CI_LOG_V("Presentation Ended");
			break;

		case MENewPresentation:
			hr = OnNewPresentation( pEvent );
			CI_LOG_V( "New Presentation" );
			break;

		case MESessionTopologySet:
			IMFTopology* topology;
			GetEventObject<IMFTopology> ( pEvent, &topology );
			WORD nodeCount;
			topology->GetNodeCount( &nodeCount );
			CI_LOG_V( "Topo set and we have " << nodeCount << " nodes" );
			//cout << "Topo set and we have "  << nodeCount << " nodes" << endl;
			SafeRelease( &topology );
			break;

		case MESessionStarted:
			//CI_LOG_V( "Started Session" );
			break;

		case MEBufferingStarted:
			CI_LOG_I( "Buffering..." );
			break;

		case MEBufferingStopped:
			CI_LOG_I( "Finished Buffering..." );
			break;

		default:
			hr = OnSessionEvent( pEvent, meType );
			break;
	}

done:
	SafeRelease( &pEvent );
	return hr;
}

HRESULT CPlayer::GetBufferProgress( DWORD* pProgress )
{
	IPropertyStore* pProp = NULL;
	PROPVARIANT var;

	// Get the property store from the media session.
	HRESULT hr = MFGetService(
	                 mSession,
	                 MFNETSOURCE_STATISTICS_SERVICE,
	                 IID_PPV_ARGS( &pProp )
	             );


	if( SUCCEEDED( hr ) ) {
		PROPERTYKEY key;
		key.fmtid = MFNETSOURCE_STATISTICS;
		key.pid = MFNETSOURCE_BUFFERPROGRESS_ID;

		hr = pProp->GetValue( key, &var );

	}

	if( SUCCEEDED( hr ) ) {
		*pProgress = var.lVal;
		//		cout << "buff prog " << *pProgress << endl;
	}

	PropVariantClear( &var );

	SafeRelease( &pProp );
	return hr;
}

//  Release all resources held by this object.
HRESULT CPlayer::Shutdown()
{
	HRESULT hr = S_OK;

	// Close the session
	hr = CloseSession();

	// Shutdown the Media Foundation platform
	if( mCloseEvent ) {
		CloseHandle( mCloseEvent );
		mCloseEvent = NULL;
	}

	if( mEVRPresenters.size() > 0 ) {
		if( mEVRPresenters[0] ) { mEVRPresenters[0]->releaseSharedTexture(); }

		if( mEVRPresenters[1] ) { mEVRPresenters[1]->releaseSharedTexture(); }

		SafeRelease( &mEVRPresenters[0] );
		SafeRelease( &mEVRPresenters[1] );

	}
	else {
		if( mEVRPresenter ) { mEVRPresenter->releaseSharedTexture(); }

		SafeRelease( &mEVRPresenter );
	}

	return hr;
}

/// Protected methods
HRESULT CPlayer::OnTopologyStatus( IMFMediaEvent* pEvent )
{
	UINT32 status;

	HRESULT hr = pEvent->GetUINT32( MF_EVENT_TOPOLOGY_STATUS, &status );

	if( SUCCEEDED( hr ) && ( status == MF_TOPOSTATUS_READY ) ) {
		SafeRelease( &mVideoDisplay );
		hr = StartPlayback();
		hr = Pause();
	}

	return hr;
}

//  Handler for MEEndOfPresentation event.
HRESULT CPlayer::OnPresentationEnded( IMFMediaEvent* pEvent )
{
	HRESULT hr = S_OK;

	if( mIsLooping ) {
		hr = Stop();
		setPosition( 0.0f );
		hr = Play();
	}
	else {
		// pause
		hr = Pause();
	}

	// emit presentation ended signal
	mPresentationEndedSignal.emit();

	return hr;
}

//  Handler for MENewPresentation event.
//
//  This event is sent if the media source has a new presentation, which
//  requires a new topology.

HRESULT CPlayer::OnNewPresentation( IMFMediaEvent* pEvent )
{
	IMFPresentationDescriptor* pPD = NULL;
	IMFTopology* pTopology = NULL;

	HRESULT hr = S_OK;

	// Get the presentation descriptor from the event.
	hr = GetEventObject( pEvent, &pPD );
	CHECK_HR( hr );

	// Create a partial topology.
	hr = CreatePlaybackTopology( mSource, pPD,  mHWNDVideo, &pTopology, mEVRPresenter );
	CHECK_HR( hr );
	SetMediaInfo( pPD );

	// Set the topology on the media session.
	hr = mSession->SetTopology( 0, pTopology );
	CHECK_HR( hr );

	mState = OPEN_PENDING;

done:
	SafeRelease( &pTopology );
	SafeRelease( &pPD );
	return S_OK;
}

//  Create a new instance of the media session.
HRESULT CPlayer::CreateSession()
{
	// Close the old session, if any.
	HRESULT hr = CloseSession();
	CHECK_HR( hr );

	assert( mState == CLOSED );

	// Create the media session.
	hr = MFCreateMediaSession( NULL, &mSession );
	CHECK_HR( hr );

	// Start pulling events from the media session
	hr = mSession->BeginGetEvent( ( IMFAsyncCallback* )this, NULL );
	CHECK_HR( hr );

	mState = READY;

done:
	return hr;
}

//  Close the media session.
HRESULT CPlayer::CloseSession()
{
	//  The IMFMediaSession::Close method is asynchronous, but the
	//  CPlayer::CloseSession method waits on the MESessionClosed event.
	//
	//  MESessionClosed is guaranteed to be the last event that the
	//  media session fires.

	HRESULT hr = S_OK;

	if( mVideoDisplay != NULL ) { SafeRelease( &mVideoDisplay ); }

	if( mVolumeControl != NULL ) { SafeRelease( &mVolumeControl ); }

	// First close the media session.
	if( mSession ) {
		DWORD dwWaitResult = 0;

		mState = CLOSING;

		hr = mSession->Close();

		// Wait for the close operation to complete
		if( SUCCEEDED( hr ) ) {
			dwWaitResult = WaitForSingleObject( mCloseEvent, 5000 );

			if( dwWaitResult == WAIT_TIMEOUT ) {
				assert( FALSE );
			}

			// Now there will be no more events from this session.
		}
	}

	// Complete shutdown operations.
	if( SUCCEEDED( hr ) ) {
		// Shut down the media source. (Synchronous operation, no events.)
		if( mSource ) {
			( void )mSource->Shutdown();
		}

		// Shut down the media session. (Synchronous operation, no events.)
		if( mSession ) {
			( void )mSession->Shutdown();
		}
	}

	SafeRelease( &mSource );
	SafeRelease( &mSession );
	mState = CLOSED;
	return hr;
}

//  Start playback from the current position.
HRESULT CPlayer::StartPlayback()
{
	assert( mSession != NULL );

	PROPVARIANT varStart;
	PropVariantInit( &varStart );

	HRESULT hr = mSession->Start( &GUID_NULL, &varStart );

	if( SUCCEEDED( hr ) ) {
		// Note: Start is an asynchronous operation. However, we
		// can treat our state as being already started. If Start
		// fails later, we'll get an MESessionStarted event with
		// an error code, and we will update our state then.
		mState = STARTED;
	}

	PropVariantClear( &varStart );

	return hr;
}

//  Start playback from paused or stopped.
HRESULT CPlayer::Play()
{
	if( mState != PAUSED && mState != STOPPED ) {
		return MF_E_INVALIDREQUEST;
	}

	if( mSession == NULL || mSource == NULL ) {
		return E_UNEXPECTED;
	}

	return StartPlayback();
}

// Gaz: Already defined in mfutils.h
//  Create a media source from a URL.
//HRESULT CreateMediaSource(PCWSTR sURL, IMFMediaSource **ppSource)
//{
//    MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;
//
//    IMFSourceResolver* pSourceResolver = NULL;
//    IUnknown* pSource = NULL;
//
//    // Create the source resolver.
//    HRESULT hr = MFCreateSourceResolver(&pSourceResolver);
//    if (FAILED(hr))
//    {
//        goto done;
//    }
//
//    // Use the source resolver to create the media source.
//
//    // Note: For simplicity this sample uses the synchronous method to create
//    // the media source. However, creating a media source can take a noticeable
//    // amount of time, especially for a network source. For a more responsive
//    // UI, use the asynchronous BeginCreateObjectFromURL method.
//
//    hr = pSourceResolver->CreateObjectFromURL(
//        sURL,                       // URL of the source.
//        MF_RESOLUTION_MEDIASOURCE,  // Create a source object.
//        NULL,                       // Optional property store.
//        &ObjectType,        // Receives the created object type.
//        &pSource            // Receives a pointer to the media source.
//        );
//    if (FAILED(hr))
//    {
//        goto done;
//    }
//
//    // Get the IMFMediaSource interface from the media source.
//    hr = pSource->QueryInterface(IID_PPV_ARGS(ppSource));
//
//done:
//    SafeRelease(&pSourceResolver);
//    SafeRelease(&pSource);
//    return hr;
//}

//  Create an activation object for a renderer, based on the stream media type.

HRESULT CreateMediaSinkActivate(
    IMFStreamDescriptor* pSourceSD,     // Pointer to the stream descriptor.
    HWND hVideoWindow,                  // Handle to the video clipping window.
    IMFActivate** ppActivate,
    IMFVideoPresenter* pVideoPresenter,
    IMFMediaSink** ppMediaSink,
    const WCHAR* audioDeviceId = 0 )
{
	IMFMediaTypeHandler* pHandler = NULL;
	IMFActivate* pActivate = NULL;
	IMFMediaSink* pSink = NULL;

	HRESULT hr = S_OK;

	// Get the media type handler for the stream.
	hr = pSourceSD->GetMediaTypeHandler( &pHandler );
	CHECK_HR( hr );

	// Get the major media type.
	GUID guidMajorType;
	hr = pHandler->GetMajorType( &guidMajorType );
	CHECK_HR( hr );

	// Create an IMFActivate object for the renderer, based on the media type.
	if( MFMediaType_Audio == guidMajorType ) {
		// Gaz: Other code is MUCH, MUCH more sparse here - why?
		HRESULT hr = S_OK;

		IMMDeviceEnumerator* pEnum = NULL;      // Audio device enumerator.
		IMMDeviceCollection* pDevices = NULL;   // Audio device collection.
		IMMDevice* pDevice = NULL;              // An audio device.
		IMFMediaSink* pSink = NULL;             // Streaming audio renderer (SAR)
		IPropertyStore* pProps = NULL;

		LPWSTR wstrID = NULL;                   // Device ID.

		// Create the device enumerator.
		hr = CoCreateInstance(
		         __uuidof( MMDeviceEnumerator ),
		         NULL,
		         CLSCTX_ALL,
		         __uuidof( IMMDeviceEnumerator ),
		         ( void** )&pEnum
		     );

		// Enumerate the rendering devices.
		if( SUCCEEDED( hr ) ) {
			hr = pEnum->EnumAudioEndpoints( eRender, DEVICE_STATE_ACTIVE, &pDevices );
		}

		// Get ID of the first device in the list.
		if( SUCCEEDED( hr ) ) {
			UINT pcDevices;
			pDevices->GetCount( &pcDevices );

			// Returns a pointer to an activation object, which can be used to create the SAR.
			if( SUCCEEDED( hr ) ) {
				hr = MFCreateAudioRendererActivate( &pActivate );
			}

			for( UINT i = 0 ; i < pcDevices; i++ ) {
				hr = pDevices->Item( i, &pDevice );

				if( SUCCEEDED( hr ) ) {
					hr = pDevice->GetId( &wstrID );
				}

				if( SUCCEEDED( hr ) ) {
					hr = pDevice->OpenPropertyStore( STGM_READ, &pProps );
				}

				PROPVARIANT varName = {0};

				if( SUCCEEDED( hr ) ) {
					// Initialize container for property value.
					PropVariantInit( &varName );

					// Get the endpoint's friendly-name property.
					hr = pProps->GetValue( PKEY_Device_FriendlyName, &varName );

					if( SUCCEEDED( hr ) ) {
						WCHAR szName[128];

						hr = PropVariantToString( varName, szName, ARRAYSIZE( szName ) );

#if 0
						// list out audio devices
						wstring ws( szName );
						string str( ws.begin(), ws.end() );
						CI_LOG_I( str );
#endif

						if( SUCCEEDED( hr ) || hr == STRSAFE_E_INSUFFICIENT_BUFFER ) {
							if( wcscmp( szName, audioDeviceId ) == 0 ) {
								hr = pDevices->Item( i, &pDevice );

								if( SUCCEEDED( hr ) ) {
									hr = pDevice->GetId( &wstrID );
									PropVariantClear( &varName );

									// Only set endpoint device if a matching name is found, if the Windows default playback device has changed, it won't switch.
									// If name not specified / not mataching, WMF will auto switch endpoint devices during playback.
									if( SUCCEEDED( hr ) ) {
										hr = pActivate->SetString(
										         MF_AUDIO_RENDERER_ATTRIBUTE_ENDPOINT_ID,
										         wstrID
										     );
									}

									break;
								}
							}
						}
					}
				}
			}
		}


		//SAFE_RELEASE(pActivate)

		// Create the audio renderer.
		*ppActivate = pActivate;
		( *ppActivate )->AddRef();

		SAFE_RELEASE( pEnum );
		SAFE_RELEASE( pDevices );
		SAFE_RELEASE( pDevice );
		CoTaskMemFree( wstrID );
	}

	else if( MFMediaType_Video == guidMajorType ) {
		// Create the video renderer.
		// hr = MFCreateVideoRendererActivate(hVideoWindow, &pActivate);
		hr = MFCreateVideoRenderer( __uuidof( IMFMediaSink ), ( void** )&pSink );
		CHECK_HR( hr );

		IMFVideoRenderer*  pVideoRenderer = NULL;
		hr = pSink->QueryInterface( __uuidof( IMFVideoRenderer ), ( void** ) &pVideoRenderer );
		CHECK_HR( hr );

		//ThrowIfFail( pVideoRenderer->InitializeRenderer( NULL, pVideoPresenter ) );
		hr = pVideoRenderer->InitializeRenderer(NULL, pVideoPresenter);
		CHECK_HR( hr );

		*ppMediaSink = pSink;
		( *ppMediaSink )->AddRef();
	}
	else {
		// Unknown stream type.
		hr = E_FAIL;
		// Optionally, you could deselect this stream instead of failing.
	}

	CHECK_HR( hr );	// Gazo: Necessary?

	// Return IMFActivate pointer to caller.

done:
	SafeRelease( &pHandler );
	SafeRelease( &pActivate );
	SafeRelease( &pSink );
	return hr;
}

// Add a source node to a topology.
HRESULT AddSourceNode(
    IMFTopology* pTopology,           // Topology.
    IMFMediaSource* pSource,          // Media source.
    IMFPresentationDescriptor* pPD,   // Presentation descriptor.
    IMFStreamDescriptor* pSD,         // Stream descriptor.
    IMFTopologyNode** ppNode )        // Receives the node pointer.
{
	IMFTopologyNode* pNode = NULL;

	HRESULT hr = S_OK;

	// Create the node.
	hr = MFCreateTopologyNode( MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode );
	CHECK_HR( hr );

	// Set the attributes.
	hr = pNode->SetUnknown( MF_TOPONODE_SOURCE, pSource );
	CHECK_HR( hr );

	hr = pNode->SetUnknown( MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD );
	CHECK_HR( hr );

	hr = pNode->SetUnknown( MF_TOPONODE_STREAM_DESCRIPTOR, pSD );
	CHECK_HR( hr );

	// Add the node to the topology.
	hr = pTopology->AddNode( pNode );
	CHECK_HR( hr );

	// Return the pointer to the caller.
	*ppNode = pNode;
	( *ppNode )->AddRef();

done:
	SafeRelease( &pNode );
	return hr;
}


HRESULT AddOutputNode(
    IMFTopology* pTopology,     // Topology.
    IMFStreamSink* pStreamSink, // Stream sink.
    IMFTopologyNode** ppNode    // Receives the node pointer.
)
{
	IMFTopologyNode* pNode = NULL;
	HRESULT hr = S_OK;

	// Create the node.
	hr = MFCreateTopologyNode( MF_TOPOLOGY_OUTPUT_NODE, &pNode );

	// Set the object pointer.
	if( SUCCEEDED( hr ) ) {
		hr = pNode->SetObject( pStreamSink );
	}

	// Add the node to the topology.
	if( SUCCEEDED( hr ) ) {
		hr = pTopology->AddNode( pNode );
	}

	if( SUCCEEDED( hr ) ) {
		hr = pNode->SetUINT32( MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, TRUE );
	}

	// Return the pointer to the caller.
	if( SUCCEEDED( hr ) ) {
		*ppNode = pNode;
		( *ppNode )->AddRef();
	}

	if( pNode ) {
		pNode->Release();
	}

	return hr;
}

// Add an output node to a topology.
HRESULT AddOutputNode(
    IMFTopology* pTopology,     // Topology.
    IMFActivate* pActivate,     // Media sink activation object.
    DWORD dwId,                 // Identifier of the stream sink.
    IMFTopologyNode** ppNode )  // Receives the node pointer.
{
	IMFTopologyNode* pNode = NULL;

	HRESULT hr = S_OK;

	// Create the node.
	hr = MFCreateTopologyNode( MF_TOPOLOGY_OUTPUT_NODE, &pNode );
	CHECK_HR( hr );

	// Set the object pointer.
	hr = pNode->SetObject( pActivate );
	CHECK_HR( hr );

	// Set the stream sink ID attribute.
	hr = pNode->SetUINT32( MF_TOPONODE_STREAMID, dwId );
	CHECK_HR( hr );

	hr = pNode->SetUINT32( MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE );
	CHECK_HR( hr );

	// Add the node to the topology.
	hr = pTopology->AddNode( pNode );
	CHECK_HR( hr );

	// Return the pointer to the caller.
	*ppNode = pNode;
	( *ppNode )->AddRef();

done:
	SafeRelease( &pNode );
	return hr;
}
//</SnippetPlayer.cpp>

//  Add a topology branch for one stream.
//
//  For each stream, this function does the following:
//
//    1. Creates a source node associated with the stream.
//    2. Creates an output node for the renderer.
//    3. Connects the two nodes.
//
//  The media session will add any decoders that are needed.

HRESULT AddBranchToPartialTopology(
    IMFTopology* pTopology,         // Topology.
    IMFMediaSource* pSource,        // Media source.
    IMFPresentationDescriptor* pPD, // Presentation descriptor.
    DWORD iStream,                  // Stream index.
    HWND hVideoWnd,
    IMFVideoPresenter* pVideoPresenter, // Window for video playback.
    const WCHAR* audioDeviceId = 0 )
{
	IMFStreamDescriptor* pSD = NULL;
	IMFActivate*         pSinkActivate = NULL;
	IMFTopologyNode*     pSourceNode = NULL;
	IMFTopologyNode*     pOutputNode = NULL;
	IMFMediaSink*        pMediaSink = NULL;

	BOOL fSelected = FALSE;

	HRESULT hr = pPD->GetStreamDescriptorByIndex( iStream, &fSelected, &pSD );
	CHECK_HR( hr );

	if( fSelected ) {
		// Create the media sink activation object.
		hr = CreateMediaSinkActivate( pSD, hVideoWnd, &pSinkActivate, pVideoPresenter, &pMediaSink, audioDeviceId );
		CHECK_HR( hr );

		// Add a source node for this stream.
		hr = AddSourceNode( pTopology, pSource, pPD, pSD, &pSourceNode );
		CHECK_HR( hr );

		// Create the output node for the renderer.
		if( pSinkActivate ) {
			hr = AddOutputNode( pTopology, pSinkActivate, 0, &pOutputNode );
		}
		else if( pMediaSink ) {
			IMFStreamSink*   pStreamSink = NULL;
			DWORD streamCount;

			pMediaSink->GetStreamSinkCount( &streamCount ) ;

			pMediaSink->GetStreamSinkByIndex( 0, &pStreamSink );

			hr = AddOutputNode( pTopology, pStreamSink, &pOutputNode );
			CHECK_HR( hr );



			//ThrowIfFail( pNode->SetObject( pStreamSink ) );
		}

		CHECK_HR( hr );	// Necessary?

		// Connect the source node to the output node.
		hr = pSourceNode->ConnectOutput( 0, pOutputNode, 0 );
	}

	// else: If not selected, don't add the branch.

done:
	SafeRelease( &pSD );
	SafeRelease( &pSinkActivate );
	SafeRelease( &pSourceNode );
	SafeRelease( &pOutputNode );

	return hr;
}

//  Create a playback topology from a media source.
HRESULT CreatePlaybackTopology(
    IMFMediaSource* pSource,          // Media source.
    IMFPresentationDescriptor* pPD,   // Presentation descriptor.
    HWND hVideoWnd,                   // Video window.
    IMFTopology** ppTopology,        // Receives a pointer to the topology.
    IMFVideoPresenter* pVideoPresenter,
    const WCHAR* audioDeviceId
)
{
	IMFTopology* pTopology = NULL;
	DWORD cSourceStreams = 0;

	HRESULT hr = S_OK;

	// Create a new topology.
	hr = MFCreateTopology( &pTopology );
	CHECK_HR( hr );

	// Get the number of streams in the media source.
	hr = pPD->GetStreamDescriptorCount( &cSourceStreams );
	CHECK_HR( hr );

	// For each stream, create the topology nodes and add them to the topology.
	for( DWORD i = 0; i < cSourceStreams; i++ ) {
		hr = AddBranchToPartialTopology( pTopology, pSource, pPD, i, hVideoWnd, pVideoPresenter, audioDeviceId );
		CHECK_HR( hr );
	}

	// Return the IMFTopology pointer to the caller.
	*ppTopology = pTopology;
	( *ppTopology )->AddRef();

done:
	SafeRelease( &pTopology );
	return hr;
}

HRESULT AddToPlaybackTopology(
    IMFMediaSource* pSource,          // Media source.
    IMFPresentationDescriptor* pPD,   // Presentation descriptor.
    HWND hVideoWnd,                   // Video window.
    IMFTopology* pTopology,        // Receives a pointer to the topology.
    IMFVideoPresenter* pVideoPresenter )
{
	DWORD cSourceStreams = 0;
	HRESULT hr;

	// Get the number of streams in the media source.
	hr = pPD->GetStreamDescriptorCount( &cSourceStreams );
	CHECK_HR( hr );

	// For each stream, create the topology nodes and add them to the topology.
	for( DWORD i = 1; i < cSourceStreams; i++ ) {
		//ofLogWarning("Ignoring audio stream of video2. If the video is missing check : ofxWMFVideoPlayerUtils");
		hr = AddBranchToPartialTopology( pTopology, pSource, pPD, i, hVideoWnd, pVideoPresenter );
		CHECK_HR( hr );
	}

done:

	return hr;
}

///------------
/// Extra functions
//---------------

float CPlayer::getDuration()
{
	float duration = 0.0;

	if( mSource == NULL ) {
		return 0.0;
	}

	IMFPresentationDescriptor* pDescriptor = NULL;
	HRESULT hr = mSource->CreatePresentationDescriptor( &pDescriptor );

	if( SUCCEEDED( hr ) ) {
		UINT64 longDuration = 0;
		hr = pDescriptor->GetUINT64( MF_PD_DURATION, &longDuration );

		if( SUCCEEDED( hr ) ) {
			duration = ( float )longDuration / 10000000.0;
		}
	}

	SafeRelease( &pDescriptor );
	return duration;
}

float CPlayer::getPosition()
{
	float position = 0.0;

	if( mSession == NULL ) {
		return 0.0;
	}

	IMFPresentationClock* pClock = NULL;
	HRESULT hr = mSession->GetClock( ( IMFClock** )&pClock );

	if( SUCCEEDED( hr ) ) {
		MFTIME longPosition = 0;
		hr = pClock->GetTime( &longPosition );

		if( SUCCEEDED( hr ) ) {
			position = ( float )longPosition / 10000000.0;
		}
	}

	SafeRelease( &pClock );
	return position;
}

float CPlayer::getFrameRate()
{
	float fps = 0.0;

	if( mSource == NULL ) {
		return 0.0;
	}

	IMFPresentationDescriptor* pDescriptor = NULL;
	IMFStreamDescriptor* pStreamHandler = NULL;
	IMFMediaTypeHandler* pMediaType = NULL;
	IMFMediaType*  pType;
	DWORD nStream;

	if FAILED( mSource->CreatePresentationDescriptor( &pDescriptor ) ) { goto done; }

	if FAILED( pDescriptor->GetStreamDescriptorCount( &nStream ) ) { goto done; }

	for( int i = 0; i < nStream; i++ ) {
		BOOL selected;
		GUID type;

		if FAILED( pDescriptor->GetStreamDescriptorByIndex( i, &selected, &pStreamHandler ) ) { goto done; }

		if FAILED( pStreamHandler->GetMediaTypeHandler( &pMediaType ) ) { goto done; }

		if FAILED( pMediaType->GetMajorType( &type ) ) { goto done; }

		if FAILED( pMediaType->GetCurrentMediaType( &pType ) ) { goto done; }

		if( type == MFMediaType_Video ) {
			UINT32 num = 0;
			UINT32 denum = 1;

			MFGetAttributeRatio(
			    pType,
			    MF_MT_FRAME_RATE,
			    &num,
			    &denum
			);

			if( denum != 0 ) {
				fps = ( float ) num / ( float ) denum;
				mNumFrames = denum;
			}
		}

		SafeRelease( &pStreamHandler );
		SafeRelease( &pMediaType );
		SafeRelease( &pType );

		if( fps != 0.0 ) { break; }  // we found the right stream, no point in continuing the loop
	}

done:
	SafeRelease( &pDescriptor );
	SafeRelease( &pStreamHandler );
	SafeRelease( &pMediaType );
	SafeRelease( &pType );
	return fps;
}

int CPlayer::getCurrentFrame()
{
	int frame = 0;

	if( mSource == NULL ) {
		return 0.0;
	}

	IMFPresentationDescriptor* pDescriptor = NULL;
	IMFStreamDescriptor* pStreamHandler = NULL;
	IMFMediaTypeHandler* pMediaType = NULL;
	IMFMediaType*  pType;
	DWORD nStream;

	if FAILED( mSource->CreatePresentationDescriptor( &pDescriptor ) ) { goto done; }

	if FAILED( pDescriptor->GetStreamDescriptorCount( &nStream ) ) { goto done; }

	for( int i = 0; i < nStream; i++ ) {
		BOOL selected;
		GUID type;

		if FAILED( pDescriptor->GetStreamDescriptorByIndex( i, &selected, &pStreamHandler ) ) { goto done; }

		if FAILED( pStreamHandler->GetMediaTypeHandler( &pMediaType ) ) { goto done; }

		if FAILED( pMediaType->GetMajorType( &type ) ) { goto done; }

		if FAILED( pMediaType->GetCurrentMediaType( &pType ) ) { goto done; }

		if( type == MFMediaType_Video ) {
			UINT32 num = 0;
			UINT32 denum = 1;

			MFGetAttributeRatio(
			    pType,
			    MF_MT_FRAME_RATE,
			    &num,
			    &denum
			);

			if( denum != 0 ) {
				frame = num;
				mNumFrames = denum; //update things
			}
		}

		SafeRelease( &pStreamHandler );
		SafeRelease( &pMediaType );
		SafeRelease( &pType );

		if( frame != 0 ) { break; }  // we found the right stream, no point in continuing the loop
	}

done:
	SafeRelease( &pDescriptor );
	SafeRelease( &pStreamHandler );
	SafeRelease( &pMediaType );
	SafeRelease( &pType );
	return frame;
}

HRESULT CPlayer:: SetMediaInfo( IMFPresentationDescriptor* pPD )
{
	mWidth = 0;
	mHeight = 0;
	HRESULT hr = S_OK;
	GUID guidMajorType = GUID_NULL;
	IMFMediaTypeHandler* pHandler = NULL;
	IMFStreamDescriptor* spStreamDesc = NULL;
	IMFMediaType* sourceType = NULL;

	DWORD count;
	pPD->GetStreamDescriptorCount( &count );

	for( DWORD i = 0; i < count; i++ ) {
		BOOL selected;

		hr = pPD->GetStreamDescriptorByIndex( i, &selected, &spStreamDesc );
		CHECK_HR( hr );

		if( selected ) {
			hr = spStreamDesc->GetMediaTypeHandler( &pHandler );
			CHECK_HR( hr );
			hr = pHandler->GetMajorType( &guidMajorType );
			CHECK_HR( hr );

			if( MFMediaType_Video == guidMajorType ) {
				// first get the source video size and allocate a new texture
				hr = pHandler->GetCurrentMediaType( &sourceType ) ;

				UINT32 w, h;
				hr = MFGetAttributeSize( sourceType, MF_MT_FRAME_SIZE, &w, &h );

				if( hr == S_OK ) {
					mWidth = w;
					mHeight = h;
				}

				UINT32 num = 0;
				UINT32 denum = 1;

				MFGetAttributeRatio(
				    sourceType,
				    MF_MT_FRAME_RATE,
				    &num,
				    &denum
				);

				if( denum != 0 ) {
					mNumFrames = denum;
				}

				goto done;
			}

		}
	}

done:
	SafeRelease( &sourceType );
	SafeRelease( &pHandler );
	SafeRelease( &spStreamDesc );
	return hr;
}

///////////////////////////////////////////////////////////////////////
//  Name: SetPlaybackRate
//  Description:
//      Gets the rate control service from Media Session.
//      Sets the playback rate to the specified rate.
//  Parameter:
//      pMediaSession: [in] Media session object to query.
//      rateRequested: [in] Playback rate to set.
//      bThin: [in] Indicates whether to use thinning.
///////////////////////////////////////////////////////////////////////
// taken from MSDN example on rate control

HRESULT  CPlayer::SetPlaybackRate( BOOL bThin, float rateRequested )
{
	HRESULT hr = S_OK;
	IMFRateControl* pRateControl = NULL;

	// Get the rate control object from the Media Session.
	hr = MFGetService(
	         mSession,
	         MF_RATE_CONTROL_SERVICE,
	         IID_IMFRateControl,
	         ( void** ) &pRateControl );

	// Set the playback rate.
	if( SUCCEEDED( hr ) ) {
		hr = pRateControl->SetRate( bThin, rateRequested );
	}

	// Clean up.
	SAFE_RELEASE( pRateControl );

	return hr;
}

float  CPlayer::GetPlaybackRate()
{
	HRESULT hr = S_OK;
	IMFRateControl* pRateControl = NULL;
	BOOL bThin;
	float rate;

	// Get the rate control object from the Media Session.
	hr = MFGetService(
	         mSession,
	         MF_RATE_CONTROL_SERVICE,
	         IID_IMFRateControl,
	         ( void** ) &pRateControl );

	// Set the playback rate.
	if( SUCCEEDED( hr ) ) {
		hr = pRateControl->GetRate( &bThin, &rate );
	}

	// Clean up.
	SAFE_RELEASE( pRateControl );

	if( !FAILED( hr ) ) {
		return rate;
	}
	else {
		cout << "Error: Could Not Get Rate" << endl;
		return NULL;
	}
}



