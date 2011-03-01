/*      
    WebKitSystemInterface.h
    Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All rights reserved.

    Public header file.
*/

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>

@class QTMovie;
@class QTMovieView;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WKCertificateParseResultSucceeded  = 0,
    WKCertificateParseResultFailed     = 1,
    WKCertificateParseResultPKCS7      = 2,
} WKCertificateParseResult;

CFStringRef WKCopyCFLocalizationPreferredName(CFStringRef localization);
void WKSetDefaultLocalization(CFStringRef localization);

CFStringRef WKSignedPublicKeyAndChallengeString(unsigned keySize, CFStringRef challenge, CFStringRef keyDescription);
WKCertificateParseResult WKAddCertificatesToKeychainFromData(const void *bytes, unsigned length);

NSString *WKGetPreferredExtensionForMIMEType(NSString *type);
NSArray *WKGetExtensionsForMIMEType(NSString *type);
NSString *WKGetMIMETypeForExtension(NSString *extension);

NSDate *WKGetNSURLResponseLastModifiedDate(NSURLResponse *response);
NSTimeInterval WKGetNSURLResponseFreshnessLifetime(NSURLResponse *response);
NSString *WKCopyNSURLResponseStatusLine(NSURLResponse *response);

#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD)
CFArrayRef WKCopyNSURLResponseCertificateChain(NSURLResponse *response);
#endif

CFStringEncoding WKGetWebDefaultCFStringEncoding(void);

void WKSetMetadataURL(NSString *URLString, NSString *referrer, NSString *path);
void WKSetNSURLConnectionDefersCallbacks(NSURLConnection *connection, BOOL defers);

void WKShowKeyAndMain(void);
#ifndef __LP64__
OSStatus WKSyncWindowWithCGAfterMove(WindowRef);
unsigned WKCarbonWindowMask(void);
void *WKGetNativeWindowFromWindowRef(WindowRef);
OSType WKCarbonWindowPropertyCreator(void);
OSType WKCarbonWindowPropertyTag(void);
#endif

typedef id WKNSURLConnectionDelegateProxyPtr;

WKNSURLConnectionDelegateProxyPtr WKCreateNSURLConnectionDelegateProxy(void);

void WKDisableCGDeferredUpdates(void);

Class WKNSURLProtocolClassForRequest(NSURLRequest *request);
void WKSetNSURLRequestShouldContentSniff(NSMutableURLRequest *request, BOOL shouldContentSniff);

void WKSetCookieStoragePrivateBrowsingEnabled(BOOL enabled);

unsigned WKGetNSAutoreleasePoolCount(void);

void WKAdvanceDefaultButtonPulseAnimation(NSButtonCell *button);

NSString *WKMouseMovedNotification(void);
NSString *WKWindowWillOrderOnScreenNotification(void);
void WKSetNSWindowShouldPostEventNotifications(NSWindow *window, BOOL post);

CFTypeID WKGetAXTextMarkerTypeID(void);
CFTypeID WKGetAXTextMarkerRangeTypeID(void);
CFTypeRef WKCreateAXTextMarker(const void *bytes, size_t len);
BOOL WKGetBytesFromAXTextMarker(CFTypeRef textMarker, void *bytes, size_t length);
CFTypeRef WKCreateAXTextMarkerRange(CFTypeRef start, CFTypeRef end);
CFTypeRef WKCopyAXTextMarkerRangeStart(CFTypeRef range);
CFTypeRef WKCopyAXTextMarkerRangeEnd(CFTypeRef range);
void WKAccessibilityHandleFocusChanged(void);
AXUIElementRef WKCreateAXUIElementRef(id element);
void WKUnregisterUniqueIdForElement(id element);


#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD) && !defined(BUILDING_ON_SNOW_LEOPARD)
// Remote Accessibility API.
void WKAXRegisterRemoteApp(void);
void WKAXInitializeElementWithPresenterPid(id, pid_t);
NSData *WKAXRemoteTokenForElement(id);
id WKAXRemoteElementForToken(NSData *);
void WKAXSetWindowForRemoteElement(id remoteWindow, id remoteElement);
void WKAXRegisterRemoteProcess(bool registerProcess, pid_t);
pid_t WKAXRemoteProcessIdentifier(id remoteElement);
#endif

