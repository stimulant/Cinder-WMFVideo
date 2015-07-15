#include "ciWMFVideoPlayerUtils.h"
#include "ciWMFVideoPlayer.h"

#include "cinder/app/App.h"
#include "cinder/gl/Texture.h"
#include "cinder/Log.h"

using namespace std;
using namespace ci;
using namespace ci::app;

typedef std::pair<HWND,ciWMFVideoPlayer*> PlayerItem;
list<PlayerItem> g_WMFVideoPlayers;

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
// Message handlers

ciWMFVideoPlayer::ScopedVideoTextureBind::ScopedVideoTextureBind( const ciWMFVideoPlayer &video, uint8_t textureUnit ) :
	mCtx( gl::context() ), mTarget( video._tex->getTarget() ), mTextureUnit( textureUnit ), mPlayer( video._player )
{
	mPlayer->m_pEVRPresenter->lockSharedTexture();
	mCtx->pushTextureBinding( mTarget, video._tex->getId(), mTextureUnit );
}

ciWMFVideoPlayer::ScopedVideoTextureBind::~ScopedVideoTextureBind()
{
	mCtx->popTextureBinding( mTarget, mTextureUnit );
	mPlayer->m_pEVRPresenter->unlockSharedTexture();
}

ciWMFVideoPlayer* findPlayers(HWND hwnd)
{
	for each (PlayerItem e in g_WMFVideoPlayers)
	{
		if (e.first == hwnd) return e.second;
	}
	return NULL;
}

int  ciWMFVideoPlayer::_instanceCount=0;

ciWMFVideoPlayer::ciWMFVideoPlayer() : _player(NULL)
{
	if (_instanceCount ==0)  {
		HRESULT hr = MFStartup(MF_VERSION);
	  if (!SUCCEEDED(hr))
		{
			//ofLog(OF_LOG_ERROR, "ciWMFVideoPlayer: Error while loading MF");
		}
	}

	_id = _instanceCount;
	_instanceCount++;
	this->InitInstance();
	
	_waitForLoadedToPlay = false;
	_sharedTextureCreated = false;	
}
	 
ciWMFVideoPlayer::~ciWMFVideoPlayer() {
	if (_player)
    {
		_player->Shutdown();
		//if (_sharedTextureCreated) _player->m_pEVRPresenter->releaseSharedTexture();
        SafeRelease(&_player);
    }

	CI_LOG_I( "Player " << _id << " Terminated" );
	_instanceCount--;
	if (_instanceCount == 0) 
	{
		 MFShutdown();
		 CI_LOG_I( "Shutting down MF" );
	}
}

void ciWMFVideoPlayer::forceExit()
{
	if (_instanceCount != 0) 
	{
		CI_LOG_I( "Shutting down MF some ciWMFVideoPlayer remains" );
		MFShutdown();
	}
}

bool ciWMFVideoPlayer::loadMovie( const fs::path &filePath, const string &audioDevice ) 
 {
	 if (!_player) 
	 { 
		//ofLogError("ciWMFVideoPlayer") << "Player not created. Can't open the movie.";
		 return false;
	}

	DWORD fileAttr = GetFileAttributesW( filePath.c_str() );
	if (fileAttr == INVALID_FILE_ATTRIBUTES) 
	{
		CI_LOG_E( "The video file could not be found: '" << filePath );
		//ofLog(OF_LOG_ERROR,"ciWMFVideoPlayer:" + s.str());
		return false;
	}
	
	//CI_LOG_I( "Videoplayer[" << _id << "] loading " << name );
	HRESULT hr = S_OK;
	string s = filePath.string();
	std::wstring w(s.length(), L' ');
	std::copy(s.begin(), s.end(), w.begin());

	std::wstring a(audioDevice.length(), L' ');
	std::copy(audioDevice.begin(), audioDevice.end(), a.begin());

	hr = _player->OpenURL( w.c_str(), a.c_str() );
	if (!_sharedTextureCreated)
	{
		_width = _player->getWidth();
		_height = _player->getHeight();

		gl::Texture::Format format;
		format.setInternalFormat(GL_RGBA);
		format.setTargetRect();
		_tex = gl::Texture::create(_width,_height, format);
		//_tex.allocate(_width,_height,GL_RGBA,true);
		_player->m_pEVRPresenter->createSharedTexture(_width, _height, _tex->getId());
		_sharedTextureCreated = true;
	}
	else 
	{
		if ((_width != _player->getWidth()) || (_height != _player->getHeight()))
		{
			_player->m_pEVRPresenter->releaseSharedTexture();

			_width = _player->getWidth();
			_height = _player->getHeight();

			gl::Texture::Format format;
			format.setInternalFormat(GL_RGBA);
			format.setTargetRect();
			_tex = gl::Texture::create(_width,_height, format);
			//_tex.allocate(_width,_height,GL_RGBA,true);
			_player->m_pEVRPresenter->createSharedTexture(_width, _height, _tex->getId());
		}
	}

	_waitForLoadedToPlay = false;
	return true;
 }

 void ciWMFVideoPlayer::draw(int x, int y, int w, int h) 
 {
	_player->m_pEVRPresenter->lockSharedTexture();

	//_tex.draw(x,y,w,h);
	gl::draw(_tex, Rectf(x, y, x+w, y+h));

	_player->m_pEVRPresenter->unlockSharedTexture();
 }

