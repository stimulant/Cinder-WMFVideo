#include "ciWMFVideoPlayerUtils.h"
#include "ciWMFVideoPlayer.h"

#include "cinder/app/App.h"
#include "cinder/gl/Texture.h"
#include "cinder/Log.h"

using namespace std;
using namespace ci;
using namespace ci::app;

typedef std::pair<HWND, ciWMFVideoPlayer*> PlayerItem;
list<PlayerItem> g_WMFVideoPlayers;

LRESULT CALLBACK WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );
// Message handlers

ciWMFVideoPlayer::ScopedVideoTextureBind::ScopedVideoTextureBind( const ciWMFVideoPlayer& video, uint8_t textureUnit )
	: mCtx( gl::context() )
	, mTarget( video.mTex->getTarget() )
	, mTextureUnit( textureUnit )
	, mPlayer( video.mPlayer )
{
	mPlayer->mEVRPresenter->lockSharedTexture();
	mCtx->pushTextureBinding( mTarget, video.mTex->getId(), mTextureUnit );
}

ciWMFVideoPlayer::ScopedVideoTextureBind::ScopedVideoTextureBind( const std::shared_ptr<ciWMFVideoPlayer> video, uint8_t textureUnit )
	: ScopedVideoTextureBind( *video, textureUnit )
{}

ciWMFVideoPlayer::ScopedVideoTextureBind::~ScopedVideoTextureBind()
{
	mCtx->popTextureBinding( mTarget, mTextureUnit );
	mPlayer->mEVRPresenter->unlockSharedTexture();
}


void ciWMFVideoPlayer::bind(uint8_t texture_unit)
{
	mPlayer->mEVRPresenter->lockSharedTexture();
	mTex->bind(texture_unit);
	mIsSharedTextureLocked = true;
}

void ciWMFVideoPlayer::unbind()
{
	mIsSharedTextureLocked = false;
	mTex->unbind();
	mPlayer->mEVRPresenter->unlockSharedTexture();
}

ciWMFVideoPlayer* findPlayers( HWND hwnd )
{
	for each( PlayerItem e in g_WMFVideoPlayers ) {
		if( e.first == hwnd ) { return e.second; }
	}

	return NULL;
}

int  ciWMFVideoPlayer::mInstanceCount = 0;

ciWMFVideoPlayer::ciWMFVideoPlayer()
	: mPlayer( NULL )
	, mVideoFill( VideoFill::FILL )
	, mPlayPending( false )
{
	if( mInstanceCount == 0 )  {
		HRESULT hr = MFStartup( MF_VERSION );

		if( !SUCCEEDED( hr ) ) {
			//ofLog(OF_LOG_ERROR, "ciWMFVideoPlayer: Error while loading MF");
		}
	}

	mId = mInstanceCount;
	mInstanceCount++;
	this->InitInstance();

	mWaitForLoadedToPlay = false;
	mSharedTextureCreated = false;

	// Make sure the video is closed before the rendering context is lost.
	auto window = app::App::get()->getWindow();

	if( window ) {
		mWinCloseConnection = window->getSignalClose().connect( std::bind( &ciWMFVideoPlayer::close, this ) );
	}
}

ciWMFVideoPlayer::~ciWMFVideoPlayer()
{
	mWinCloseConnection.disconnect();

	if( mPlayer ) {
		mPlayer->Shutdown();
		//if (mSharedTextureCreated) mPlayer->mEVRPresenter->releaseSharedTexture();
		SafeRelease( &mPlayer );
	}

	CI_LOG_I( "Player " << mId << " Terminated" );

	auto hwnd = mHWNDPlayer;

	g_WMFVideoPlayers.erase( std::remove_if( g_WMFVideoPlayers.begin(), g_WMFVideoPlayers.end(), [hwnd]( PlayerItem & p ) {
		return p.first == hwnd;
	} ), g_WMFVideoPlayers.end() );

	mInstanceCount--;

	if( mInstanceCount == 0 ) {
		MFShutdown();
		CI_LOG_I( "Shutting down MF" );
	}
}

