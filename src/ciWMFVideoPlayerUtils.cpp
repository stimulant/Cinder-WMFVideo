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
#include "atlcomcli.h"

#include "cinder/Log.h"

using namespace std;

const std::string g_PlayerStateString[] = { "Closed", "Ready", "OpenAsyncPending", "OpenAsyncComplete", "OpenPending", "Started", "Paused", "Stopped", "Closing" };
const std::string& GetPlayerStateString(const PlayerState p) { return g_PlayerStateString[p]; };

template <class Q>
HRESULT GetEventObject(IMFMediaEvent *pEvent, Q **ppObject)
{
    *ppObject = NULL;   // zero output

    PROPVARIANT var;
    HRESULT hr = pEvent->GetValue(&var);
    if (SUCCEEDED(hr))
    {
        if (var.vt == VT_UNKNOWN)
        {
            hr = var.punkVal->QueryInterface(ppObject);
        }
        else
        {
            hr = MF_E_INVALIDTYPE;
        }
        PropVariantClear(&var);
    }
    return hr;
}

//HRESULT CreateMediaSource(PCWSTR pszURL, IMFMediaSource **ppSource);

HRESULT CreatePlaybackTopology(IMFMediaSource *pSource, 
    IMFPresentationDescriptor *pPD, HWND hVideoWnd,IMFTopology **ppTopology,IMFVideoPresenter *pVideoPresenter, const WCHAR *audioDeviceId = 0);

HRESULT AddToPlaybackTopology(IMFMediaSource *pSource, 
							   IMFPresentationDescriptor *pPD, HWND hVideoWnd,IMFTopology *pTopology,IMFVideoPresenter *pVideoPresenter);

//  Static class method to create the CPlayer object.

HRESULT CPlayer::CreateInstance(
    HWND hVideo,                  // Video window.
    HWND hEvent,                  // Window to receive notifications.
    CPlayer **ppPlayer)           // Receives a pointer to the CPlayer object.
{
    if (ppPlayer == NULL)
    {
        return E_POINTER;
    }

    CPlayer *pPlayer = new (std::nothrow) CPlayer(hVideo, hEvent);
    if (pPlayer == NULL)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = pPlayer->Initialize();
    if (SUCCEEDED(hr))
    {
        *ppPlayer = pPlayer;
    }
    else
    {
        pPlayer->Release();
    }
    return hr;
}

HRESULT CPlayer::Initialize()
{
    
   HRESULT hr = 0;

        m_hCloseEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (m_hCloseEvent == NULL)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }

		if (!m_pEVRPresenter)  { 
			m_pEVRPresenter = new EVRCustomPresenter(hr);
			m_pEVRPresenter->SetVideoWindow(m_hwndVideo);
		}
    
    return hr;
}

CPlayer::CPlayer(HWND hVideo, HWND hEvent) : 
    m_pSession(NULL),
    m_pSource(NULL),
	m_pSourceResolver( NULL ),
    m_pVideoDisplay(NULL),
    m_hwndVideo(hVideo),
    m_hwndEvent(hEvent),
    m_state(Closed),
    m_hCloseEvent(NULL),
    m_nRefCount(1),
	m_pEVRPresenter(NULL),
	m_pSequencerSource(NULL),
	m_pVolumeControl( NULL ),
	_previousTopoID(0),
	_isLooping(false)
{

}

CPlayer::~CPlayer()
{
    assert(m_pSession == NULL);  
    // If FALSE, the app did not call Shutdown().

    // When CPlayer calls IMediaEventGenerator::BeginGetEvent on the
    // media session, it causes the media session to hold a reference 
    // count on the CPlayer. 
    
    // This creates a circular reference count between CPlayer and the 
    // media session. Calling Shutdown breaks the circular reference 
    // count.

    // If CreateInstance fails, the application will not call 
    // Shutdown. To handle that case, call Shutdown in the destructor. 


	if (v_EVRPresenters.size() >1) {
		SAFE_RELEASE(v_EVRPresenters[0]);
		SAFE_RELEASE(v_EVRPresenters[1]);
	}

    Shutdown();
	//SAFE_RELEASE(m_pEVRPresenter);
	SafeRelease(&m_pSequencerSource);


}

// IUnknown methods

HRESULT CPlayer::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] = 
    {
        QITABENT(CPlayer, IMFAsyncCallback),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

ULONG CPlayer::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

ULONG CPlayer::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0)
    {
        delete this;
    }
    return uCount;
}

