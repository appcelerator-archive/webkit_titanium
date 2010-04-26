//
//  WebScriptElement.mm
//  WebKit
//
//  Created by Marshall on 2/9/09.
//  Copyright 2009 Appcelerator, Inc. All rights reserved.
//

#import "WebScriptElement.h"
#import <WebKit/WebKit.h>
#import <JavaScriptCore/JSContextRef.h>

#import "ScriptElement.h"
#import "ScriptEvaluator.h"
#import "ScriptSourceCode.h"

class EvaluatorAdapter : public WebCore::ScriptEvaluator
{
protected:
    id object;
    
public:
    EvaluatorAdapter(id object) : object(object) {}
    virtual ~EvaluatorAdapter() {}
    
    virtual bool matchesMimeType(const WebCore::String& mimeType) 
    {
        if ([object respondsToSelector:@selector(matchesMimeType:)]) {
            NSString *str = mimeType;
            
            BOOL result = (BOOL) (intptr_t) [object performSelector:@selector(matchesMimeType:) withObject:str];
            return result;
        }
        return false;
    }
    
    virtual void evaluate(const WebCore::String& mimeType,
                          const WebCore::ScriptSourceCode& sourceCode, void *context)
    {
        NSString *mimeTypeStr = mimeType;
        WebCore::String webCoreSourceCodeStr = sourceCode.jsSourceCode().toString();
        NSString *sourceCodeStr = webCoreSourceCodeStr;
        
        SEL evalSelector;
        evalSelector = @selector(evaluate:sourceCode:context:);
        if ([object respondsToSelector:evalSelector]) {
            
            
            NSMethodSignature *sig = [object methodSignatureForSelector:evalSelector];
            NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:sig];
            [invocation setTarget:object];
            [invocation setSelector:evalSelector];
            [invocation setArgument:&mimeTypeStr atIndex:2];
            [invocation setArgument:&sourceCodeStr atIndex:3];
            [invocation setArgument:&context atIndex:4];
            [invocation retainArguments];
            [invocation invoke];
        }
    }
};

@implementation WebScriptElement

+(void) addScriptEvaluator:(id)evaluator
{
    WebCore::ScriptElement::addScriptEvaluator(new EvaluatorAdapter(evaluator));
}


@end