void ciWMFVideoPlayer::forceExit()
{
	if( mInstanceCount != 0 ) {
		CI_LOG_I( "Shutting down MF some ciWMFVideoPlayer remains" );
		MFShutdown();
	}
}

bool ciWMFVideoPlayer::loadMovie( const fs::path& filePath, const string& audioDevice, bool isAudioOnly )
{
	if( !mPlayer ) {
		//ofLogError("ciWMFVideoPlayer") << "Player not created. Can't open the movie.";
		return false;
	}

	mIsAudioOnly = isAudioOnly;

	//DWORD fileAttr = GetFileAttributesW( filePath.c_str() );
	//if (fileAttr == INVALID_FILE_ATTRIBUTES)
	//{
	//	CI_LOG_E( "The video file could not be found: '" << filePath );
	//	//ofLog(OF_LOG_ERROR,"ciWMFVideoPlayer:" + s.str());
	//	return false;
	//}

	//CI_LOG_I( "Videoplayer[" << mId << "] loading " << name );

	HRESULT hr = S_OK;
	string s = filePath.string();
	std::wstring w( s.length(), L' ' );
	std::copy( s.begin(), s.end(), w.begin() );

	std::wstring a( audioDevice.length(), L' ' );
	std::copy( audioDevice.begin(), audioDevice.end(), a.begin() );

	hr = mPlayer->OpenURL( w.c_str(), a.c_str() );

	//	CI_LOG_D(GetPlayerStateString(mPlayer->GetState()));

	if( !isAudioOnly ) {
		if( !mSharedTextureCreated ) {
			mWidth = mPlayer->getWidth();
			mHeight = mPlayer->getHeight();

			gl::Texture::Format format;
			format.setInternalFormat( GL_RGBA );
			format.setTargetRect();
			format.loadTopDown( true );
			mTex = gl::Texture::create( mWidth, mHeight, format );
			mPlayer->mEVRPresenter->createSharedTexture( mWidth, mHeight, mTex->getId() );
			mSharedTextureCreated = true;
		}
		else {
			if( ( mWidth != mPlayer->getWidth() ) || ( mHeight != mPlayer->getHeight() ) ) {
				mPlayer->mEVRPresenter->releaseSharedTexture();

				mWidth = mPlayer->getWidth();
				mHeight = mPlayer->getHeight();

				gl::Texture::Format format;
				format.setInternalFormat( GL_RGBA );
				format.setTargetRect();
				format.loadTopDown( true );
				mTex = gl::Texture::create( mWidth, mHeight, format );
				mPlayer->mEVRPresenter->createSharedTexture( mWidth, mHeight, mTex->getId() );
			}
		}
	}

	mWaitForLoadedToPlay = false;
	return true;
}

void ciWMFVideoPlayer::draw( int x, int y, int w, int h )
{
	if( !mPlayer ) {
		return;
	}

	if( mIsAudioOnly ) {
		return;
	}

	mPlayer->mEVRPresenter->lockSharedTexture();

	if( mTex ) {
		Rectf destRect = Rectf( x, y, x + w, y + h );

		switch( mVideoFill ) {
			case VideoFill::FILL:
				gl::draw( mTex, destRect );
				break;

			case VideoFill::ASPECT_FIT:
				gl::draw( mTex, Rectf( mTex->getBounds() ).getCenteredFit( destRect, true ) ) ;
				break;

			case VideoFill::CROP_FIT:
				gl::draw( mTex, Area( destRect.getCenteredFit( mTex->getBounds(), true ) ), destRect );
				break;
		}

	}

	mPlayer->mEVRPresenter->unlockSharedTexture();
}

bool ciWMFVideoPlayer::isPlaying()
{
	return mPlayer->GetState() == STARTED;
}

bool ciWMFVideoPlayer::isStopped()
{
	return ( mPlayer->GetState() == STOPPED || mPlayer->GetState() == PAUSED );
}

bool ciWMFVideoPlayer::isPaused()
{
	return mPlayer->GetState() == PAUSED;
}