HRESULT CPlayer::OpenMultipleURL(vector<const WCHAR *> &urls)
{
	if (m_state == OpenPending) return S_FALSE;
	IMFTopology *pTopology = NULL;
	IMFPresentationDescriptor* pSourcePD = NULL;

	//Some lolilol for the sequencer that's coming from the outerspace (see topoEdit src code)
	IMFMediaSource* spSrc = NULL;
	IMFPresentationDescriptor* spPD = NULL;
	IMFMediaSourceTopologyProvider* spSrcTopoProvider = NULL;
	HRESULT hr = S_OK;

	if(_previousTopoID != 0)
	{
		hr = m_pSequencerSource->DeleteTopology(_previousTopoID) ;
		_previousTopoID = 0;
	}

	SafeRelease(&m_pSequencerSource);

	if (!m_pSequencerSource) 	
	{
		hr = MFCreateSequencerSource(NULL,&m_pSequencerSource);
		CHECK_HR( hr );

		hr = CreateSession();
		CHECK_HR( hr );

		hr = m_pSequencerSource->QueryInterface(IID_PPV_ARGS(&m_pSource));
		CHECK_HR( hr );
	}

	int nUrl = urls.size();
	int nPresenters = v_EVRPresenters.size();

	for (int i=nPresenters; i < nUrl; i ++ )
	{
		EVRCustomPresenter* presenter = new EVRCustomPresenter(hr);
		presenter->SetVideoWindow(m_hwndVideo);
		v_EVRPresenters.push_back(presenter);
	}

	// Create the media session.
	//SafeRelease(&m_pSource);

	for (int i=0; i < nUrl; i++)
	{
		IMFMediaSource* source = NULL;

		const WCHAR* sURL = urls[i];
		// Create the media source.
		hr = CreateMediaSource(sURL, &source);
		CHECK_HR( hr );

		// Other source-code returns here - why?
		//return hr;
		//All the following code will never be reached...

		// Create the presentation descriptor for the media source.
		hr = source->CreatePresentationDescriptor(&pSourcePD);
		CHECK_HR( hr );

		if (i==0)  	hr = CreatePlaybackTopology(source, pSourcePD, m_hwndVideo, &pTopology,v_EVRPresenters[i]);
		else hr =  AddToPlaybackTopology(source, pSourcePD, m_hwndVideo, pTopology,v_EVRPresenters[i]);
		CHECK_HR( hr );

		//v_sources.push_back(source);

		/*if (i==0) m_pSource = source; //keep one source for time tracking
		else */ SafeRelease(&source);
		SetMediaInfo(pSourcePD);

		SafeRelease(&pSourcePD);
	}

	MFSequencerElementId NewID;
	hr = m_pSequencerSource->AppendTopology(pTopology, SequencerTopologyFlags_Last, &NewID) ;
	CHECK_HR( hr );
	_previousTopoID = NewID;
	hr = m_pSequencerSource->QueryInterface(IID_IMFMediaSource, (void**) &spSrc) ;
	CHECK_HR( hr );
	hr = spSrc->CreatePresentationDescriptor(&spPD) ;
	CHECK_HR( hr );
	hr = m_pSequencerSource->QueryInterface(IID_IMFMediaSourceTopologyProvider, (void**) &spSrcTopoProvider) ;
	CHECK_HR( hr );
	SafeRelease(&pTopology);
	hr = spSrcTopoProvider->GetMediaSourceTopology(spPD, &pTopology) ;
	CHECK_HR( hr );

	//Now that we're done, we set the topolgy as it should be....
	hr = m_pSession->SetTopology(0, pTopology);
	CHECK_HR( hr );

	m_state = OpenPending;
	_currentVolume = 1.0f;

	// If SetTopology succeeds, the media session will queue an 
	// MESessionTopologySet event.

done:
	if (FAILED(hr))
	{
		m_state = Closed;
	}
	SafeRelease(&pSourcePD);
	SafeRelease(&pTopology);
	//SafeRelease(&spPD);
	//SafeRelease(&spSrc);
	//SafeRelease(&spSrcTopoProvider);  //Uncoment this and get a crash in D3D shared texture..
	return hr;
}

//  Open a URL for playback.
HRESULT CPlayer::OpenURL( const WCHAR *sURL, const WCHAR *audioDeviceId )
{
	// 1. Create a new media session.
	// 2. Create the media source.

	// Create the media session.
	HRESULT hr = CreateSession();
	CHECK_HR( hr );

	// Create the media source.
	hr = CreateMediaSource( sURL, &m_pSource );
	CHECK_HR( hr );

	EndOpenURL( audioDeviceId );

done:
	if (FAILED(hr))
	{
		m_state = Closed;

		switch (hr)
		{
		case MF_E_SOURCERESOLVER_MUTUALLY_EXCLUSIVE_FLAGS:	// 	The dwFlags parameter contains mutually exclusive flags.
			CI_LOG_E("MF_E_SOURCERESOLVER_MUTUALLY_EXCLUSIVE_FLAGS"); break;
		case MF_E_UNSUPPORTED_SCHEME:						//	The URL scheme is not supported.
			CI_LOG_E("MF_E_UNSUPPORTED_SCHEME"); break;
		case MF_E_UNSUPPORTED_BYTESTREAM_TYPE:
			CI_LOG_E("MF_E_UNSUPPORTED_BYTESTREAM_TYPE"); break;
		default:
			CI_LOG_E("Unknown eror"); break;
		break;
		}
	}

	return hr;
}

HRESULT CPlayer::OpenURLAsync( const WCHAR *sURL )
{
	// 1. Create a new media session.
	// 2. Create the media source.

	// Create the media session.
	HRESULT hr = S_OK;
	hr = CreateSession();
	CHECK_HR( hr );

	// Create the media source.
	hr = BeginCreateMediaSource( sURL, this, &m_pSourceResolver );
	CHECK_HR( hr );

	/////MADE ASYNCHRONOUS
	m_state = OpenAsyncPending;
done:
	if ( FAILED( hr ) )
		m_state = Closed;
	return hr;
}

HRESULT CPlayer::EndOpenURL( const WCHAR *audioDeviceId )
{
	HRESULT hr;

	// 3. Create the topology.
	// 4. Queue the topology [asynchronous]
	// 5. Start playback [asynchronous - does not happen in this method.]

	IMFTopology *pTopology = NULL;
	IMFPresentationDescriptor* pSourcePD = NULL;

    // Create the presentation descriptor for the media source.
    hr = m_pSource->CreatePresentationDescriptor(&pSourcePD);
	CHECK_HR( hr );

    // Create a partial topology.
    hr = CreatePlaybackTopology(m_pSource, pSourcePD, m_hwndVideo, &pTopology, m_pEVRPresenter, audioDeviceId);
	CHECK_HR( hr );

	SetMediaInfo(pSourcePD);

    // Set the topology on the media session.
    hr = m_pSession->SetTopology(0, pTopology);
	CHECK_HR( hr );

    m_state = OpenPending;
	_currentVolume = 1.0f;

    // If SetTopology succeeds, the media session will queue an 
    // MESessionTopologySet event.
	isDone = false;

done:
    if (FAILED(hr))
    {
        m_state = Closed;
    }

    SafeRelease(&pSourcePD);
    SafeRelease(&pTopology);
    return hr;
}

