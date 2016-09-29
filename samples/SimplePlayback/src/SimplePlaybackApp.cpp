#include "cinder/app/App.h"
#include "cinder/gl/gl.h"
#include "cinder/params/Params.h"

#include "ciWMFVideoPlayer.h"

class SimplePlaybackApp : public ci::app::App
{
public:
	SimplePlaybackApp();
	~SimplePlaybackApp();

	void draw();
	void mouseDown( ci::app::MouseEvent event ) override;
	void mouseDrag( ci::app::MouseEvent event ) override;
	void setup();
	void update();

private:
	bool						mVideoSetup;
	ciWMFVideoPlayer			mVideo1;

	ci::params::InterfaceGlRef	mParams;
	float						mFps;
};

#include "cinder/app/RendererGl.h"
#include "cinder/Log.h"

using namespace ci;
using namespace ci::app;
using namespace std;

SimplePlaybackApp::SimplePlaybackApp() :
	mFps( 0.0f )
{
}

SimplePlaybackApp::~SimplePlaybackApp()
{
}

void SimplePlaybackApp::draw()
{
	gl::clear( Color( 0, 0, 0 ) );
	mVideo1.draw( 0, 0 );

	mParams->draw();
}

void SimplePlaybackApp::setup()
{
	std::string videoPath = getAssetPath( "demo.mp4" ).string();
	mVideo1.loadMovie( videoPath, "Headphones (High Definition Audio Device)" );
	mVideo1.play();
	mVideo1.setLoop(true);
	mVideo1.getPresentationEndedSignal().connect( []() {
		ci::app::console() << "Video finished playing!" << std::endl;
	} );

	mParams = params::InterfaceGl::create( "Params", ivec2( 210, 210 ) );
	mParams->addParam<float>( "FPS", &mFps, true );
}

void SimplePlaybackApp::mouseDown( MouseEvent event )
{
	CI_LOG_I( "Video position: " << mVideo1.getPosition() );
}

void SimplePlaybackApp::mouseDrag( MouseEvent event )
{
}

void SimplePlaybackApp::update()
{
	mFps = getAverageFps();
	mVideo1.update();
}

CINDER_APP( SimplePlaybackApp, RendererGl, []( App::Settings * settings )
{
	settings->setFrameRate( 60.0f );
	settings->prepareWindow( Window::Format().size( 1280, 720 ).title( "Simple Playback" ) );
} )
