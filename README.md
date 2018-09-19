Cinder-WMFVideo
===============

Cinder port of [Second Story's](http://www.secondstory.com/) [ofxWMFVideoPlayer addon](https://github.com/secondstory/ofxWMFVideoPlayer).  Allows playback of videos and routing of audio to specific audio devices.

# Revision History

## v.Next
- Potion [forked](https://github.com/Potion/Cinder-WMFVideo) 266Hz's version
- Added features / utility functions:
	- Signal when video finishes playing
	- Video Fill types: FILL, ASPECT_FILL and CROP_FIT
	- StepForward 1 frame
	- Switch audio device automatically if not specified 
- Some bug fixes:
	- Prevent crash when window is closed
	- Video orientation (top-down)
	- Video looping issues
- Some cleanup

## v.3.2
- 266Hz [forked](https://github.com/2666hz/Cinder-WMFVideo) Gazoo101's version
- Better messaging when URL fails to open
- Some cleanup

## v.3.1
- Gazoo101 [forked](https://github.com/Gazoo101/Cinder-WMFVideo) djmike's most recent version
- Attempt to merge the fork with [DomAmato's update](https://github.com/DomAmato/ofxWMFVideoPlayer) to the original ofxWMFVideoPlayer
	- Variable rate video playback
	- Asynchronous video loading
- Added guards to keep IMFTrackedSample from being re-defined
- Converted some printed error messages to Cinders new error log system

## v.3
- djmike forked Stimulants version, [updating it in a number of ways](https://github.com/djmike/Cinder-WMFVideo)
- Cinder 0.9.0 compatible
- Video-2-Texture Functionality
- Various other minor features/improvements

## v.2
- Stimulant ported the 0.9 release of ofxWMFVideoPlayer to Cinder (~0.8.6), creating [Cinder-WMFVideo](https://github.com/stimulant/Cinder-WMFVideo)

## V.1
- ofxWMFVideoPlayer release 0.9 by Philippe Laulheret for [Second Story](http://www.secondstory.com/)

# Installation
- You will need to modify your Cinder build slightly to upgrade to GLee 5.5 which supports the WGL_NV_DX_interop.
    * Copy cinder-src\GLee.c to Cinder\src\cinder\gl\
    * Copy cinder-src\GLee.h to Cinder\include\cinder\gl\
- Make sure you have an up to date video driver.  WGL_NV_DX_interop behaves poorly with older drivers.
- After copying/cloning this repo to your Cinder blocks folder, use Tinderbox to create a new project and select to use the Cinder-WMFVideo block as either reference or copy.

# Use
The ciWMFVideoPlayer class can be used very similarly to qtime::MovieGl.  To load a video, pass a path to the video you want to load to ciWMFVideoPlayer::load.  Videos can be stopped, looped, paused and the current position of the playhead in the video can be retrieved or set.  Currently drawing videos to screen is a bit different than qtime::MovieGl in that you will have to call the ciWMFVideoPlayer::draw method with a screen position and width/height.