//  Pause playback.
HRESULT CPlayer::Pause()    
{
    if (m_state != Started)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pSession == NULL || m_pSource == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = m_pSession->Pause();
    if (SUCCEEDED(hr))
    {
        m_state = Paused;
    }

    return hr;
}

// Stop playback.
HRESULT CPlayer::Stop()
{
    if (m_state != Started && m_state != Paused)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pSession == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = m_pSession->Stop();
    if (SUCCEEDED(hr))
    {
        m_state = Stopped;
    }
    return hr;
}

HRESULT CPlayer::setPosition(float pos)
{
	if (m_state == OpenPending)
	{
		CI_LOG_E("Error cannot seek during opening");
		return S_FALSE;
	}

/*	bool wasPlaying = (m_state == Started);
	m_pSession->Pause();
	m_state = Paused;*/

	//Create variant for seeking information
	PROPVARIANT varStart;
	PlayerState curState = m_state;
	PropVariantInit(&varStart);
	varStart.vt = VT_I8;
	varStart.hVal.QuadPart = pos* 10000000.0; //i.e. seeking to pos // should be MFTIME and not float :(

	HRESULT hr = m_pSession->Start(&GUID_NULL,&varStart);

	if (SUCCEEDED(hr))
	{
		m_state = Started; //setting the rate automatically sets it to play
		if ( curState == Stopped ) hr = Stop();
		if ( curState == Paused ) Pause();
	}
	else 
	{
		CI_LOG_E("Error while seeking");
		return S_FALSE;
	}

	/*if (!wasPlaying) m_pSession->Pause();
	m_state = Paused;*/

	PropVariantClear(&varStart);
	return S_OK;
}

HRESULT CPlayer::setVolume( float vol )
{
	//Should we lock here as well ?
	if ( m_pSession == NULL )
	{
		CI_LOG_E( "Error session is null" );
		return E_FAIL;
	}
	if ( m_pVolumeControl == NULL )
	{

		HRESULT hr = MFGetService( m_pSession, MR_STREAM_VOLUME_SERVICE, __uuidof( IMFAudioStreamVolume ), (void**) &m_pVolumeControl );
		_currentVolume = vol;
		if ( FAILED( hr ) )
		{
			CI_LOG_E( "Error while getting sound control interface" );
			//ofLogError( "ofxWMFVideoPlayer", "setVolume: Error while getting sound control interface" );
			return E_FAIL;
		}

	}
	UINT32 nChannels;
	m_pVolumeControl->GetChannelCount( &nChannels );
	for ( int i = 0; i < nChannels; i++ )
	{
		m_pVolumeControl->SetChannelVolume( i, vol );
	}
	_currentVolume = vol;

	return S_OK;
}

//  Callback for the asynchronous BeginGetEvent method.
HRESULT CPlayer::Invoke(IMFAsyncResult *pResult)
{
    MediaEventType meType = MEUnknown;  // Event type
    IMFMediaEvent *pEvent = NULL;

	HRESULT hr;
	if ( !m_pSession ) {
		CI_LOG_W("Called with a null session");
		return -1; //Sometimes Invoke is called but m_pSession is closed
	}

	// Handle async-loading
	if ( m_state == OpenAsyncPending ) {
		if ( !&m_pSourceResolver ) {
			CI_LOG_E("Async request returned with NULL session");
			//ofLogError( "CPlayer::Invoke" ) << "Async request returned with NULL session";
			return -1;
		}
		MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;
		IUnknown	  *pSourceUnk = NULL;
		//CheckPointer(m_pSource, E_POINTER);

		hr = m_pSourceResolver->EndCreateObjectFromURL(
			pResult,					// Invoke result
			&ObjectType,                // Receives the created object type. 
			&pSourceUnk                  // Receives a pointer to the media source.
			);

		// Get the IMFMediaSource interface from the media source.
		if ( SUCCEEDED( hr ) )
		{
			hr = pSourceUnk->QueryInterface( __uuidof( IMFMediaSource ), (void**) ( &m_pSource ) );
			m_state = OpenAsyncComplete; // Session finished opening URL
		}
		SafeRelease( &pSourceUnk );
		return hr;
	}


    // Get the event from the event queue.
    hr = m_pSession->EndGetEvent(pResult, &pEvent);
	CHECK_HR( hr );

    // Get the event type. 
    hr = pEvent->GetType(&meType);
	CHECK_HR( hr );

    if (meType == MESessionClosed)
    {
        // The session was closed. 
        // The application is waiting on the m_hCloseEvent event handle. 
        SetEvent(m_hCloseEvent);
    }
    else
    {
        // For all other events, get the next event in the queue.
        hr = m_pSession->BeginGetEvent(this, NULL);
		CHECK_HR( hr );
    }

    // Check the application state. 
        
    // If a call to IMFMediaSession::Close is pending, it means the 
    // application is waiting on the m_hCloseEvent event and
    // the application's message loop is blocked. 

    // Otherwise, post a private window message to the application. 

    if (m_state != Closing)
    {
        // Leave a reference count on the event.
        pEvent->AddRef();

        PostMessage(m_hwndEvent, WM_APP_PLAYER_EVENT, 
            (WPARAM)pEvent, (LPARAM)meType);
    }

done:
    SafeRelease(&pEvent);
    return S_OK;
}

