#ifndef WebScriptEvaluator_h
#define WebScriptEvaluator_h

class WebScriptEvaluator
{
public:
    virtual bool matchesMimeType(const UChar * mimeType) = 0;
    virtual void evaluate(const UChar * sourceCode) = 0;
};

#endif

