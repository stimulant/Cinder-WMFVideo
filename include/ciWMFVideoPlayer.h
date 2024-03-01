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
#include "cinder/Signals.h"

class ciWMFVideoPlayer;
class CPlayer;

enum VideoFill {
	FILL,	// fill rectangle (distort texture to fill)
	ASPECT_FIT,	// fit rectangle and keep aspect ratio with blank space
	CROP_FIT	// fit rectangle, keep aspect ratio and crop overflow
};


typedef cinder::signals::Signal<void()> PlayStartedSignal;
typedef std::shared_ptr<class ciWMFVideoPlayer> ciWMFVideoPlayerRef;

class ciWMFVideoPlayer
{
	private:
		static int mInstanceCount;
		HWND mHWNDPlayer;

		int mWidth;
		int mHeight;

		bool mWaitForLoadedToPlay;
		bool mIsLooping;
		VideoFill mVideoFill;
		bool mIsSharedTextureLocked = false;

		bool mSharedTextureCreated;

		ci::gl::TextureRef mTex;

		cinder::signals::Connection mWinCloseConnection;

		BOOL InitInstance();
		void OnPlayerEvent( HWND hwnd, WPARAM pUnkPtr );

		PlayStartedSignal       mPlayStartedSignal;
		bool                    mPlayPending;
		bool                    mIsAudioOnly;

	public:
		friend struct ScopedVideoTextureBind;
		struct ScopedVideoTextureBind : private ci::Noncopyable {
			public:
				ScopedVideoTextureBind( const ciWMFVideoPlayerRef video, uint8_t textureUnit );
				ScopedVideoTextureBind( const ciWMFVideoPlayer& video, uint8_t textureUnit );
				~ScopedVideoTextureBind();

			private:
				ci::gl::Context* mCtx;
				GLenum mTarget;
				uint8_t mTextureUnit;
				CPlayer* mPlayer;
		};

		CPlayer* mPlayer;
		int mId;

		ciWMFVideoPlayer();
		~ciWMFVideoPlayer();

		bool loadMovie( const ci::fs::path& filePath, const std::string& audioDevice = "", bool isAudioOnly = false );
		void close();
		void update();

		void play();
		void stop();
		void pause();

		float getPosition();
		float getDuration();
		float getFrameRate();
		float getVolume();

		void setPosition( float pos );
		void stepForward();
		void setVolume( float vol );

		float getHeight();
		float getWidth();
		inline ci::vec2 getSize() { return ci::vec2(getWidth(), getHeight()); }

		bool isPlaying();
		bool isStopped();
		bool isPaused();

		void bind(uint8_t texture_unit);
		void unbind();

		bool isTexturedLocked()const { return mIsSharedTextureLocked; }

		bool setSpeed( float speed, bool useThinning = false ); //thinning drops delta frames for faster playback though appears to be choppy, default is false
		float getSpeed();

		void setLoop( bool isLooping );
		bool isLooping() const { return mIsLooping; }

		bool hasTexture() const { return ( mPlayer && mTex ); }
		ci::vec2 getTextureSize();

		void setVideoFill( VideoFill videoFill ) { mVideoFill = videoFill; }

		void draw( int x, int y, int w, int h );
		void draw( int x, int y ) { draw( x, y, getWidth(), getHeight() ); }

		HWND getHandle() const { return mHWNDPlayer; }
		LRESULT WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );

		PresentationEndedSignal& getPresentationEndedSignal();

		PlayStartedSignal&      getPlayStartedSignal() { return mPlayStartedSignal; }

		static void forceExit();
};