void WKSetUpFontCache(void);

void WKSignalCFReadStreamEnd(CFReadStreamRef stream);
void WKSignalCFReadStreamHasBytes(CFReadStreamRef stream);
void WKSignalCFReadStreamError(CFReadStreamRef stream, CFStreamError *error);

CFReadStreamRef WKCreateCustomCFReadStream(void *(*formCreate)(CFReadStreamRef, void *), 
    void (*formFinalize)(CFReadStreamRef, void *), 
    Boolean (*formOpen)(CFReadStreamRef, CFStreamError *, Boolean *, void *), 
    CFIndex (*formRead)(CFReadStreamRef, UInt8 *, CFIndex, CFStreamError *, Boolean *, void *), 
    Boolean (*formCanRead)(CFReadStreamRef, void *), 
    void (*formClose)(CFReadStreamRef, void *), 
    void (*formSchedule)(CFReadStreamRef, CFRunLoopRef, CFStringRef, void *), 
    void (*formUnschedule)(CFReadStreamRef, CFRunLoopRef, CFStringRef, void *),
    void *context);

void WKDrawCapsLockIndicator(CGContextRef, CGRect);

void WKDrawFocusRing(CGContextRef context, CGColorRef color, int radius);
    // The CG context's current path is the focus ring's path.
    // A color of 0 means "use system focus ring color".
    // A radius of 0 means "use default focus ring radius".

void WKSetDragImage(NSImage *image, NSPoint offset);

void WKDrawBezeledTextFieldCell(NSRect, BOOL enabled);
void WKDrawTextFieldCellFocusRing(NSTextFieldCell*, NSRect);
void WKDrawBezeledTextArea(NSRect, BOOL enabled);
void WKPopupMenu(NSMenu*, NSPoint location, float width, NSView*, int selectedItem, NSFont*);
void WKPopupContextMenu(NSMenu *menu, NSPoint screenLocation);
void WKSendUserChangeNotifications(void);
#ifndef __LP64__
BOOL WKConvertNSEventToCarbonEvent(EventRecord *carbonEvent, NSEvent *cocoaEvent);
void WKSendKeyEventToTSM(NSEvent *theEvent);
void WKCallDrawingNotification(CGrafPtr port, Rect *bounds);
#endif

BOOL WKGetGlyphTransformedAdvances(CGFontRef, NSFont*, CGAffineTransform *m, ATSGlyphRef *glyph, CGSize *advance);
NSFont *WKGetFontInLanguageForRange(NSFont *font, NSString *string, NSRange range);
NSFont *WKGetFontInLanguageForCharacter(NSFont *font, UniChar ch);
void WKSetCGFontRenderingMode(CGContextRef cgContext, NSFont *font);
BOOL WKCGContextGetShouldSmoothFonts(CGContextRef cgContext);

#ifdef BUILDING_ON_TIGER
// CGFontGetAscent, CGFontGetDescent, CGFontGetLeading and CGFontGetUnitsPerEm were not available until Leopard
void WKGetFontMetrics(CGFontRef font, int *ascent, int *descent, int *lineGap, unsigned *unitsPerEm);
// CTFontCopyGraphicsFont was not available until Leopard
CGFontRef WKGetCGFontFromNSFont(NSFont *font);
// CTFontGetPlatformFont was not available until Leopard
ATSUFontID WKGetNSFontATSUFontId(NSFont *font);
// CGFontCopyFullName was not available until Leopard
CFStringRef WKCopyFullFontName(CGFontRef font);
#endif

void WKSetPatternBaseCTM(CGContextRef, CGAffineTransform);
void WKSetPatternPhaseInUserSpace(CGContextRef, CGPoint);
CGAffineTransform WKGetUserToBaseCTM(CGContextRef);