HRESULT CPlayer::HandleEvent(UINT_PTR pEventPtr)
{
    HRESULT hrStatus = S_OK;
	HRESULT hr = S_OK;
    MediaEventType meType = MEUnknown;  

    IMFMediaEvent *pEvent = (IMFMediaEvent*)pEventPtr;

    if (pEvent == NULL)
    {
        return E_POINTER;
    }

    // Get the event type.
    hr = pEvent->GetType(&meType);
	CHECK_HR( hr );

    // Get the event status. If the operation that triggered the event 
    // did not succeed, the status is a failure code.
    hr = pEvent->GetStatus(&hrStatus);

    // Check if the async operation succeeded.
    if (SUCCEEDED(hr) && FAILED(hrStatus)) 
    {
        hr = hrStatus;
    }
	CHECK_HR( hr );

    switch(meType)
    {
    case MESessionTopologyStatus:
        hr = OnTopologyStatus(pEvent);
        break;

    case MEEndOfPresentation:
        hr = OnPresentationEnded(pEvent);
		CI_LOG_V("Presentation Ended");
        break;

    case MENewPresentation:
        hr = OnNewPresentation(pEvent);
		CI_LOG_V( "New Presentation" );
        break;

	case MESessionTopologySet:
		IMFTopology * topology;
		GetEventObject<IMFTopology> (pEvent,&topology);
		WORD nodeCount;
		topology->GetNodeCount(&nodeCount);
		CI_LOG_V( "Topo set and we have " << nodeCount << " nodes" );
		//cout << "Topo set and we have "  << nodeCount << " nodes" << endl;
		SafeRelease(&topology);
		break;

	case MESessionStarted:
		CI_LOG_V( "Started Session" );
		break;
	case MEBufferingStarted:
		CI_LOG_I( "Buffering..." );
		break;
	case MEBufferingStopped:
		CI_LOG_I( "Finished Buffering..." );
		break;

    default:
        hr = OnSessionEvent(pEvent, meType);
        break;
    }

done:
    SafeRelease(&pEvent);
    return hr;
}

