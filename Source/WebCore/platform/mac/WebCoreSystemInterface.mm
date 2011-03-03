/*
 * Copyright 2006, 2007, 2008, 2009, 2010 Apple Computer, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#import "config.h"
#import "WebCoreSystemInterface.h"
#import <Foundation/Foundation.h>

void (*wkAdvanceDefaultButtonPulseAnimation)(NSButtonCell *);
BOOL (*wkCGContextGetShouldSmoothFonts)(CGContextRef);
CFStringRef (*wkCopyCFLocalizationPreferredName)(CFStringRef);
NSString* (*wkCopyNSURLResponseStatusLine)(NSURLResponse*);
NSString* (*wkCreateURLPasteboardFlavorTypeName)(void);
NSString* (*wkCreateURLNPasteboardFlavorTypeName)(void);
void (*wkDrawBezeledTextFieldCell)(NSRect, BOOL enabled);
void (*wkDrawTextFieldCellFocusRing)(NSTextFieldCell*, NSRect);
void (*wkDrawCapsLockIndicator)(CGContextRef, CGRect);
void (*wkDrawBezeledTextArea)(NSRect, BOOL enabled);
void (*wkDrawFocusRing)(CGContextRef, CGColorRef, int radius);
NSFont* (*wkGetFontInLanguageForRange)(NSFont*, NSString*, NSRange);
NSFont* (*wkGetFontInLanguageForCharacter)(NSFont*, UniChar);
BOOL (*wkGetGlyphTransformedAdvances)(CGFontRef, NSFont*, CGAffineTransform*, ATSGlyphRef*, CGSize* advance);
void (*wkDrawMediaSliderTrack)(int themeStyle, CGContextRef context, CGRect rect, float timeLoaded, float currentTime, 
    float duration, unsigned state);
BOOL (*wkHitTestMediaUIPart)(int part, int themeStyle, CGRect bounds, CGPoint point);
void (*wkDrawMediaUIPart)(int part, int themeStyle, CGContextRef context, CGRect rect, unsigned state);
void (*wkMeasureMediaUIPart)(int part, int themeStyle, CGRect *bounds, CGSize *naturalSize);
BOOL (*wkMediaControllerThemeAvailable)(int themeStyle);
NSString* (*wkGetPreferredExtensionForMIMEType)(NSString*);
NSArray* (*wkGetExtensionsForMIMEType)(NSString*);
NSString* (*wkGetMIMETypeForExtension)(NSString*);
NSTimeInterval (*wkGetNSURLResponseCalculatedExpiration)(NSURLResponse *response);
NSDate *(*wkGetNSURLResponseLastModifiedDate)(NSURLResponse *response);
BOOL (*wkGetNSURLResponseMustRevalidate)(NSURLResponse *response);
void (*wkGetWheelEventDeltas)(NSEvent*, float* deltaX, float* deltaY, BOOL* continuous);
void (*wkPopupMenu)(NSMenu*, NSPoint location, float width, NSView*, int selectedItem, NSFont*);
unsigned (*wkQTIncludeOnlyModernMediaFileTypes)(void);
int (*wkQTMovieDataRate)(QTMovie*);
void (*wkQTMovieDisableComponent)(uint32_t[5]);
float (*wkQTMovieMaxTimeLoaded)(QTMovie*);
NSString *(*wkQTMovieMaxTimeLoadedChangeNotification)(void);
float (*wkQTMovieMaxTimeSeekable)(QTMovie*);
int (*wkQTMovieGetType)(QTMovie*);
BOOL (*wkQTMovieHasClosedCaptions)(QTMovie*);
void (*wkQTMovieSetShowClosedCaptions)(QTMovie*, BOOL);
void (*wkQTMovieSelectPreferredAlternates)(QTMovie*);
void (*wkQTMovieViewSetDrawSynchronously)(QTMovieView*, BOOL);
void (*wkSetCGFontRenderingMode)(CGContextRef, NSFont*);
void (*wkSetCookieStoragePrivateBrowsingEnabled)(BOOL);
void (*wkSetDragImage)(NSImage*, NSPoint offset);
void (*wkSetPatternBaseCTM)(CGContextRef, CGAffineTransform);
void (*wkSetPatternPhaseInUserSpace)(CGContextRef, CGPoint point);
CGAffineTransform (*wkGetUserToBaseCTM)(CGContextRef);
void (*wkSetUpFontCache)();
void (*wkSignalCFReadStreamEnd)(CFReadStreamRef stream);
void (*wkSignalCFReadStreamHasBytes)(CFReadStreamRef stream);
void (*wkSignalCFReadStreamError)(CFReadStreamRef stream, CFStreamError *error);
CFReadStreamRef (*wkCreateCustomCFReadStream)(void *(*formCreate)(CFReadStreamRef, void *), 
    void (*formFinalize)(CFReadStreamRef, void *), 
    Boolean (*formOpen)(CFReadStreamRef, CFStreamError *, Boolean *, void *), 
    CFIndex (*formRead)(CFReadStreamRef, UInt8 *, CFIndex, CFStreamError *, Boolean *, void *), 
    Boolean (*formCanRead)(CFReadStreamRef, void *), 
    void (*formClose)(CFReadStreamRef, void *), 
    void (*formSchedule)(CFReadStreamRef, CFRunLoopRef, CFStringRef, void *), 
    void (*formUnschedule)(CFReadStreamRef, CFRunLoopRef, CFStringRef, void *),
    void *context);
void (*wkSetNSURLConnectionDefersCallbacks)(NSURLConnection *, BOOL);
void (*wkSetNSURLRequestShouldContentSniff)(NSMutableURLRequest *, BOOL);
id (*wkCreateNSURLConnectionDelegateProxy)(void);
unsigned (*wkInitializeMaximumHTTPConnectionCountPerHost)(unsigned preferredConnectionCount);
int (*wkGetHTTPPipeliningPriority)(NSURLRequest *);
void (*wkSetHTTPPipeliningPriority)(NSMutableURLRequest *, int priority);
void (*wkSetCONNECTProxyForStream)(CFReadStreamRef, CFStringRef proxyHost, CFNumberRef proxyPort);
void (*wkSetCONNECTProxyAuthorizationForStream)(CFReadStreamRef, CFStringRef proxyAuthorizationString);
CFHTTPMessageRef (*wkCopyCONNECTProxyResponse)(CFReadStreamRef, CFURLRef responseURL);
BOOL (*wkIsLatchingWheelEvent)(NSEvent *);

#ifndef BUILDING_ON_TIGER
void (*wkGetGlyphsForCharacters)(CGFontRef, const UniChar[], CGGlyph[], size_t);
#else
void (*wkClearGlyphVector)(void* glyphs);
OSStatus (*wkConvertCharToGlyphs)(void* styleGroup, const UniChar*, unsigned numCharacters, void* glyphs);
CFStringRef (*wkCopyFullFontName)(CGFontRef font);
OSStatus (*wkGetATSStyleGroup)(ATSUStyle, void** styleGroup);
CGFontRef (*wkGetCGFontFromNSFont)(NSFont*);
void (*wkGetFontMetrics)(CGFontRef, int* ascent, int* descent, int* lineGap, unsigned* unitsPerEm);
ATSLayoutRecord* (*wkGetGlyphVectorFirstRecord)(void* glyphVector);
void* wkGetGlyphsForCharacters;
int (*wkGetGlyphVectorNumGlyphs)(void* glyphVector);
size_t (*wkGetGlyphVectorRecordSize)(void* glyphVector);
OSStatus (*wkInitializeGlyphVector)(int count, void* glyphs);
void (*wkReleaseStyleGroup)(void* group);
ATSUFontID (*wkGetNSFontATSUFontId)(NSFont*);
BOOL (*wkSupportsMultipartXMixedReplace)(NSMutableURLRequest *);
#endif

#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD) && !defined(BUILDING_ON_SNOW_LEOPARD)
void* wkGetHyphenationLocationBeforeIndex;
#else
CFIndex (*wkGetHyphenationLocationBeforeIndex)(CFStringRef string, CFIndex index);
#endif

CTLineRef (*wkCreateCTLineWithUniCharProvider)(const UniChar* (*provide)(CFIndex stringIndex, CFIndex* charCount, CFDictionaryRef* attributes, void*), void (*dispose)(const UniChar* chars, void*), void*);
#if !defined(BUILDING_ON_TIGER) && !defined(BUILDING_ON_LEOPARD) && !defined(BUILDING_ON_SNOW_LEOPARD)
CTTypesetterRef (*wkCreateCTTypesetterWithUniCharProviderAndOptions)(const UniChar* (*provide)(CFIndex stringIndex, CFIndex* charCount, CFDictionaryRef* attributes, void*), void (*dispose)(const UniChar* chars, void*), void*, CFDictionaryRef options);

CGContextRef (*wkIOSurfaceContextCreate)(IOSurfaceRef surface, unsigned width, unsigned height, CGColorSpaceRef colorSpace);
CGImageRef (*wkIOSurfaceContextCreateImage)(CGContextRef context);

WKScrollbarPainterRef (*wkMakeScrollbarPainter)(int controlSize, bool isHorizontal);
WKScrollbarPainterRef (*wkMakeScrollbarReplacementPainter)(WKScrollbarPainterRef oldPainter, int newStyle, int controlSize, bool isHorizontal);
void (*wkScrollbarPainterSetDelegate)(WKScrollbarPainterRef, id scrollbarPainterDelegate);
void (*wkScrollbarPainterPaint)(WKScrollbarPainterRef, bool enabled, double value, CGFloat proportion, CGRect frameRect);
int (*wkScrollbarThickness)(int controlSize);
int (*wkScrollbarMinimumThumbLength)(WKScrollbarPainterRef);
int (*wkScrollbarMinimumTotalLengthNeededForThumb)(WKScrollbarPainterRef);
CGFloat (*wkScrollbarPainterKnobAlpha)(WKScrollbarPainterRef);
void (*wkSetScrollbarPainterKnobAlpha)(WKScrollbarPainterRef, CGFloat);
CGFloat (*wkScrollbarPainterTrackAlpha)(WKScrollbarPainterRef);
void (*wkSetScrollbarPainterTrackAlpha)(WKScrollbarPainterRef, CGFloat);
bool (*wkScrollbarPainterIsHorizontal)(WKScrollbarPainterRef);
void (*wkScrollbarPainterSetOverlayState)(WKScrollbarPainterRef, int overlayScrollerState);

WKScrollbarPainterControllerRef (*wkMakeScrollbarPainterController)(id painterControllerDelegate);
void (*wkSetPainterForPainterController)(WKScrollbarPainterControllerRef, WKScrollbarPainterRef, bool isHorizontal);
WKScrollbarPainterRef (*wkVerticalScrollbarPainterForController)(WKScrollbarPainterControllerRef);
WKScrollbarPainterRef (*wkHorizontalScrollbarPainterForController)(WKScrollbarPainterControllerRef);
void (*wkSetScrollbarPainterControllerStyle)(WKScrollbarPainterControllerRef, int newStyle);
void (*wkContentAreaScrolled)(WKScrollbarPainterControllerRef);
void (*wkContentAreaWillPaint)(WKScrollbarPainterControllerRef);
void (*wkMouseEnteredContentArea)(WKScrollbarPainterControllerRef);
void (*wkMouseExitedContentArea)(WKScrollbarPainterControllerRef);
void (*wkMouseMovedInContentArea)(WKScrollbarPainterControllerRef);
void (*wkWillStartLiveResize)(WKScrollbarPainterControllerRef);
void (*wkContentAreaResized)(WKScrollbarPainterControllerRef);
void (*wkWillEndLiveResize)(WKScrollbarPainterControllerRef);
void (*wkContentAreaDidShow)(WKScrollbarPainterControllerRef);
void (*wkContentAreaDidHide)(WKScrollbarPainterControllerRef);

bool (*wkScrollbarPainterUsesOverlayScrollers)(void);
#endif

void (*wkUnregisterUniqueIdForElement)(id element);
void (*wkAccessibilityHandleFocusChanged)(void);
CFTypeID (*wkGetAXTextMarkerTypeID)(void);
CFTypeID (*wkGetAXTextMarkerRangeTypeID)(void);
CFTypeRef (*wkCreateAXTextMarkerRange)(CFTypeRef start, CFTypeRef end);
CFTypeRef (*wkCopyAXTextMarkerRangeStart)(CFTypeRef range);
CFTypeRef (*wkCopyAXTextMarkerRangeEnd)(CFTypeRef range);
CFTypeRef (*wkCreateAXTextMarker)(const void *bytes, size_t len);
BOOL (*wkGetBytesFromAXTextMarker)(CFTypeRef textMarker, void *bytes, size_t length);
AXUIElementRef (*wkCreateAXUIElementRef)(id element);

CFURLStorageSessionRef (*wkCreatePrivateStorageSession)(CFStringRef);