#ifndef BUILDING_ON_TIGER
void WKGetGlyphsForCharacters(CGFontRef, const UniChar[], CGGlyph[], size_t);
#else
typedef void *WKGlyphVectorRef;
OSStatus WKConvertCharToGlyphs(void *styleGroup, const UniChar* characters, unsigned numCharacters, WKGlyphVectorRef glyphs);
OSStatus WKGetATSStyleGroup(ATSUStyle fontStyle, void **styleGroup);
void WKReleaseStyleGroup(void *group);
OSStatus WKInitializeGlyphVector(int count, WKGlyphVectorRef glyphs);
void WKClearGlyphVector(WKGlyphVectorRef glyphs);

int WKGetGlyphVectorNumGlyphs(WKGlyphVectorRef glyphVector);
ATSLayoutRecord *WKGetGlyphVectorFirstRecord(WKGlyphVectorRef glyphVector);
size_t WKGetGlyphVectorRecordSize(WKGlyphVectorRef glyphVector);
#endif

CTLineRef WKCreateCTLineWithUniCharProvider(const UniChar* (*provide)(CFIndex stringIndex, CFIndex* charCount, CFDictionaryRef* attributes, void*), void (*dispose)(const UniChar* chars, void*), void*);
#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD) && !defined(BUILDING_ON_SNOW_LEOPARD)
CTTypesetterRef WKCreateCTTypesetterWithUniCharProviderAndOptions(const UniChar* (*provide)(CFIndex stringIndex, CFIndex* charCount, CFDictionaryRef* attributes, void*), void (*dispose)(const UniChar* chars, void*), void*, CFDictionaryRef options);

CGContextRef WKIOSurfaceContextCreate(IOSurfaceRef, unsigned width, unsigned height, CGColorSpaceRef);
CGImageRef WKIOSurfaceContextCreateImage(CGContextRef context);
#endif

#ifndef __LP64__
NSEvent *WKCreateNSEventWithCarbonEvent(EventRef eventRef);
NSEvent *WKCreateNSEventWithCarbonMouseMoveEvent(EventRef inEvent, NSWindow *window);
NSEvent *WKCreateNSEventWithCarbonClickEvent(EventRef inEvent, WindowRef windowRef);
#endif

CGContextRef WKNSWindowOverrideCGContext(NSWindow *, CGContextRef);
void WKNSWindowRestoreCGContext(NSWindow *, CGContextRef);

void WKNSWindowMakeBottomCornersSquare(NSWindow *);

// These constants match the ones used by ThemeScrollbarArrowStyle (some of the values are private, so we can't just
// use that enum directly).
typedef enum {
    WKThemeScrollBarArrowsSingle     = 0,
    WKThemeScrollBarArrowsLowerRight = 1,
    WKThemeScrollBarArrowsDouble     = 2,
    WKThemeScrollBarArrowsUpperLeft  = 3,
} WKThemeScrollBarArrowStyle;

OSStatus WKThemeDrawTrack(const HIThemeTrackDrawInfo* inDrawInfo, CGContextRef inContext, int inArrowStyle);

#ifdef BUILDING_ON_TIGER
// WKSupportsMultipartXMixedReplace is not required on Leopard as multipart/x-mixed-replace is always handled by NSURLRequest
BOOL WKSupportsMultipartXMixedReplace(NSMutableURLRequest *request);
#endif

BOOL WKCGContextIsBitmapContext(CGContextRef context);

void WKGetWheelEventDeltas(NSEvent *, float *deltaX, float *deltaY, BOOL *continuous);

BOOL WKAppVersionCheckLessThan(NSString *, int, double);

typedef enum {
    WKMovieTypeUnknown,
    WKMovieTypeDownload,
    WKMovieTypeStoredStream,
    WKMovieTypeLiveStream
} WKMovieType;

int WKQTMovieGetType(QTMovie* movie);

BOOL WKQTMovieHasClosedCaptions(QTMovie* movie);
void WKQTMovieSetShowClosedCaptions(QTMovie* movie, BOOL showClosedCaptions);
void WKQTMovieSelectPreferredAlternates(QTMovie* movie);
void WKQTMovieSelectPreferredAlternateTrackForMediaType(QTMovie* movie, NSString* mediaType);