void ciWMFVideoPlayer::close()
{
	mPlayer->Shutdown();
}

void ciWMFVideoPlayer::update()
{
	if( !mPlayer ) { return; }

	if ( mWaitForLoadedToPlay && mPlayer->GetState() == PAUSED) {
		mWaitForLoadedToPlay = false;
		mPlayer->Play();
	}

	if( mPlayPending && mPlayer->GetState() == STARTED ) {
		mPlayPending = false;
		mPlayStartedSignal.emit();
	}
}

void ciWMFVideoPlayer::play()
{
	if( !mPlayer ) { return; }

	if( mPlayer->GetState()  == OPEN_PENDING ) { mWaitForLoadedToPlay = true; }

	mPlayer->Play();
	mPlayPending = true;
}

void ciWMFVideoPlayer::stop()
{
	mPlayer->Stop();
}

void ciWMFVideoPlayer::pause()
{
	if (mPlayer->GetState() == OPEN_PENDING) { mWaitForLoadedToPlay = false; }
	mPlayer->Pause();
}

float ciWMFVideoPlayer::getPosition()
{
	return mPlayer->getPosition();
}

float ciWMFVideoPlayer::getFrameRate()
{
	return mPlayer->getFrameRate();
}

float ciWMFVideoPlayer::getDuration()
{
	return mPlayer->getDuration();
}

float ciWMFVideoPlayer::getVolume()
{
	return mPlayer->getVolume();
}

void ciWMFVideoPlayer::setPosition( float pos )
{
	mPlayer->setPosition( pos );
}

void ciWMFVideoPlayer::setVolume( float vol )
{
	mPlayer->setVolume( vol );
}

void ciWMFVideoPlayer::stepForward()
{
	if( mPlayer->GetState() == STOPPED ) {
		return;
	}

	mPlayer->Pause();
	float fps = mPlayer->getFrameRate();
	float step = 1 / fps;
	float currentVidPos = mPlayer->getPosition();
	float targetVidPos = currentVidPos + step;

	if( mPlayer->GetState() == PAUSED ) {
		play();
	}

	mPlayer->setPosition( mPlayer->getPosition() + step );
	mPlayer->Pause();
}
float ciWMFVideoPlayer::getSpeed()
{
	return mPlayer->GetPlaybackRate();
}

bool ciWMFVideoPlayer::setSpeed( float speed, bool useThinning )
{
	//according to MSDN playback must be stopped to change between forward and reverse playback and vice versa
	//but is only required to pause in order to shift between forward rates
	float curRate = getSpeed();
	HRESULT hr = S_OK;
	bool resume = isPlaying();

	if( curRate >= 0 && speed >= 0 ) {
		if( !isPaused() ) {
			mPlayer->Pause();
		}

		hr = mPlayer->SetPlaybackRate( useThinning, speed );

		if( resume ) {
			mPlayer->Play();
		}
	}
	else {
		//setting to a negative doesn't seem to work though no error is thrown...
		float position = getPosition();
		if(isPlaying())
		mPlayer->Stop();
		hr = mPlayer->SetPlaybackRate(useThinning, speed);
		if(resume){
			mPlayer->setPosition(position);
			mPlayer->Play();
		}
	}

	if( hr == S_OK ) {
		return true;
	}
	else {
		if( hr == MF_E_REVERSE_UNSUPPORTED ) {
			cout << "The object does not support reverse playback." << endl;
		}

		if( hr == MF_E_THINNING_UNSUPPORTED ) {
			cout << "The object does not support thinning." << endl;
		}

		if( hr == MF_E_UNSUPPORTED_RATE ) {
			cout << "The object does not support the requested playback rate." << endl;
		}

		if( hr == MF_E_UNSUPPORTED_RATE_TRANSITION ) {
			cout << "The object cannot change to the new rate while in the running state." << endl;
		}

		return false;
	}
}