bool  ciWMFVideoPlayer:: isPlaying() {
	return _player->GetState() == Started;
 }
bool  ciWMFVideoPlayer:: isStopped() {
	return (_player->GetState() == Stopped || _player->GetState() == Paused);
 }

bool  ciWMFVideoPlayer:: isPaused() 
{
	return _player->GetState() == Paused;
}

 void	ciWMFVideoPlayer::	close() {
	 _player->Shutdown();

}
void	ciWMFVideoPlayer::	update() {
	if (!_player) return;
	if ((_waitForLoadedToPlay) && _player->GetState() == Paused)
	{
		_waitForLoadedToPlay=false;
		_player->Play();
		
	}
	return;
 }

void	ciWMFVideoPlayer::	play() 
{
	if (!_player) return;
	if (_player->GetState()  == OpenPending) _waitForLoadedToPlay = true;
	_player->Play();
}

void	ciWMFVideoPlayer::	stop() 
{
	_player->Stop();
}

void	ciWMFVideoPlayer::	pause() 
{
	_player->Pause();
}

float 			ciWMFVideoPlayer::	getPosition() {
	return _player->getPosition();
}

float 			ciWMFVideoPlayer::	getDuration() {
	return _player->getDuration();
}

void ciWMFVideoPlayer::setPosition(float pos)
{
	_player->setPosition(pos);
}

float ciWMFVideoPlayer::getHeight() { return _player->getHeight(); }
float ciWMFVideoPlayer::getWidth() { return _player->getWidth(); }
void  ciWMFVideoPlayer::setLoop(bool isLooping) { _isLooping = isLooping; _player->setLooping(isLooping); }

//-----------------------------------
// Prvate Functions
//-----------------------------------

// Handler for Media Session events.
void ciWMFVideoPlayer::OnPlayerEvent(HWND hwnd, WPARAM pUnkPtr)
{
    HRESULT hr = _player->HandleEvent(pUnkPtr);
    if (FAILED(hr))
    {
        //ofLogError("ciWMFVideoPlayer", "An error occurred.");
    }
 }

LRESULT CALLBACK WndProcDummy(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
		case WM_CREATE:
		{
			return DefWindowProc(hwnd, message, wParam, lParam);
		}
		default:
		{
			ciWMFVideoPlayer*   myPlayer = findPlayers(hwnd);
			if (!myPlayer) 
				return DefWindowProc(hwnd, message, wParam, lParam);
			return myPlayer->WndProc (hwnd, message, wParam, lParam);
		}
    }
    return 0;
}

LRESULT  ciWMFVideoPlayer::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		case WM_APP_PLAYER_EVENT:
			OnPlayerEvent(hwnd, wParam);
			break;

		default:
			return DefWindowProc(hwnd, message, wParam, lParam);
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
    ZeroMemory(&wcex, sizeof(WNDCLASSEX));
    wcex.cbSize         = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW  ;

    wcex.lpfnWndProc    =  WndProcDummy;
	//  wcex.hInstance      = hInst;
	wcex.hbrBackground  = (HBRUSH)(BLACK_BRUSH);
    // wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_MFPLAYBACK);
    wcex.lpszClassName  = szWindowClass;

    if (RegisterClassEx(&wcex) == 0)
    {
       // return FALSE;
    }

    // Create the application window.
    hwnd = CreateWindow(szWindowClass, L"", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL);
    if (hwnd == 0)
    {
        return FALSE;
    }

	g_WMFVideoPlayers.push_back(std::pair<HWND,ciWMFVideoPlayer*>(hwnd,this));
	HRESULT hr = CPlayer::CreateInstance(hwnd, hwnd, &_player); 

	LONG style2 = ::GetWindowLong(hwnd, GWL_STYLE);  
    style2 &= ~WS_DLGFRAME;
    style2 &= ~WS_CAPTION; 
    style2 &= ~WS_BORDER; 
    style2 &= WS_POPUP;
    LONG exstyle2 = ::GetWindowLong(hwnd, GWL_EXSTYLE);  
    exstyle2 &= ~WS_EX_DLGMODALFRAME;  
    ::SetWindowLong(hwnd, GWL_STYLE, style2);  
    ::SetWindowLong(hwnd, GWL_EXSTYLE, exstyle2);  

	_hwndPlayer = hwnd;
    UpdateWindow(hwnd);
	
    return TRUE;
}

