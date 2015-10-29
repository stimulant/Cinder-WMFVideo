#pragma once

/*
#ifdef __gl_h_
#undef __gl_h_
#endif

#ifdef __glext_h_
#undef __glext_h_
#endif

#ifdef __wglext_h_
#undef __wglext_h_
#endif

#ifdef __glxext_h_
#undef __glxext_h_
#endif*/

#include "ciWMFVideoPlayerUtils.h"
#include "presenter/EVRPresenter.h"
#include "cinder/gl/gl.h"

class ciWMFVideoPlayer;
class CPlayer;

class ciWMFVideoPlayer
{
private:
	static int			_instanceCount;
	HWND				_hwndPlayer;
	BOOL				bRepaintClient;
		
	int					_width;
	int					_height;

	bool				_waitForLoadedToPlay;
	bool				_isLooping;

	bool				_sharedTextureCreated;
		
	ci::gl::TextureRef	_tex;
	
	BOOL	InitInstance();
	void	OnPlayerEvent( HWND hwnd, WPARAM pUnkPtr );

public:
	friend struct ScopedVideoTextureBind;
	struct ScopedVideoTextureBind : private ci::Noncopyable
	{
	public:
		ScopedVideoTextureBind( const ciWMFVideoPlayer &video, uint8_t textureUnit );
		~ScopedVideoTextureBind();

	private:
		ci::gl::Context *mCtx;
		GLenum			mTarget;
		uint8_t			mTextureUnit;
		CPlayer			*mPlayer;
	};

	CPlayer*	_player;
	int			_id;
	
	ciWMFVideoPlayer();
	~ciWMFVideoPlayer();

	bool	loadMovie( const ci::fs::path &filePath, const std::string &audioDevice = "" );
	void	close();
	void	update();
	
	void	play();
	void	stop();		
	void	pause();
	
	float	getPosition();
	float	getDuration();
	
	void	setPosition( float pos );
	
	float	getHeight();
	float	getWidth();
	
	bool	isPlaying(); 
	bool	isStopped();
	bool	isPaused();

	bool	setSpeed( float speed, bool useThinning = false ); //thinning drops delta frames for faster playback though appears to be choppy, default is false
	float	getSpeed();
	
	void	setLoop( bool isLooping );
	bool	isLooping() const { return _isLooping; }

	void	draw( int x, int y , int w, int h );
	void	draw( int x, int y ) { draw( x, y, getWidth(), getHeight() ); }

	HWND getHandle() const { return _hwndPlayer; }
	LRESULT WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );

	static void forceExit();
};