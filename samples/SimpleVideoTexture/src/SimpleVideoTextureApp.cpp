#include "cinder/app/App.h"
#include "cinder/gl/gl.h"
#include "cinder/params/Params.h"
#include "cinder/CameraUi.h"

#include "ciWMFVideoPlayer.h"

class SimpleVideoTextureApp : public ci::app::App
{
public:
	SimpleVideoTextureApp();
	~SimpleVideoTextureApp();

	void	draw() override;
	void	mouseDown( ci::app::MouseEvent event ) override;
	void	mouseDrag( ci::app::MouseEvent event ) override;
	void	mouseUp( ci::app::MouseEvent event ) override;
	void	resize() override;
	void	setup() override;
	void	update() override;

private:
	void	loadShaders();

private:
	ci::CameraPersp				mCam;
	ci::CameraUi				mCamUi;

	ci::gl::BatchRef			mBatchWirePlane;
	ci::gl::BatchRef			mBatchPlaneNoise;
	ci::gl::BatchRef			mBatchPlaneVideo;
	ci::gl::GlslProgRef			mGlslTvNoise;
	ci::gl::GlslProgRef			mGlslStockColor;
	ci::gl::GlslProgRef			mGlslVideoTexture;

	ciWMFVideoPlayer			mVideo;
	float						mVideoPos;
	float						mVideoDuration;
	bool						mIsVideoLoaded;

	ci::params::InterfaceGlRef	mParams;
	float						mFps;
	bool						mIsFullScreen;
};


#include "cinder/app/RendererGl.h"
#include "cinder/Log.h"

using namespace ci;
using namespace ci::app;
using namespace std;

SimpleVideoTextureApp::SimpleVideoTextureApp() : 
	mVideoPos( 0.0f ), 
	mVideoDuration( 0.0f ), 
	mIsVideoLoaded( false ), 
	mFps( 0.0f ), 
	mIsFullScreen( false )
{
	loadShaders();

	mBatchWirePlane = gl::Batch::create( geom::WirePlane().normal( vec3( 0, 0, 1 ) ).subdivisions( vec2( 3 ) ), mGlslStockColor );
	mBatchPlaneNoise = gl::Batch::create( geom::Plane().normal( vec3( 0, 0, 1 ) ), mGlslTvNoise );
	mBatchPlaneVideo = gl::Batch::create( geom::Plane().normal( vec3( 0, 0, 1 ) ), mGlslVideoTexture );

	mCam.setPerspective( 60.0f, getWindowAspectRatio(), 0.01f, 10000.0f );
	mCam.lookAt( vec3( 0, 0, 500 ), vec3(), vec3( 0, 1, 0 ) );

	mCamUi.setCamera( &mCam );
	mCamUi.setMouseWheelMultiplier( -mCamUi.getMouseWheelMultiplier() );
}

SimpleVideoTextureApp::~SimpleVideoTextureApp()
{
}

void SimpleVideoTextureApp::draw()
{
	gl::ScopedViewport scopedViewport( getWindowSize() );
	gl::ScopedDepth scopedDepth( true );
	gl::ScopedMatrices scopedMatrices;
	
	gl::setMatrices( mCam );
	gl::clear( Color( 0, 0, 0 ) );

	if ( mIsVideoLoaded ) {
		vec2 videoSize = vec2( mVideo.getWidth(), mVideo.getHeight() );
		mGlslVideoTexture->uniform( "uVideoSize", videoSize );
		videoSize *= 0.25f;
		videoSize *= 0.5f;

		{
			gl::ScopedColor scopedColor( Colorf( 1, 0, 1 ) );
			gl::ScopedModelMatrix scopedModelMatrix;

			gl::translate( vec3( 0, 0, 1.0f ) );
			gl::scale( vec3( videoSize, 1.0f ) );
			mBatchWirePlane->draw();
		}

		{
			gl::ScopedColor scopedColor( Colorf::white() );
			gl::ScopedModelMatrix scopedModelMatrix;

			ciWMFVideoPlayer::ScopedVideoTextureBind scopedVideoTex( mVideo, 0 );

			gl::scale( vec3( videoSize, 1.0f ) );
			mBatchPlaneVideo->draw();
		}
	} else {
		{
			gl::ScopedColor scopedColor( Colorf( 1, 0, 1 ) );
			gl::ScopedModelMatrix scopedModelMatrix;

			gl::translate( vec3( 0, 0, 1 ) );
			gl::scale( vec3( 320, 180, 1 ) );
			mBatchWirePlane->draw();
		}

		{
			gl::ScopedModelMatrix scopedModelMatrix;

			mGlslTvNoise->uniform( "uTime", static_cast<float>( getElapsedSeconds() ) );

			gl::scale( vec3( 320, 180, 1 ) );
			mBatchPlaneNoise->draw();
		}
	}

	mParams->draw();
}