HRESULT CPlayer::GetBufferProgress( DWORD *pProgress )
{
	IPropertyStore *pProp = NULL;
	PROPVARIANT var;

	// Get the property store from the media session.
	HRESULT hr = MFGetService(
		m_pSession,
		MFNETSOURCE_STATISTICS_SERVICE,
		IID_PPV_ARGS( &pProp )
		);


	if ( SUCCEEDED( hr ) )
	{
		PROPERTYKEY key;
		key.fmtid = MFNETSOURCE_STATISTICS;
		key.pid = MFNETSOURCE_BUFFERPROGRESS_ID;

		hr = pProp->GetValue( key, &var );

	}

	if ( SUCCEEDED( hr ) )
	{
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
    if (m_hCloseEvent)
    {
        CloseHandle(m_hCloseEvent);
        m_hCloseEvent = NULL;
    }

	if (v_EVRPresenters.size() > 0)
	{
		if (v_EVRPresenters[0]) v_EVRPresenters[0]->releaseSharedTexture();
		if (v_EVRPresenters[1]) v_EVRPresenters[1]->releaseSharedTexture();

		SafeRelease(&v_EVRPresenters[0]);
		SafeRelease(&v_EVRPresenters[1]);

	}
	else
	{
		if (m_pEVRPresenter) { m_pEVRPresenter->releaseSharedTexture(); }
		SafeRelease(&m_pEVRPresenter);
	}

    return hr;
}

/// Protected methods
HRESULT CPlayer::OnTopologyStatus(IMFMediaEvent *pEvent)
{
    UINT32 status; 

    HRESULT hr = pEvent->GetUINT32(MF_EVENT_TOPOLOGY_STATUS, &status);
    if (SUCCEEDED(hr) && (status == MF_TOPOSTATUS_READY))
    {
        SafeRelease(&m_pVideoDisplay);
        hr = StartPlayback();
		hr = Pause();
    }
    return hr;
}

//  Handler for MEEndOfPresentation event.
HRESULT CPlayer::OnPresentationEnded(IMFMediaEvent *pEvent)
{
	HRESULT hr = S_OK;

	// New

	if ( _isLooping ) {

		m_state = Started;

		//Create variant for seeking information
		PROPVARIANT varStart;
		PropVariantInit( &varStart );


		varStart.vt = VT_I8;
		float rate = GetPlaybackRate();
		if ( rate > 0 )
			varStart.hVal.QuadPart = 0; //seeking to the beginning 
		//else
		//	varStart.hVal.QuadPart = getDuration()*.95 * 10000000.0; //seeking to the end 

		hr = m_pSession->Start( &GUID_NULL, &varStart );
		if FAILED( hr )
		{
			CI_LOG_E( "Error while looping" );
			//ofLogError( "ofxWMFVideoPlayerUtils", "Error while looping" );
		}
		PropVariantClear( &varStart );
	}
	else {
		// The session puts itself into the stopped state automatically.
		m_state = Stopped;
		isDone = true;
	}

	// Old

	//m_pSession->Pause();
	//m_state = Paused;

	////Create variant for seeking information
	//PROPVARIANT varStart;
	//PropVariantInit(&varStart);
	//varStart.vt = VT_I8;
	//varStart.hVal.QuadPart = 0; //i.e. seeking to the beginning
	//	
	////HRESULT hr = S_OK;
	////hr = m_pSession->Start(&GUID_NULL,&varStart);

	////if FAILED(hr)
	//{
	//	//ofLogError("ofxWMFVideoPlayerUtils", "Error while looping");
	//}
	//if (!_isLooping) m_pSession->Pause();
	//else m_state = Started;
	//	
	//PropVariantClear(&varStart);
	//
 //   // The session puts itself into the stopped state automatically.
	//// else m_state = Stopped;
    return S_OK;
}

//  Handler for MENewPresentation event.
//
//  This event is sent if the media source has a new presentation, which 
//  requires a new topology. 

HRESULT CPlayer::OnNewPresentation(IMFMediaEvent *pEvent)
{
    IMFPresentationDescriptor *pPD = NULL;
    IMFTopology *pTopology = NULL;

	HRESULT hr = S_OK;

    // Get the presentation descriptor from the event.
    hr = GetEventObject(pEvent, &pPD);
	CHECK_HR( hr );

    // Create a partial topology.
    hr = CreatePlaybackTopology(m_pSource, pPD,  m_hwndVideo,&pTopology,m_pEVRPresenter);
	CHECK_HR( hr );
	SetMediaInfo(pPD);

    // Set the topology on the media session.
    hr = m_pSession->SetTopology(0, pTopology);
	CHECK_HR( hr );

    m_state = OpenPending;

done:
    SafeRelease(&pTopology);
    SafeRelease(&pPD);
    return S_OK;
}

//  Create a new instance of the media session.
HRESULT CPlayer::CreateSession()
{
    // Close the old session, if any.
    HRESULT hr = CloseSession();
	CHECK_HR( hr );

    assert(m_state == Closed);

    // Create the media session.
    hr = MFCreateMediaSession(NULL, &m_pSession);
	CHECK_HR( hr );

    // Start pulling events from the media session
    hr = m_pSession->BeginGetEvent((IMFAsyncCallback*)this, NULL);
	CHECK_HR( hr );

    m_state = Ready;

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

    if (m_pVideoDisplay != NULL ) SafeRelease(&m_pVideoDisplay);
	if ( m_pVolumeControl != NULL ) SafeRelease( &m_pVolumeControl );

    // First close the media session.
    if (m_pSession)
    {
        DWORD dwWaitResult = 0;

        m_state = Closing;
           
        hr = m_pSession->Close();
        // Wait for the close operation to complete
        if (SUCCEEDED(hr))
        {
            dwWaitResult = WaitForSingleObject(m_hCloseEvent, 5000);
            if (dwWaitResult == WAIT_TIMEOUT)
            {
                assert(FALSE);
            }
            // Now there will be no more events from this session.
        }
    }

    // Complete shutdown operations.
    if (SUCCEEDED(hr))
    {
        // Shut down the media source. (Synchronous operation, no events.)
        if (m_pSource)
        {
            (void)m_pSource->Shutdown();
        }
        // Shut down the media session. (Synchronous operation, no events.)
        if (m_pSession)
        {
            (void)m_pSession->Shutdown();
        }
    }

    SafeRelease(&m_pSource);
    SafeRelease(&m_pSession);
    m_state = Closed;
    return hr;
}

//  Start playback from the current position. 
HRESULT CPlayer::StartPlayback()
{
    assert(m_pSession != NULL);

    PROPVARIANT varStart;
    PropVariantInit(&varStart);

    HRESULT hr = m_pSession->Start(&GUID_NULL, &varStart);
    if (SUCCEEDED(hr))
    {
        // Note: Start is an asynchronous operation. However, we
        // can treat our state as being already started. If Start
        // fails later, we'll get an MESessionStarted event with
        // an error code, and we will update our state then.
        m_state = Started;
    }
	
    PropVariantClear(&varStart);
	isDone = false;

    return hr;
}

//  Start playback from paused or stopped.
HRESULT CPlayer::Play()
{
    if (m_state != Paused && m_state != Stopped)
    {
        return MF_E_INVALIDREQUEST;
    }
    if (m_pSession == NULL || m_pSource == NULL)
    {
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
    IMFStreamDescriptor *pSourceSD,     // Pointer to the stream descriptor.
    HWND hVideoWindow,                  // Handle to the video clipping window.
    IMFActivate **ppActivate,
	IMFVideoPresenter *pVideoPresenter,
	IMFMediaSink **ppMediaSink,
	const WCHAR *audioDeviceId = 0)
{
    IMFMediaTypeHandler *pHandler = NULL;
    IMFActivate *pActivate = NULL;
	IMFMediaSink *pSink = NULL;

	HRESULT hr = S_OK;

    // Get the media type handler for the stream.
    hr = pSourceSD->GetMediaTypeHandler(&pHandler);
	CHECK_HR( hr );

    // Get the major media type.
    GUID guidMajorType;
    hr = pHandler->GetMajorType(&guidMajorType);
	CHECK_HR( hr );
 
    // Create an IMFActivate object for the renderer, based on the media type.
    if (MFMediaType_Audio == guidMajorType)
    {
		// Gaz: Other code is MUCH, MUCH more sparse here - why?
		HRESULT hr = S_OK;

		IMMDeviceEnumerator *pEnum = NULL;      // Audio device enumerator.
		IMMDeviceCollection *pDevices = NULL;   // Audio device collection.
		IMMDevice *pDevice = NULL;              // An audio device.		
		IMFMediaSink *pSink = NULL;             // Streaming audio renderer (SAR)
		IPropertyStore *pProps = NULL;

		LPWSTR wstrID = NULL;                   // Device ID.

		// Create the device enumerator.
		hr = CoCreateInstance(
			__uuidof(MMDeviceEnumerator), 
			NULL,
			CLSCTX_ALL, 
			__uuidof(IMMDeviceEnumerator), 
			(void**)&pEnum
			);

		// Enumerate the rendering devices.
		if (SUCCEEDED(hr))
		{
			hr = pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pDevices);
		}

		// Get ID of the first device in the list.
		if (SUCCEEDED(hr))
		{
			UINT pcDevices;
			pDevices->GetCount( &pcDevices );			

			for(UINT i = 0 ; i < pcDevices; i++){
				hr = pDevices->Item(i, &pDevice);

				if (SUCCEEDED(hr))
				{
					hr = pDevice->GetId(&wstrID);												
				}

				if(SUCCEEDED(hr))
				{
					hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
				}				

				PROPVARIANT varName = {0};
				if(SUCCEEDED(hr))
				{				
					// Initialize container for property value.
					PropVariantInit(&varName);

					// Get the endpoint's friendly-name property.
					hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);		

					if(SUCCEEDED(hr))
					{
						WCHAR szName[128];
								
						hr = PropVariantToString( varName, szName, ARRAYSIZE(szName));
						if (SUCCEEDED(hr) || hr == STRSAFE_E_INSUFFICIENT_BUFFER)
						{
							if(wcscmp ( szName, audioDeviceId) == 0 ) {
								hr = pDevices->Item(i, &pDevice);
								if (SUCCEEDED(hr))
								{
									hr = pDevice->GetId(&wstrID);	
									PropVariantClear(&varName);
									break;
								}
							}	
						}					
					}
				}
 			}
		}

		if (SUCCEEDED(hr))
		{
			hr = MFCreateAudioRendererActivate(&pActivate);    
		}		
		
		if (SUCCEEDED(hr))
		{
			hr = pActivate->SetString(
				MF_AUDIO_RENDERER_ATTRIBUTE_ENDPOINT_ID, 
				wstrID
				);
		}

		//SAFE_RELEASE(pActivate)

        // Create the audio renderer.
		*ppActivate = pActivate;
		(*ppActivate)->AddRef();

		SAFE_RELEASE(pEnum);
		SAFE_RELEASE(pDevices);
		SAFE_RELEASE(pDevice); 		
		CoTaskMemFree(wstrID);
    }

    else if (MFMediaType_Video == guidMajorType)
    {
        // Create the video renderer.
       // hr = MFCreateVideoRendererActivate(hVideoWindow, &pActivate);
		hr = MFCreateVideoRenderer( __uuidof(IMFMediaSink), (void**)&pSink);
		CHECK_HR( hr );

		IMFVideoRenderer*  pVideoRenderer=NULL;
		hr = pSink->QueryInterface(__uuidof(IMFVideoRenderer),(void**) &pVideoRenderer);
		CHECK_HR( hr );

		//ThrowIfFail( pVideoRenderer->InitializeRenderer( NULL, pVideoPresenter ) );
		hr = pVideoRenderer->InitializeRenderer( NULL, pVideoPresenter ) ;
		CHECK_HR( hr );

		*ppMediaSink = pSink;
		(*ppMediaSink)->AddRef();
    }
    else
    {
        // Unknown stream type. 
        hr = E_FAIL;
        // Optionally, you could deselect this stream instead of failing.
    }
	CHECK_HR( hr );	// Gazo: Necessary?
 
    // Return IMFActivate pointer to caller.

done:
    SafeRelease(&pHandler);
    SafeRelease(&pActivate);
	SafeRelease(&pSink);
    return hr;
}