PresentationEndedSignal& ciWMFVideoPlayer::getPresentationEndedSignal()
{
	if( mPlayer ) {
		return mPlayer->getPresentationEndedSignal();
	}
}

float ciWMFVideoPlayer::getHeight() { return mPlayer->getHeight(); }
float ciWMFVideoPlayer::getWidth() { return mPlayer->getWidth(); }
void  ciWMFVideoPlayer::setLoop( bool isLooping ) { mIsLooping = isLooping; mPlayer->setLooping( isLooping ); }

ci::vec2 ciWMFVideoPlayer::getTextureSize()
{
	if( mTex ) {
		return ci::vec2( mTex->getWidth(), mTex->getHeight() );
	}

	return ci::vec2( 0 );
}

//-----------------------------------
// Prvate Functions
//-----------------------------------

// Handler for Media Session events.
void ciWMFVideoPlayer::OnPlayerEvent( HWND hwnd, WPARAM pUnkPtr )
{
	HRESULT hr = mPlayer->HandleEvent( pUnkPtr );

	if( FAILED( hr ) ) {
		//ofLogError("ciWMFVideoPlayer", "An error occurred.");
	}
}

LRESULT CALLBACK WndProcDummy( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	switch( message ) {
		case WM_CREATE: {
			return DefWindowProc( hwnd, message, wParam, lParam );
		}

		default: {
			ciWMFVideoPlayer*   myPlayer = findPlayers( hwnd );

			if( !myPlayer ) {
				return DefWindowProc( hwnd, message, wParam, lParam );
			}

			return myPlayer->WndProc( hwnd, message, wParam, lParam );
		}
	}

	return 0;
}

LRESULT  ciWMFVideoPlayer::WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	switch( message ) {
		case WM_DESTROY:
			PostQuitMessage( 0 );
			break;

		case WM_APP_PLAYER_EVENT:
			OnPlayerEvent( hwnd, wParam );
			break;

		default:
			return DefWindowProc( hwnd, message, wParam, lParam );
	}

	return 0;
}

//  Create the application window.
BOOL ciWMFVideoPlayer::InitInstance()
{
	PCWSTR szWindowClass = L"MFBASICPLAYBACK" ;
	HWND hwnd;
	WNDCLASSEX wcex;

	//   g_hInstance = hInst; // Store the instance handle.
	// Register the window class.
	ZeroMemory( &wcex, sizeof( WNDCLASSEX ) );
	wcex.cbSize         = sizeof( WNDCLASSEX );
	wcex.style          = CS_HREDRAW | CS_VREDRAW  ;

	wcex.lpfnWndProc    =  WndProcDummy;
	//  wcex.hInstance      = hInst;
	wcex.hbrBackground  = ( HBRUSH )( BLACK_BRUSH );
	// wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_MFPLAYBACK);
	wcex.lpszClassName  = szWindowClass;

	if( RegisterClassEx( &wcex ) == 0 ) {
		// return FALSE;
	}

	// Create the application window.
	hwnd = CreateWindow( szWindowClass, L"", WS_OVERLAPPEDWINDOW,
	                     CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL );

	if( hwnd == 0 ) {
		return FALSE;
	}

	g_WMFVideoPlayers.push_back( std::pair<HWND, ciWMFVideoPlayer*>( hwnd, this ) );
	HRESULT hr = CPlayer::CreateInstance( hwnd, hwnd, &mPlayer );

	LONG style2 = ::GetWindowLong( hwnd, GWL_STYLE );
	style2 &= ~WS_DLGFRAME;
	style2 &= ~WS_CAPTION;
	style2 &= ~WS_BORDER;
	style2 &= WS_POPUP;
	LONG exstyle2 = ::GetWindowLong( hwnd, GWL_EXSTYLE );
	exstyle2 &= ~WS_EX_DLGMODALFRAME;
	::SetWindowLong( hwnd, GWL_STYLE, style2 );
	::SetWindowLong( hwnd, GWL_EXSTYLE, exstyle2 );

	mHWNDPlayer = hwnd;
	UpdateWindow( hwnd );

	return TRUE;
}