void SimpleVideoTextureApp::loadShaders()
{
	mGlslStockColor = gl::getStockShader( gl::ShaderDef().color() );

	try {
		mGlslVideoTexture = gl::GlslProg::create( gl::GlslProg::Format()
			.vertex( loadAsset( "video_texture.vs.glsl" ) ) 
			.fragment( loadAsset( "video_texture.fs.glsl" ) ) );
	} catch ( gl::GlslProgCompileExc ex ) {
		CI_LOG_E( "<< GlslProg Compile Error >>\n" << ex.what() );
	} catch ( gl::GlslProgLinkExc ex ) {
		CI_LOG_E( "<< GlslProg Link Error >>\n" << ex.what() );
	} catch ( gl::GlslProgExc ex ) {
		CI_LOG_E( "<< GlslProg Error >> " << ex.what() );
	} catch ( AssetLoadExc ex ) {
		CI_LOG_E( "<< Asset Load Error >> " << ex.what() );
	}

	try {
		mGlslTvNoise = gl::GlslProg::create( gl::GlslProg::Format()
			.vertex( loadAsset( "tv_noise.vs.glsl" ) ) 
			.fragment( loadAsset( "tv_noise.fs.glsl" ) ) );
	} catch ( gl::GlslProgCompileExc ex ) {
		CI_LOG_E( "<< GlslProg Compile Error >>\n" << ex.what() );
	} catch ( gl::GlslProgLinkExc ex ) {
		CI_LOG_E( "<< GlslProg Link Error >>\n" << ex.what() );
	} catch ( gl::GlslProgExc ex ) {
		CI_LOG_E( "<< GlslProg Error >> " << ex.what() );
	} catch ( AssetLoadExc ex ) {
		CI_LOG_E( "<< Asset Load Error >> " << ex.what() );
	}

	if ( mBatchWirePlane ) {
		mBatchWirePlane->replaceGlslProg( mGlslStockColor );
	}

	if ( mBatchPlaneVideo ) {
		mBatchPlaneVideo->replaceGlslProg( mGlslVideoTexture );
	}

	if ( mBatchPlaneNoise ) {
		mBatchPlaneNoise->replaceGlslProg( mGlslTvNoise );
	}
}

void SimpleVideoTextureApp::mouseDown( MouseEvent event )
{
	mCamUi.mouseDown( event );
}

void SimpleVideoTextureApp::mouseDrag( MouseEvent event )
{
	mCamUi.mouseDrag( event );
}

void SimpleVideoTextureApp::mouseUp( MouseEvent event )
{
}

void SimpleVideoTextureApp::resize()
{
	mCam.setAspectRatio( getWindowAspectRatio() );
}

void SimpleVideoTextureApp::setup()
{
	auto reloadShaders = [ & ]() -> void
	{
		loadShaders();
	};

	auto getIsFullScreen = [ & ]() -> bool
	{
		return mIsFullScreen;
	};

	auto setIsFullScreen = [ & ]( bool value ) -> void
	{
		mIsFullScreen = value;
		setFullScreen( mIsFullScreen );
	};

	auto loadVideo = [ & ]() -> void
	{
		fs::path videoPath = getOpenFilePath( fs::path(), { "mp4", "wmv", "avi", "mov" } );
		if ( !videoPath.empty() ) {
			if ( !mVideo.isStopped() ) {
				mVideo.stop();
			}

			mIsVideoLoaded = mVideo.loadMovie( videoPath );
			mVideoDuration = mVideo.getDuration();
			mVideoPos = mVideo.getPosition();
			mVideo.play();
			mVideo.setLoop(true);
		}
	};

	mParams = params::InterfaceGl::create( "Params", ivec2( 210, 210 ) );
	mParams->addParam<float>( "FPS", &mFps, true );
	mParams->addParam<bool>( "Full Screen", setIsFullScreen, getIsFullScreen ).key( "F" );
	mParams->addParam<float>( "Video duration", &mVideoDuration, true );
	mParams->addParam<float>( "Video position", &mVideoPos, true );
	mParams->addButton( "Reload shaders", reloadShaders, "key=R" );
	mParams->addButton( "Load video", loadVideo, "key=V" );
}

void SimpleVideoTextureApp::update()
{
	mFps = getAverageFps();
	mVideo.update();
	mVideoPos = mVideo.getPosition();
}

CINDER_APP( SimpleVideoTextureApp, RendererGl, []( App::Settings * settings )
{
	settings->setFrameRate( 60.0f );
	settings->prepareWindow( Window::Format().size( 1280, 720 ).title( "Simple Video Texture Test" ) );
} )