// Add a source node to a topology.
HRESULT AddSourceNode(
    IMFTopology *pTopology,           // Topology.
    IMFMediaSource *pSource,          // Media source.
    IMFPresentationDescriptor *pPD,   // Presentation descriptor.
    IMFStreamDescriptor *pSD,         // Stream descriptor.
    IMFTopologyNode **ppNode)         // Receives the node pointer.
{
    IMFTopologyNode *pNode = NULL;

	HRESULT hr = S_OK;

    // Create the node.
    hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pNode);
	CHECK_HR( hr );

    // Set the attributes.
    hr = pNode->SetUnknown(MF_TOPONODE_SOURCE, pSource);
	CHECK_HR( hr );

    hr = pNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD);
	CHECK_HR( hr );

    hr = pNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pSD);
	CHECK_HR( hr );
    
    // Add the node to the topology.
    hr = pTopology->AddNode(pNode);
	CHECK_HR( hr );

    // Return the pointer to the caller.
    *ppNode = pNode;
    (*ppNode)->AddRef();

done:
    SafeRelease(&pNode);
    return hr;
}


HRESULT AddOutputNode(
	IMFTopology *pTopology,     // Topology.
	IMFStreamSink *pStreamSink, // Stream sink.
	IMFTopologyNode **ppNode    // Receives the node pointer.
	)
{
	IMFTopologyNode *pNode = NULL;
	HRESULT hr = S_OK;

	// Create the node.
	hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);

	// Set the object pointer.
	if (SUCCEEDED(hr))
	{
		hr = pNode->SetObject(pStreamSink);
	}

	// Add the node to the topology.
	if (SUCCEEDED(hr))
	{
		hr = pTopology->AddNode(pNode);
	}

	if (SUCCEEDED(hr))
	{
		hr = pNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, TRUE);
	}

	// Return the pointer to the caller.
	if (SUCCEEDED(hr))
	{
		*ppNode = pNode;
		(*ppNode)->AddRef();
	}

	if (pNode)
	{
		pNode->Release();
	}
	return hr;
}

// Add an output node to a topology.
HRESULT AddOutputNode(
    IMFTopology *pTopology,     // Topology.
    IMFActivate *pActivate,     // Media sink activation object.
    DWORD dwId,                 // Identifier of the stream sink.
    IMFTopologyNode **ppNode)   // Receives the node pointer.
{
    IMFTopologyNode *pNode = NULL;

	HRESULT hr = S_OK;

    // Create the node.
    hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pNode);
	CHECK_HR( hr );

    // Set the object pointer.
    hr = pNode->SetObject(pActivate);
	CHECK_HR( hr );

    // Set the stream sink ID attribute.
    hr = pNode->SetUINT32(MF_TOPONODE_STREAMID, dwId);
	CHECK_HR( hr );

    hr = pNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
	CHECK_HR( hr );

    // Add the node to the topology.
    hr = pTopology->AddNode(pNode);
	CHECK_HR( hr );

    // Return the pointer to the caller.
    *ppNode = pNode;
    (*ppNode)->AddRef();