unsigned WKQTIncludeOnlyModernMediaFileTypes(void);
int WKQTMovieDataRate(QTMovie* movie);
float WKQTMovieMaxTimeLoaded(QTMovie* movie);
float WKQTMovieMaxTimeSeekable(QTMovie* movie);
NSString *WKQTMovieMaxTimeLoadedChangeNotification(void);
void WKQTMovieViewSetDrawSynchronously(QTMovieView* view, BOOL sync);
void WKQTMovieDisableComponent(uint32_t[5]);

CFStringRef WKCopyFoundationCacheDirectory(void);

void WKSetVisibleApplicationName(CFStringRef);

typedef enum {
    WKMediaUIPartFullscreenButton   = 0,
    WKMediaUIPartMuteButton,
    WKMediaUIPartPlayButton,
    WKMediaUIPartSeekBackButton,
    WKMediaUIPartSeekForwardButton,
    WKMediaUIPartTimelineSlider,
    WKMediaUIPartTimelineSliderThumb,
    WKMediaUIPartRewindButton,
    WKMediaUIPartSeekToRealtimeButton,
    WKMediaUIPartShowClosedCaptionsButton,
    WKMediaUIPartHideClosedCaptionsButton,
    WKMediaUIPartUnMuteButton,
    WKMediaUIPartPauseButton,
    WKMediaUIPartBackground,
    WKMediaUIPartCurrentTimeDisplay,
    WKMediaUIPartTimeRemainingDisplay,
    WKMediaUIPartStatusDisplay,
    WKMediaUIPartControlsPanel,
    WKMediaUIPartVolumeSliderContainer,
    WKMediaUIPartVolumeSlider,
    WKMediaUIPartVolumeSliderThumb
} WKMediaUIPart;

typedef enum {
    WKMediaControllerThemeClassic   = 1,
    WKMediaControllerThemeQuickTime = 2
} WKMediaControllerThemeStyle;

typedef enum {
    WKMediaControllerFlagDisabled = 1 << 0,
    WKMediaControllerFlagPressed = 1 << 1,
    WKMediaControllerFlagDrawEndCaps = 1 << 3,
    WKMediaControllerFlagFocused = 1 << 4
} WKMediaControllerThemeState;

BOOL WKMediaControllerThemeAvailable(int themeStyle);
BOOL WKHitTestMediaUIPart(int part, int themeStyle, CGRect bounds, CGPoint point);
void WKMeasureMediaUIPart(int part, int themeStyle, CGRect *bounds, CGSize *naturalSize);
void WKDrawMediaUIPart(int part, int themeStyle, CGContextRef context, CGRect rect, unsigned state);
void WKDrawMediaSliderTrack(int themeStyle, CGContextRef context, CGRect rect, float timeLoaded, float currentTime, float duration, unsigned state);
NSView *WKCreateMediaUIBackgroundView(void);

typedef enum {
    WKMediaUIControlTimeline,
    WKMediaUIControlSlider,
    WKMediaUIControlPlayPauseButton,
    WKMediaUIControlExitFullscreenButton,
    WKMediaUIControlRewindButton,
    WKMediaUIControlFastForwardButton,
    WKMediaUIControlVolumeUpButton,
    WKMediaUIControlVolumeDownButton
} WKMediaUIControlType;
    
NSControl *WKCreateMediaUIControl(int controlType);

#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD)
mach_port_t WKInitializeRenderServer(void);
    
@class CALayer;

CALayer *WKMakeRenderLayer(uint32_t contextID);
    
typedef struct __WKSoftwareCARendererRef *WKSoftwareCARendererRef;

WKSoftwareCARendererRef WKSoftwareCARendererCreate(uint32_t contextID);
void WKSoftwareCARendererDestroy(WKSoftwareCARendererRef);
void WKSoftwareCARendererRender(WKSoftwareCARendererRef, CGContextRef, CGRect);

