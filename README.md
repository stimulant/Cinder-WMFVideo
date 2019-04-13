Cinder-WMFVideo
===============

Cinder port of [Second Story's](http://www.secondstory.com/) [ofxWMFVideoPlayer addon](https://github.com/secondstory/ofxWMFVideoPlayer).  Allows playback of videos and routing of audio to specific audio devices.

# Revision History

## 2019 April 12
* Merged in changes from Potion's version

## 2016 Sept 29
* Forked Potion's version of Cinder-WMFVideo.
* Added 64-bit versions of the projects

# Installation
- You will need to modify your Cinder build slightly to upgrade to GLee 5.5 which supports the WGL_NV_DX_interop.
    * Copy cinder-src\GLee.c to Cinder\src\cinder\gl\
    * Copy cinder-src\GLee.h to Cinder\include\cinder\gl\
- Make sure you have an up to date video driver.  WGL_NV_DX_interop behaves poorly with older drivers.
- After copying/cloning this repo to your Cinder blocks folder, use Tinderbox to create a new project and select to use the Cinder-WMFVideo block as either reference or copy.

# Use
The ciWMFVideoPlayer class can be used very similarly to qtime::MovieGl.  To load a video, pass a path to the video you want to load to ciWMFVideoPlayer::load.  Videos can be stopped, looped, paused and the current position of the playhead in the video can be retrieved or set.  Currently drawing videos to screen is a bit different than qtime::MovieGl in that you will have to call the ciWMFVideoPlayer::draw method with a screen position and width/height.