done:
    SafeRelease(&pNode);
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
    IMFTopology *pTopology,         // Topology.
    IMFMediaSource *pSource,        // Media source.
    IMFPresentationDescriptor *pPD, // Presentation descriptor.
    DWORD iStream,                  // Stream index.
    HWND hVideoWnd,
	IMFVideoPresenter *pVideoPresenter, // Window for video playback.
	const WCHAR * audioDeviceId = 0)
{
    IMFStreamDescriptor *pSD = NULL;
    IMFActivate         *pSinkActivate = NULL;
    IMFTopologyNode     *pSourceNode = NULL;
    IMFTopologyNode     *pOutputNode = NULL;
	IMFMediaSink        *pMediaSink = NULL;

    BOOL fSelected = FALSE;

    HRESULT hr = pPD->GetStreamDescriptorByIndex(iStream, &fSelected, &pSD);
	CHECK_HR( hr );

    if (fSelected)
    {
        // Create the media sink activation object.
        hr = CreateMediaSinkActivate(pSD, hVideoWnd, &pSinkActivate,pVideoPresenter,&pMediaSink, audioDeviceId);
		CHECK_HR( hr );

        // Add a source node for this stream.
        hr = AddSourceNode(pTopology, pSource, pPD, pSD, &pSourceNode);
		CHECK_HR( hr );

        // Create the output node for the renderer.
		if ( pSinkActivate )
		{
			 hr = AddOutputNode(pTopology, pSinkActivate, 0, &pOutputNode);
		}
		else if ( pMediaSink )
		{
			IMFStreamSink  * pStreamSink=NULL;
			DWORD streamCount;

			pMediaSink->GetStreamSinkCount( &streamCount ) ;

			pMediaSink->GetStreamSinkByIndex( 0, &pStreamSink );

			hr = AddOutputNode( pTopology, pStreamSink, &pOutputNode );
			CHECK_HR( hr );

			

			//ThrowIfFail( pNode->SetObject( pStreamSink ) );
		}
		CHECK_HR( hr );	// Necessary?

        // Connect the source node to the output node.
        hr = pSourceNode->ConnectOutput(0, pOutputNode, 0);
    }
    // else: If not selected, don't add the branch. 

done:
    SafeRelease(&pSD);
    SafeRelease(&pSinkActivate);
    SafeRelease(&pSourceNode);
    SafeRelease(&pOutputNode);
	
    return hr;
}

//  Create a playback topology from a media source.
HRESULT CreatePlaybackTopology(
    IMFMediaSource *pSource,          // Media source.
    IMFPresentationDescriptor *pPD,   // Presentation descriptor.
    HWND hVideoWnd,                   // Video window.
    IMFTopology **ppTopology,        // Receives a pointer to the topology.
	IMFVideoPresenter *pVideoPresenter,
	const WCHAR *audioDeviceId
	)         
{
    IMFTopology *pTopology = NULL;
    DWORD cSourceStreams = 0;

	HRESULT hr = S_OK;

    // Create a new topology.
    hr = MFCreateTopology(&pTopology);
	CHECK_HR( hr );

    // Get the number of streams in the media source.
    hr = pPD->GetStreamDescriptorCount(&cSourceStreams);
	CHECK_HR( hr );

    // For each stream, create the topology nodes and add them to the topology.
    for (DWORD i = 0; i < cSourceStreams; i++)
    {
        hr = AddBranchToPartialTopology(pTopology, pSource, pPD, i, hVideoWnd,pVideoPresenter, audioDeviceId);
		CHECK_HR( hr );
    }
	
    // Return the IMFTopology pointer to the caller.
    *ppTopology = pTopology;
    (*ppTopology)->AddRef();

done:
    SafeRelease(&pTopology);
    return hr;
}

HRESULT AddToPlaybackTopology(
	IMFMediaSource *pSource,          // Media source.
	IMFPresentationDescriptor *pPD,   // Presentation descriptor.
	HWND hVideoWnd,                   // Video window.
	IMFTopology *pTopology,        // Receives a pointer to the topology.
	IMFVideoPresenter *pVideoPresenter)         
{
	DWORD cSourceStreams = 0;
	HRESULT hr;

	// Get the number of streams in the media source.
	hr = pPD->GetStreamDescriptorCount(&cSourceStreams);
	CHECK_HR( hr );

	// For each stream, create the topology nodes and add them to the topology.
	for (DWORD i = 1; i < cSourceStreams; i++)
	{
		//ofLogWarning("Ignoring audio stream of video2. If the video is missing check : ofxWMFVideoPlayerUtils");
		hr = AddBranchToPartialTopology(pTopology, pSource, pPD, i, hVideoWnd,pVideoPresenter);
		CHECK_HR( hr );
	}

done:
	
	return hr;
}

///------------ 
/// Extra functions
//---------------

float CPlayer::getDuration() {
	float duration = 0.0;
	if (m_pSource == NULL)
		return 0.0;
	IMFPresentationDescriptor *pDescriptor = NULL;
	HRESULT hr = m_pSource->CreatePresentationDescriptor(&pDescriptor);
	if (SUCCEEDED(hr)) {
		UINT64 longDuration = 0;
		hr = pDescriptor->GetUINT64(MF_PD_DURATION, &longDuration);
		if (SUCCEEDED(hr))
			duration = (float)longDuration / 10000000.0;
	}
	SafeRelease(&pDescriptor);
	return duration;
}

float CPlayer::getPosition() {
	float position = 0.0;
	if (m_pSession == NULL)
		return 0.0;
	IMFPresentationClock *pClock = NULL;
	HRESULT hr = m_pSession->GetClock((IMFClock **)&pClock);
	
	if (SUCCEEDED(hr)) {
		MFTIME longPosition = 0;
		hr = pClock->GetTime(&longPosition);
		if (SUCCEEDED(hr))
			position = (float)longPosition / 10000000.0;
	}
	SafeRelease(&pClock);
	return position;
}