typedef struct __WKCARemoteLayerClientRef *WKCARemoteLayerClientRef;

WKCARemoteLayerClientRef WKCARemoteLayerClientMakeWithServerPort(mach_port_t port);
void WKCARemoteLayerClientInvalidate(WKCARemoteLayerClientRef);
uint32_t WKCARemoteLayerClientGetClientId(WKCARemoteLayerClientRef);
void WKCARemoteLayerClientSetLayer(WKCARemoteLayerClientRef, CALayer *);
CALayer *WKCARemoteLayerClientGetLayer(WKCARemoteLayerClientRef);

@class CARenderer;

void WKCARendererAddChangeNotificationObserver(CARenderer *, void (*callback)(void*), void* context);
void WKCARendererRemoveChangeNotificationObserver(CARenderer *, void (*callback)(void*), void* context);

typedef struct __WKWindowBounceAnimationContext *WKWindowBounceAnimationContextRef;

WKWindowBounceAnimationContextRef WKWindowBounceAnimationContextCreate(NSWindow *window);
void WKWindowBounceAnimationContextDestroy(WKWindowBounceAnimationContextRef context);
void WKWindowBounceAnimationSetAnimationProgress(WKWindowBounceAnimationContextRef context, double animationProgress);

#if defined(__x86_64__)
#import <mach/mig.h>
CFRunLoopSourceRef WKCreateMIGServerSource(mig_subsystem_t subsystem, mach_port_t serverPort);
#endif // defined(__x86_64__)

NSUInteger WKGetInputPanelWindowStyle(void);
UInt8 WKGetNSEventKeyChar(NSEvent *);
#endif // !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD)

@class CAPropertyAnimation;
void WKSetCAAnimationValueFunction(CAPropertyAnimation*, NSString* function);

unsigned WKInitializeMaximumHTTPConnectionCountPerHost(unsigned preferredConnectionCount);
int WKGetHTTPPipeliningPriority(NSURLRequest *);
void WKSetHTTPPipeliningPriority(NSMutableURLRequest *, int priority);

void WKSetCONNECTProxyForStream(CFReadStreamRef, CFStringRef proxyHost, CFNumberRef proxyPort);
void WKSetCONNECTProxyAuthorizationForStream(CFReadStreamRef, CFStringRef proxyAuthorizationString);
CFHTTPMessageRef WKCopyCONNECTProxyResponse(CFReadStreamRef, CFURLRef responseURL);

BOOL WKIsLatchingWheelEvent(NSEvent *);

#ifndef BUILDING_ON_TIGER
void WKWindowSetAlpha(NSWindow *window, float alphaValue);
void WKWindowSetScaledFrame(NSWindow *window, NSRect scaleFrame, NSRect nonScaledFrame);
#endif

#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD)
void WKSyncSurfaceToView(NSView *view);

void WKEnableSettingCursorWhenInBackground(void);

CFDictionaryRef WKNSURLRequestCreateSerializableRepresentation(NSURLRequest *request, CFTypeRef tokenNull);
NSURLRequest *WKNSURLRequestFromSerializableRepresentation(CFDictionaryRef representation, CFTypeRef tokenNull);

CFDictionaryRef WKNSURLResponseCreateSerializableRepresentation(NSURLResponse *response, CFTypeRef tokenNull);
NSURLResponse *WKNSURLResponseFromSerializableRepresentation(CFDictionaryRef representation, CFTypeRef tokenNull);

#ifndef __LP64__
ScriptCode WKGetScriptCodeFromCurrentKeyboardInputSource(void);
#endif

#endif

#if defined(BUILDING_ON_TIGER) || defined(BUILDING_ON_LEOPARD) || defined(BUILDING_ON_SNOW_LEOPARD)
CFIndex WKGetHyphenationLocationBeforeIndex(CFStringRef string, CFIndex index);
#endif

CFArrayRef WKCFURLCacheCopyAllHostNamesInPersistentStore(void);
void WKCFURLCacheDeleteHostNamesInPersistentStore(CFArrayRef hostArray);    

