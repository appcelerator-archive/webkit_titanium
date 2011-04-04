/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

WebInspector.SourceFile = function(id, script, contentChangedDelegate)
{
    this._scripts = [script];
    this._contentChangedDelegate = contentChangedDelegate;
    if (script.sourceURL)
        this._resource = WebInspector.networkManager.inflightResourceForURL(script.sourceURL) || WebInspector.resourceForURL(script.sourceURL);
    this._requestContentCallbacks = [];

    this.id = id;
    this.url = script.sourceURL;
    this.isExtensionScript = script.worldType === WebInspector.Script.WorldType.EXTENSIONS_WORLD;
    this.messages = [];
    this.breakpoints = {};

    if (this._hasPendingResource())
        this._resource.addEventListener("finished", this.reload.bind(this));
}

WebInspector.SourceFile.prototype = {
    addScript: function(script)
    {
        this._scripts.push(script);
    },

    requestContent: function(callback)
    {
        if (this._contentLoaded) {
            callback(this._mimeType, this._content);
            return;
        }

        this._requestContentCallbacks.push(callback);
        this._requestContent();
    },

    get content()
    {
        return this._content;
    },

    requestSourceMapping: function(callback)
    {
        if (!this._mapping)
            this._mapping = new WebInspector.SourceMapping(this._scripts);
        callback(this._mapping);
    },

    forceLoadContent: function(script)
    {
        if (!this._hasPendingResource())
            return;

        if (!this._concatenatedScripts)
            this._concatenatedScripts = {};
        if (this._concatenatedScripts[script.sourceID])
            return;
        for (var i = 0; i < this._scripts.length; ++i)
            this._concatenatedScripts[this._scripts[i].sourceID] = true;

        this.reload();

        if (!this._contentRequested) {
            this._contentRequested = true;
            this._loadAndConcatenateScriptsContent();
        }
    },

    reload: function()
    {
        if (this._contentLoaded) {
            this._contentLoaded = false;
            this._contentChangedDelegate();
        } else if (this._contentRequested)
            this._reloadContent = true;
        else if (this._requestContentCallbacks.length)
            this._requestContent();
    },

    _requestContent: function()
    {
        if (this._contentRequested)
            return;

        this._contentRequested = true;
        if (this._resource && this._resource.finished)
            this._loadResourceContent(this._resource);
        else if (!this._resource)
            this._loadScriptContent();
        else if (this._concatenatedScripts)
            this._loadAndConcatenateScriptsContent();
        else
            this._contentRequested = false;
    },

    _loadResourceContent: function(resource)
    {
        function didRequestContent(text)
        {
            if (resource.type === WebInspector.Resource.Type.Script)
                this._didRequestContent("text/javascript", text);
            else {
                // WebKit html lexer normalizes line endings and scripts are passed to VM with "\n" line endings.
                // However, resource content has original line endings, so we have to normalize line endings here.
                this._didRequestContent("text/html", text.replace(/\r\n/g, "\n"));
            }
        }
        resource.requestContent(didRequestContent.bind(this));
    },

    _loadScriptContent: function()
    {
        this._scripts[0].requestSource(this._didRequestContent.bind(this, "text/javascript"));
    },

    _loadAndConcatenateScriptsContent: function()
    {
        var scripts = this._scripts.slice();
        scripts.sort(function(x, y) { return x.lineOffset - y.lineOffset || x.columnOffset - y.columnOffset; });
        var sources = [];
        function didRequestSource(source)
        {
            sources.push(source);
            if (sources.length === scripts.length)
                this._concatenateScriptsContent(scripts, sources);
        }
        for (var i = 0; i < scripts.length; ++i)
            scripts[i].requestSource(didRequestSource.bind(this));
    },

    _concatenateScriptsContent: function(scripts, sources)
    {
        var content = "";
        var lineNumber = 0;
        var columnNumber = 0;
        var scriptRanges = [];
        function appendChunk(chunk, script)
        {
            var start = { lineNumber: lineNumber, columnNumber: columnNumber };
            content += chunk;
            var lineEndings = chunk.lineEndings();
            var lineCount = lineEndings.length;
            if (lineCount === 1)
                columnNumber += chunk.length;
            else {
                lineNumber += lineCount - 1;
                columnNumber = lineEndings[lineCount - 1] - lineEndings[lineCount - 2] - 1;
            }
            var end = { lineNumber: lineNumber, columnNumber: columnNumber };
            if (script)
                scriptRanges.push({ start: start, end: end, sourceID: script.sourceID });
        }

        var scriptOpenTag = "<script>";
        var scriptCloseTag = "</script>";
        for (var i = 0; i < scripts.length; ++i) {
            // Fill the gap with whitespace characters.
            while (lineNumber < scripts[i].lineOffset)
                appendChunk("\n");
            while (columnNumber < scripts[i].columnOffset - scriptOpenTag.length)
                appendChunk(" ");

            // Add script tag.
            appendChunk(scriptOpenTag);
            appendChunk(sources[i], scripts[i]);
            appendChunk(scriptCloseTag);
        }

        this._didRequestContent("text/html", content);
    },

    _didRequestContent: function(mimeType, content)
    {
        this._contentLoaded = true;
        this._contentRequested = false;
        this._mimeType = mimeType;
        this._content = content;

        for (var i = 0; i < this._requestContentCallbacks.length; ++i)
            this._requestContentCallbacks[i](mimeType, content);
        this._requestContentCallbacks = [];

        if (this._reloadContent)
            this.reload();
    },

    _hasPendingResource: function()
    {
        return this._resource && !this._resource.finished;
    }
}