float CPlayer::getFrameRate() {
	float fps = 0.0;
	if ( m_pSource == NULL )
		return 0.0;
	IMFPresentationDescriptor *pDescriptor = NULL;
	IMFStreamDescriptor *pStreamHandler = NULL;
	IMFMediaTypeHandler *pMediaType = NULL;
	IMFMediaType  *pType;
	DWORD nStream;
	if FAILED( m_pSource->CreatePresentationDescriptor( &pDescriptor ) ) goto done;
	if FAILED( pDescriptor->GetStreamDescriptorCount( &nStream ) ) goto done;
	for ( int i = 0; i < nStream; i++ )
	{
		BOOL selected;
		GUID type;
		if FAILED( pDescriptor->GetStreamDescriptorByIndex( i, &selected, &pStreamHandler ) ) goto done;
		if FAILED( pStreamHandler->GetMediaTypeHandler( &pMediaType ) ) goto done;
		if FAILED( pMediaType->GetMajorType( &type ) ) goto done;
		if FAILED( pMediaType->GetCurrentMediaType( &pType ) ) goto done;

		if ( type == MFMediaType_Video )
		{
			UINT32 num = 0;
			UINT32 denum = 1;

			MFGetAttributeRatio(
				pType,
				MF_MT_FRAME_RATE,
				&num,
				&denum
				);

			if ( denum != 0 ) {
				fps = (float) num / (float) denum;
				numFrames = denum;
			}
		}

		SafeRelease( &pStreamHandler );
		SafeRelease( &pMediaType );
		SafeRelease( &pType );
		if ( fps != 0.0 ) break; // we found the right stream, no point in continuing the loop
	}
done:
	SafeRelease( &pDescriptor );
	SafeRelease( &pStreamHandler );
	SafeRelease( &pMediaType );
	SafeRelease( &pType );
	return fps;
}

int CPlayer::getCurrentFrame() {
	int frame = 0;
	if ( m_pSource == NULL )
		return 0.0;
	IMFPresentationDescriptor *pDescriptor = NULL;
	IMFStreamDescriptor *pStreamHandler = NULL;
	IMFMediaTypeHandler *pMediaType = NULL;
	IMFMediaType  *pType;
	DWORD nStream;
	if FAILED( m_pSource->CreatePresentationDescriptor( &pDescriptor ) ) goto done;
	if FAILED( pDescriptor->GetStreamDescriptorCount( &nStream ) ) goto done;
	for ( int i = 0; i < nStream; i++ )
	{
		BOOL selected;
		GUID type;
		if FAILED( pDescriptor->GetStreamDescriptorByIndex( i, &selected, &pStreamHandler ) ) goto done;
		if FAILED( pStreamHandler->GetMediaTypeHandler( &pMediaType ) ) goto done;
		if FAILED( pMediaType->GetMajorType( &type ) ) goto done;
		if FAILED( pMediaType->GetCurrentMediaType( &pType ) ) goto done;

		if ( type == MFMediaType_Video )
		{
			UINT32 num = 0;
			UINT32 denum = 1;

			MFGetAttributeRatio(
				pType,
				MF_MT_FRAME_RATE,
				&num,
				&denum
				);

			if ( denum != 0 ) {
				frame = num;
				numFrames = denum; //update things
			}
		}

		SafeRelease( &pStreamHandler );
		SafeRelease( &pMediaType );
		SafeRelease( &pType );
		if ( frame != 0 ) break; // we found the right stream, no point in continuing the loop
	}
done:
	SafeRelease( &pDescriptor );
	SafeRelease( &pStreamHandler );
	SafeRelease( &pMediaType );
	SafeRelease( &pType );
	return frame;
}

HRESULT CPlayer:: SetMediaInfo( IMFPresentationDescriptor *pPD ) {
	_width = 0;
	_height = 0;
	HRESULT hr = S_OK;
	GUID guidMajorType = GUID_NULL;
	IMFMediaTypeHandler *pHandler = NULL;
	IMFStreamDescriptor* spStreamDesc = NULL;
	IMFMediaType *sourceType = NULL;

	DWORD count;
	pPD->GetStreamDescriptorCount(&count);
	for(DWORD i = 0; i < count; i++) {
		BOOL selected;
		
		hr = pPD->GetStreamDescriptorByIndex(i, &selected, &spStreamDesc);
		CHECK_HR( hr );
		if(selected) {
			hr = spStreamDesc->GetMediaTypeHandler(&pHandler);
			CHECK_HR( hr );
			hr = pHandler->GetMajorType(&guidMajorType);
			CHECK_HR( hr );
			
			if (MFMediaType_Video == guidMajorType) {
				// first get the source video size and allocate a new texture
				hr = pHandler->GetCurrentMediaType(&sourceType) ;

				UINT32 w, h;
				hr = MFGetAttributeSize(sourceType, MF_MT_FRAME_SIZE, &w, &h);
				if (hr == S_OK) {
					_width = w;
					_height =h;
				}

				UINT32 num = 0;
				UINT32 denum = 1;

				MFGetAttributeRatio(
					sourceType,
					MF_MT_FRAME_RATE,
					&num,
					&denum
					);

				if ( denum != 0 ) {
					numFrames = denum;
				}

				goto done;
			}

		}
	}

done:
	SafeRelease(&sourceType);
	SafeRelease(&pHandler);
	SafeRelease(&spStreamDesc);
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
	IMFRateControl *pRateControl = NULL;

	// Get the rate control object from the Media Session.
	hr = MFGetService(
		m_pSession,
		MF_RATE_CONTROL_SERVICE,
		IID_IMFRateControl,
		(void**) &pRateControl );

	// Set the playback rate.
	if ( SUCCEEDED( hr ) )
	{
		hr = pRateControl->SetRate( bThin, rateRequested );
	}

	// Clean up.
	SAFE_RELEASE( pRateControl );

	return hr;
}

float  CPlayer::GetPlaybackRate()
{
	HRESULT hr = S_OK;
	IMFRateControl *pRateControl = NULL;
	BOOL bThin;
	float rate;

	// Get the rate control object from the Media Session.
	hr = MFGetService(
		m_pSession,
		MF_RATE_CONTROL_SERVICE,
		IID_IMFRateControl,
		(void**) &pRateControl );

	// Set the playback rate.
	if ( SUCCEEDED( hr ) )
	{
		hr = pRateControl->GetRate( &bThin, &rate );
	}

	// Clean up.
	SAFE_RELEASE( pRateControl );
	if ( !FAILED( hr ) )
		return rate;
	else{
		cout << "Error: Could Not Get Rate" << endl;
		return NULL;
	}
}