#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD) && !defined(BUILDING_ON_SNOW_LEOPARD)
typedef enum {
    WKSandboxExtensionTypeReadOnly,
    WKSandboxExtensionTypeWriteOnly,    
    WKSandboxExtensionTypeReadWrite,
} WKSandboxExtensionType;
typedef struct __WKSandboxExtension *WKSandboxExtensionRef;

WKSandboxExtensionRef WKSandboxExtensionCreate(const char* path, WKSandboxExtensionType type);
void WKSandboxExtensionDestroy(WKSandboxExtensionRef sandboxExtension);

bool WKSandboxExtensionConsume(WKSandboxExtensionRef sandboxExtension);
bool WKSandboxExtensionInvalidate(WKSandboxExtensionRef sandboxExtension);

const char* WKSandboxExtensionGetSerializedFormat(WKSandboxExtensionRef sandboxExtension, size_t* length);
WKSandboxExtensionRef WKSandboxExtensionCreateFromSerializedFormat(const char* serializationFormat, size_t length);

typedef struct __WKScrollbarPainter *WKScrollbarPainterRef;
WKScrollbarPainterRef WKMakeScrollbarPainter(int controlSize, bool isHorizontal);
WKScrollbarPainterRef WKMakeScrollbarReplacementPainter(WKScrollbarPainterRef oldPainter, int newStyle, int controlSize, bool isHorizontal);
void WKScrollbarPainterSetDelegate(WKScrollbarPainterRef, id scrollbarPainterDelegate);
void WKScrollbarPainterPaint(WKScrollbarPainterRef, bool enabled, double value, CGFloat proportion, CGRect frameRect);
int WKScrollbarThickness(int controlSize);
int WKScrollbarMinimumThumbLength(WKScrollbarPainterRef);
int WKScrollbarMinimumTotalLengthNeededForThumb(WKScrollbarPainterRef);
CGFloat WKScrollbarPainterKnobAlpha(WKScrollbarPainterRef);
void WKSetScrollbarPainterKnobAlpha(WKScrollbarPainterRef, CGFloat);
CGFloat WKScrollbarPainterTrackAlpha(WKScrollbarPainterRef);
void WKSetScrollbarPainterTrackAlpha(WKScrollbarPainterRef, CGFloat);
bool WKScrollbarPainterIsHorizontal(WKScrollbarPainterRef);
void WKScrollbarPainterSetOverlayState(WKScrollbarPainterRef, int overlayScrollerState);

typedef struct __WKScrollbarPainterController *WKScrollbarPainterControllerRef;
WKScrollbarPainterControllerRef WKMakeScrollbarPainterController(id painterControllerDelegate);
void WKSetPainterForPainterController(WKScrollbarPainterControllerRef, WKScrollbarPainterRef, bool isHorizontal);
WKScrollbarPainterRef WKVerticalScrollbarPainterForController(WKScrollbarPainterControllerRef);
WKScrollbarPainterRef WKHorizontalScrollbarPainterForController(WKScrollbarPainterControllerRef);
void WKSetScrollbarPainterControllerStyle(WKScrollbarPainterControllerRef, int newStyle);
void WKContentAreaScrolled(WKScrollbarPainterControllerRef);
void WKContentAreaWillPaint(WKScrollbarPainterControllerRef);
void WKMouseEnteredContentArea(WKScrollbarPainterControllerRef);
void WKMouseExitedContentArea(WKScrollbarPainterControllerRef);
void WKMouseMovedInContentArea(WKScrollbarPainterControllerRef);
void WKWillStartLiveResize(WKScrollbarPainterControllerRef);
void WKContentAreaResized(WKScrollbarPainterControllerRef);
void WKWillEndLiveResize(WKScrollbarPainterControllerRef);
void WKContentAreaDidShow(WKScrollbarPainterControllerRef);
void WKContentAreaDidHide(WKScrollbarPainterControllerRef);

bool WKScrollbarPainterUsesOverlayScrollers(void);

#endif

#ifdef __cplusplus
}
#endif