WebInspector.FormattedSourceFile = function(sourceFileId, script, contentChangedDelegate, formatter)
{
    WebInspector.SourceFile.call(this, sourceFileId, script, contentChangedDelegate);
    this._formatter = formatter;
}

WebInspector.FormattedSourceFile.prototype = {
    requestSourceMapping: function(callback)
    {
        function didRequestContent()
        {
            callback(this._mapping);
        }
        this.requestContent(didRequestContent.bind(this));
    },

    _didRequestContent: function(mimeType, text)
    {
        function didFormatContent(formattedText, mapping)
        {
            this._mapping = new WebInspector.FormattedSourceMapping(this._scripts, text, formattedText, mapping);
            WebInspector.SourceFile.prototype._didRequestContent.call(this, mimeType, formattedText);
        }
        this._formatter.formatContent(text, this._scripts, didFormatContent.bind(this));
    }
}

WebInspector.FormattedSourceFile.prototype.__proto__ = WebInspector.SourceFile.prototype;

WebInspector.SourceMapping = function(sortedScripts)
{
    this._sortedScripts = sortedScripts;
}

WebInspector.SourceMapping.prototype = {
    scriptLocationToSourceLocation: function(lineNumber, columnNumber)
    {
        return { lineNumber: lineNumber, columnNumber: columnNumber };
    },

    sourceLocationToScriptLocation: function(lineNumber, columnNumber)
    {
        var closestScript = this._sortedScripts[0];
        for (var i = 1; i < this._sortedScripts.length; ++i) {
            script = this._sortedScripts[i];
            if (script.lineOffset > lineNumber || (script.lineOffset === lineNumber && script.columnOffset > columnNumber))
                break;
            closestScript = script;
        }
        return { scriptId: closestScript.sourceID, lineNumber: lineNumber, columnNumber: columnNumber };
    }
}

WebInspector.FormattedSourceMapping = function(sortedScripts, originalText, formattedText, mapping)
{
    WebInspector.SourceMapping.call(this, sortedScripts);
    this._originalLineEndings = originalText.lineEndings();
    this._formattedLineEndings = formattedText.lineEndings();
    this._mapping = mapping;
}

WebInspector.FormattedSourceMapping.prototype = {
    scriptLocationToSourceLocation: function(lineNumber, columnNumber)
    {
        var originalPosition = WebInspector.ScriptFormatter.locationToPosition(this._originalLineEndings, lineNumber, columnNumber);
        var formattedPosition = this._convertPosition(this._mapping.original, this._mapping.formatted, originalPosition);
        return WebInspector.ScriptFormatter.positionToLocation(this._formattedLineEndings, formattedPosition);
    },

    sourceLocationToScriptLocation: function(lineNumber, columnNumber)
    {
        var formattedPosition = WebInspector.ScriptFormatter.locationToPosition(this._formattedLineEndings, lineNumber, columnNumber);
        var originalPosition = this._convertPosition(this._mapping.formatted, this._mapping.original, formattedPosition);
        var originalLocation = WebInspector.ScriptFormatter.positionToLocation(this._originalLineEndings, originalPosition);
        return WebInspector.SourceMapping.prototype.sourceLocationToScriptLocation.call(this, originalLocation.lineNumber, originalLocation.columnNumber);
    },

    _convertPosition: function(positions1, positions2, position)
    {
        var index = positions1.upperBound(position);
        var range1 = positions1[index] - positions1[index - 1];
        var range2 = positions2[index] - positions2[index - 1];
        var position2 = positions2[index - 1];
        if (range1)
            position2 += Math.round((position - positions1[index - 1]) * range2 / range1);
        return position2;
    }
}

WebInspector.FormattedSourceMapping.prototype.__proto__ = WebInspector.SourceMapping.prototype;
