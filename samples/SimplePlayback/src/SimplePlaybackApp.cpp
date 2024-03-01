#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/params/Params.h"
#include "cinder/app/RendererGl.h"
#include "cinder/Log.h"
#include "ciWMFVideoPlayer.h"

using namespace ci;
using namespace ci::app;
using namespace std;

using namespace ci;
using namespace ci::app;
using namespace std;

class SimplePlaybackApp : public App {
	public:
		SimplePlaybackApp();
		~SimplePlaybackApp();
		void setup();
		void update();
		void draw();
		void mouseDown(ci::app::MouseEvent event) override;
	private:
		bool mVideoSetup;
		ciWMFVideoPlayer mVideo1;
		ci::params::InterfaceGlRef mParams;
		float mFps;
};

SimplePlaybackApp::SimplePlaybackApp() :
	mFps(0.0f)
{
}

SimplePlaybackApp::~SimplePlaybackApp()
{
}

void SimplePlaybackApp::setup() {
	std::string videoPath = getAssetPath("1.wmv").string();
	mVideo1.loadMovie(videoPath, "Headphones (High Definition Audio Device)");
	mVideo1.play();
	mVideo1.setLoop(true);
	mVideo1.getPresentationEndedSignal().connect([]() {
		ci::app::console() << "Video finished playing!" << std::endl;
		});

	mParams = params::InterfaceGl::create("Params", ivec2(210, 210));
	mParams->addParam<float>("FPS", &mFps, true);
}

void SimplePlaybackApp::mouseDown( MouseEvent event ) {
	CI_LOG_I("Video position: " << mVideo1.getPosition());
}

void SimplePlaybackApp::update() {
	mFps = getAverageFps();
	mVideo1.update();
}

void SimplePlaybackApp::draw() {
	gl::clear(Color(0, 0, 0));
	mVideo1.draw(0, 0);

	mParams->draw();
}

CINDER_APP(SimplePlaybackApp, RendererGl, [](App::Settings* settings) {
		settings->setFrameRate(60.0f);
		settings->prepareWindow(Window::Format().size(1280, 720).title("Simple Playback"));
		settings->setConsoleWindowEnabled(true);
})
