#pragma once

// Compatibility umbrella for this checkout, which exposes the openFrameworks
// subsystem headers but does not ship a top-level ofMain.h wrapper.

#include "app/ofAppBaseWindow.h"
#include "app/ofAppGLFWWindow.h"
#include "app/ofAppRunner.h"
#include "app/ofBaseApp.h"
#include "app/ofIcon.h"
#include "app/ofWindowSettings.h"

#include "communication/ofArduino.h"
#include "communication/ofSerial.h"

#include "events/ofEvents.h"

#include "gl/ofBufferObject.h"
#include "gl/ofFbo.h"
#include "gl/ofGLUtils.h"
#include "gl/ofShader.h"
#include "gl/ofTexture.h"
#include "gl/ofVbo.h"
#include "gl/ofVboMesh.h"

#include "graphics/of3dGraphics.h"
#include "graphics/ofBitmapFont.h"
#include "graphics/ofGraphics.h"
#include "graphics/ofImage.h"
#include "graphics/ofPath.h"
#include "graphics/ofPixels.h"
#include "graphics/ofPolyline.h"
#include "graphics/ofTrueTypeFont.h"

#include "math/ofMath.h"
#include "math/ofVectorMath.h"

#include "sound/ofSoundBuffer.h"
#include "sound/ofSoundPlayer.h"
#include "sound/ofSoundStream.h"

#include "types/ofColor.h"
#include "types/ofParameter.h"
#include "types/ofRectangle.h"
#include "types/ofTypes.h"

#include "utils/ofConstants.h"
#include "utils/ofFileUtils.h"
#include "utils/ofJson.h"
#include "utils/ofLog.h"
#include "utils/ofSystemUtils.h"
#include "utils/ofUtils.h"
#include "utils/ofXml.h"

#include "video/ofVideoBaseTypes.h"
#include "video/ofVideoGrabber.h"
#include "video/ofVideoPlayer.h"
