#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "../../../src/ciWMFVideoPlayer.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class SimplePlaybackApp : public AppNative {
	bool videoSetup;
	ciWMFVideoPlayer video1;
  public:
	void setup();
	void mouseDown( MouseEvent event );	
	void update();
	void draw();
};

void SimplePlaybackApp::setup()
{
	app::setWindowSize(1280, 720);
	//video1.loadMovie( "1.mp4" );
	video1.loadMovie( "1.wmv", "Headphones (High Definition Audio Device)" );
	video1.play();
}

void SimplePlaybackApp::mouseDown( MouseEvent event )
{
}

void SimplePlaybackApp::update()
{
	video1.update();
}

void SimplePlaybackApp::draw()
{
	// clear out the window with black
	gl::clear( Color( 0, 0, 0 ) ); 

	video1.draw( 0, 0 );
}

CINDER_APP_NATIVE( SimplePlaybackApp, RendererGl )
