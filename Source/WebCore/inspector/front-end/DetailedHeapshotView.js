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

WebInspector.HeapSnapshotContainmentDataGrid = function()
{
    var columns = {
        object: { title: WebInspector.UIString("Object"), disclosure: true, sortable: true, sort: "ascending" },
        shallowSize: { title: WebInspector.UIString("Shallow Size"), width: "90px", sortable: true },
        retainedSize: { title: WebInspector.UIString("Retained Size"), width: "90px", sortable: true }
    };
    WebInspector.DataGrid.call(this, columns);
    this.addEventListener("sorting changed", this.sort, this);
    this._defaultPopulateCount = 100;
}

WebInspector.HeapSnapshotContainmentDataGrid.prototype = {
    setDataSource: function(snapshotView, snapshot)
    {
        this.snapshotView = snapshotView;
        this.snapshot = snapshot;
        this.snapshotNodeIndex = this.snapshot.rootNodeIndex;
        this._provider = this._createProvider(snapshot, this.snapshotNodeIndex);
        this.sort();
    }
};

MixInSnapshotNodeFunctions(WebInspector.HeapSnapshotObjectNode.prototype, WebInspector.HeapSnapshotContainmentDataGrid.prototype);
WebInspector.HeapSnapshotContainmentDataGrid.prototype.__proto__ = WebInspector.DataGrid.prototype;

WebInspector.HeapSnapshotSortableDataGrid = function(columns)
{
    WebInspector.DataGrid.call(this, columns);
    this.addEventListener("sorting changed", this.sortingChanged, this);
}

WebInspector.HeapSnapshotSortableDataGrid.prototype = {
    sortingChanged: function()
    {
        var sortAscending = this.sortOrder === "ascending";
        var sortColumnIdentifier = this.sortColumnIdentifier;
        var sortFields = this._sortFields(sortColumnIdentifier, sortAscending);

        function SortByTwoFields(nodeA, nodeB)
        {
            var field1 = nodeA[sortFields[0]];
            var field2 = nodeB[sortFields[0]];
            var result = field1 < field2 ? -1 : (field1 > field2 ? 1 : 0);
            if (!sortFields[1])
                result = -result;
            if (result !== 0)
                return result;
            field1 = nodeA[sortFields[2]];
            field2 = nodeB[sortFields[2]];
            result = field1 < field2 ? -1 : (field1 > field2 ? 1 : 0);
            if (!sortFields[3])
                result = -result;
            return result;
        }

        this._performSorting(SortByTwoFields);
    },

    _performSorting: function(sortFunction)
    {
        var children = this.children;
        this.removeChildren();
        children.sort(sortFunction);
        for (var i = 0, l = children.length; i < l; ++i) {
            var child = children[i];
            this.appendChild(child);
            if (child.expanded)
                child.sort();
        }
    }
};

WebInspector.HeapSnapshotSortableDataGrid.prototype.__proto__ = WebInspector.DataGrid.prototype;

WebInspector.HeapSnapshotConstructorsDataGrid = function()
{
    var columns = {
        object: { title: WebInspector.UIString("Constructor"), disclosure: true, sortable: true },
        count: { title: WebInspector.UIString("#"), width: "45px", sortable: true },
        shallowSize: { title: WebInspector.UIString("Shallow Size"), width: "90px", sortable: true },
        retainedSize: { title: WebInspector.UIString("Retained Size"), width: "90px", sort: "descending", sortable: true }
    };
    WebInspector.HeapSnapshotSortableDataGrid.call(this, columns);
}

WebInspector.HeapSnapshotConstructorsDataGrid.prototype = {
    _sortFields: function(sortColumn, sortAscending)
    {
        return {
            object: ["_name", sortAscending, "_count", false],
            count: ["_count", sortAscending, "_name", true],
            shallowSize: ["_shallowSize", sortAscending, "_name", true],
            retainedSize: ["_retainedSize", sortAscending, "_name", true]
        }[sortColumn];
    },

    setDataSource: function(snapshotView, snapshot)
    {
        this.snapshotView = snapshotView;
        this.snapshot = snapshot;
        this.populateChildren();
        this.sortingChanged();
    },

    populateChildren: function()
    {
        var aggregates = this.snapshot.aggregates();
        for (var constructor in aggregates)
            this.appendChild(new WebInspector.HeapSnapshotConstructorNode(this, constructor, aggregates[constructor]));
    }
};

WebInspector.HeapSnapshotConstructorsDataGrid.prototype.__proto__ = WebInspector.HeapSnapshotSortableDataGrid.prototype;

WebInspector.HeapSnapshotDiffDataGrid = function()
{
    var columns = {
        object: { title: WebInspector.UIString("Constructor"), disclosure: true, sortable: true },
        // \xb1 is a "plus-minus" sign.
        addedCount: { title: WebInspector.UIString("# New"), width: "72px", sortable: true, sort: "descending" },
        removedCount: { title: WebInspector.UIString("# Deleted"), width: "72px", sortable: true },
        // \u0394 is a Greek delta letter.
        countDelta: { title: "\u0394", width: "40px", sortable: true },
        addedSize: { title: WebInspector.UIString("Alloc. Size"), width: "72px", sortable: true },
        removedSize: { title: WebInspector.UIString("Freed Size"), width: "72px", sortable: true },
        sizeDelta: { title: "\u0394", width: "72px", sortable: true }
    };
    WebInspector.HeapSnapshotSortableDataGrid.call(this, columns);
}

WebInspector.HeapSnapshotDiffDataGrid.prototype = {
    _sortFields: function(sortColumn, sortAscending)
    {
        return {
            object: ["_name", sortAscending, "_count", false],
            addedCount: ["_addedCount", sortAscending, "_name", true],
            removedCount: ["_removedCount", sortAscending, "_name", true],
            countDelta: ["_countDelta", sortAscending, "_name", true],
            addedSize: ["_addedSize", sortAscending, "_name", true],
            removedSize: ["_removedSize", sortAscending, "_name", true],
            sizeDelta: ["_sizeDelta", sortAscending, "_name", true]
        }[sortColumn];
    },

    setDataSource: function(snapshotView, snapshot)
    {
        this.snapshotView = snapshotView;
        this.snapshot = snapshot;
    },

    setBaseDataSource: function(baseSnapshot)
    {
        this.baseSnapshot = baseSnapshot;
        this.removeChildren();
        if (this.baseSnapshot !== this.snapshot) {
            this.populateChildren();
            this.sortingChanged();
        }
    },

    populateChildren: function()
    {
        var baseClasses = this.baseSnapshot.aggregates(true);
        var classes = this.snapshot.aggregates(true);
        for (var clss in baseClasses) {
            var node = new WebInspector.HeapSnapshotDiffNode(this, clss, baseClasses[clss], classes[clss]);
            if (!node.zeroDiff)
                this.appendChild(node);
        }
        for (clss in classes) {
            if (!(clss in baseClasses)) {
                var node = new WebInspector.HeapSnapshotDiffNode(this, clss, null, classes[clss]);
                if (!node.zeroDiff)
                    this.appendChild(node);
            }
        }
    }
};

WebInspector.HeapSnapshotDiffDataGrid.prototype.__proto__ = WebInspector.HeapSnapshotSortableDataGrid.prototype;

WebInspector.HeapSnapshotDominatorsDataGrid = function()
{
    var columns = {
        object: { title: WebInspector.UIString("Object"), disclosure: true, sortable: true },
        shallowSize: { title: WebInspector.UIString("Shallow Size"), width: "90px", sortable: true },
        retainedSize: { title: WebInspector.UIString("Retained Size"), width: "90px", sort: "descending", sortable: true }
    };
    WebInspector.DataGrid.call(this, columns);
    this.addEventListener("sorting changed", this.sort, this);
    this._defaultPopulateCount = 25;
}

WebInspector.HeapSnapshotDominatorsDataGrid.prototype = {
    setDataSource: function(snapshotView, snapshot)
    {
        this.snapshotView = snapshotView;
        this.snapshot = snapshot;
        this.snapshotNodeIndex = this.snapshot.rootNodeIndex;
        this._provider = this._createProvider(snapshot, this.snapshotNodeIndex);
        this.sort();
    }
};

MixInSnapshotNodeFunctions(WebInspector.HeapSnapshotDominatorObjectNode.prototype, WebInspector.HeapSnapshotDominatorsDataGrid.prototype);
WebInspector.HeapSnapshotDominatorsDataGrid.prototype.__proto__ = WebInspector.DataGrid.prototype;

WebInspector.HeapSnapshotRetainingPathsList = function()
{
    var columns = {
        path: { title: WebInspector.UIString("Retaining path"), sortable: true },
        len: { title: WebInspector.UIString("Length"), width: "90px", sortable: true, sort: "ascending" }
    };
    WebInspector.HeapSnapshotSortableDataGrid.call(this, columns);
}

WebInspector.HeapSnapshotRetainingPathsList.prototype = {
    _sortFields: function(sortColumn, sortAscending)
    {
        return {
            path: ["path", sortAscending, "len", true],
            len: ["len", sortAscending, "path", true]
        }[sortColumn];
    },

    setDataSource: function(snapshotView, snapshot, nodeIndex, prefix)
    {
        this.snapshotView = snapshotView;
        this._prefix = prefix;

        if (this.pathFinder)
            this.searchCancelled();

        this.pathFinder = new WebInspector.HeapSnapshotPathFinder(snapshot, nodeIndex);
        this._setRootChildrenForFinder();

        this.removeChildren();

        this._counter = 0;
        this.showNext(100);
    },

    refresh: function()
    {
        this.removeChildren();
        this._counter = 0;
        delete this._cancel;
        this._setRootChildrenForFinder();
        this.showNext(100);
    },

    showNext: function(pathsCount)
    {
        WebInspector.PleaseWaitMessage.prototype.show(this.element, this.searchCancelled.bind(this, pathsCount));
        window.setTimeout(startSearching.bind(this), 500);

        function startSearching()
        {
            if (this._cancel !== this.pathFinder) {
                if (this._counter < pathsCount) {
                    var result = this.pathFinder.findNext();
                    if (result === null) {
                        WebInspector.PleaseWaitMessage.prototype.hide();
                        if (!this.children.length)
                            this.appendChild(new WebInspector.DataGridNode({path:WebInspector.UIString("Can't find any paths."), len:""}, false));
                        return;
                    } else if (result !== false) {
                        if (this._prefix)
                            result.path = this._prefix + result.path;
                        this.appendChild(new WebInspector.DataGridNode(result, false));
                        ++this._counter;
                    }
                    window.setTimeout(startSearching.bind(this), 0);
                    return;
                } else
                    this.searchCancelled.call(this, pathsCount);
            }
            this._cancel = false;
        }
    },

    searchCancelled: function(pathsCount)
    {
        WebInspector.PleaseWaitMessage.prototype.hide();
        this._counter = 0;
        this._cancel = this.pathFinder;
        if (pathsCount) {
            this.appendChild(new WebInspector.ShowMoreDataGridNode(this.showNext.bind(this), pathsCount));
            this.sortingChanged();
        }
    },

    _setRootChildrenForFinder: function()
    {
        function FilterDOMWindow(node)
        {
            return node.name === "DOMWindow";
        }

        if (this.snapshotView.isTracingToWindowObjects)
            this.pathFinder.updateRoots(FilterDOMWindow);
        else
            this.pathFinder.updateRoots();
    },

    _performSorting: function(sortFunction)
    {
        function DataExtractorWrapper(nodeA, nodeB)
        {
            return sortFunction(nodeA.data, nodeB.data);
        }

        this.sortNodes(DataExtractorWrapper);
    }
};

WebInspector.HeapSnapshotRetainingPathsList.prototype.__proto__ = WebInspector.HeapSnapshotSortableDataGrid.prototype;

WebInspector.DetailedHeapshotView = function(parent, profile)
{
    WebInspector.View.call(this);

    this.element.addStyleClass("detailed-heapshot-view");

    this.parent = parent;
    this.parent.addEventListener("profile added", this._updateBaseOptions, this);

    this.showCountAsPercent = false;
    this.showShallowSizeAsPercent = false;
    this.showRetainedSizeAsPercent = false;

    this.containmentView = new WebInspector.View();
    this.containmentView.element.addStyleClass("view");
    this.containmentDataGrid = new WebInspector.HeapSnapshotContainmentDataGrid();
    this.containmentDataGrid.element.addEventListener("click", this._mouseClickInContainmentGrid.bind(this), true);
    this.containmentView.element.appendChild(this.containmentDataGrid.element);
    this.element.appendChild(this.containmentView.element);

    this.constructorsView = new WebInspector.View();
    this.constructorsView.element.addStyleClass("view");
    this.constructorsDataGrid = new WebInspector.HeapSnapshotConstructorsDataGrid();
    this.constructorsDataGrid.element.addEventListener("click", this._mouseClickInContainmentGrid.bind(this), true);
    this.constructorsView.element.appendChild(this.constructorsDataGrid.element);
    this.element.appendChild(this.constructorsView.element);

    this.diffView = new WebInspector.View();
    this.diffView.element.addStyleClass("view");
    this.diffDataGrid = new WebInspector.HeapSnapshotDiffDataGrid();
    this.diffDataGrid.element.addEventListener("click", this._mouseClickInContainmentGrid.bind(this), true);
    this.diffView.element.appendChild(this.diffDataGrid.element);
    this.element.appendChild(this.diffView.element);

    this.dominatorView = new WebInspector.View();
    this.dominatorView.element.addStyleClass("view");
    this.dominatorDataGrid = new WebInspector.HeapSnapshotDominatorsDataGrid();
    this.dominatorDataGrid.element.addEventListener("click", this._mouseClickInContainmentGrid.bind(this), true);
    this.dominatorView.element.appendChild(this.dominatorDataGrid.element);
    this.element.appendChild(this.dominatorView.element);

    var retainmentView = new WebInspector.View();
    retainmentView.element.addStyleClass("view");
    retainmentView.element.addStyleClass("retaining-paths-view");
    var retainingPathsTitleDiv = document.createElement("div");
    retainingPathsTitleDiv.className = "title";
    var retainingPathsTitle = document.createElement("span");
    retainingPathsTitle.textContent = WebInspector.UIString("Paths from the selected object");
    this.retainingPathsRoot = document.createElement("select");
    this.retainingPathsRoot.className = "status-bar-item";
    this.retainingPathsRoot.addEventListener("change", this._changeRetainingPathsRoot.bind(this), false);
    var toGCRootsTraceOption = document.createElement("option");
    toGCRootsTraceOption.label = WebInspector.UIString("to GC roots");
    var toWindowObjectsTraceOption = document.createElement("option");
    toWindowObjectsTraceOption.label = WebInspector.UIString("to window objects");
    this.retainingPathsRoot.appendChild(toGCRootsTraceOption);
    this.retainingPathsRoot.appendChild(toWindowObjectsTraceOption);
    retainingPathsTitleDiv.appendChild(retainingPathsTitle);
    retainingPathsTitleDiv.appendChild(this.retainingPathsRoot);
    retainmentView.element.appendChild(retainingPathsTitleDiv);
    this.retainmentDataGrid = new WebInspector.HeapSnapshotRetainingPathsList();
    retainmentView.element.appendChild(this.retainmentDataGrid.element);
    retainmentView.visible = true;
    this.element.appendChild(retainmentView.element);

    this.dataGrid = this.constructorsDataGrid;
    this.currentView = this.constructorsView;

    this.viewSelectElement = document.createElement("select");
    this.viewSelectElement.className = "status-bar-item";
    this.viewSelectElement.addEventListener("change", this._changeView.bind(this), false);

    var classesViewOption = document.createElement("option");
    classesViewOption.label = WebInspector.UIString("Summary");
    var diffViewOption = document.createElement("option");
    diffViewOption.label = WebInspector.UIString("Comparison");
    var containmentViewOption = document.createElement("option");
    containmentViewOption.label = WebInspector.UIString("Containment");
    var dominatorsViewOption = document.createElement("option");
    dominatorsViewOption.label = WebInspector.UIString("Dominators");
    this.viewSelectElement.appendChild(classesViewOption);
    this.viewSelectElement.appendChild(diffViewOption);
    this.viewSelectElement.appendChild(containmentViewOption);
    this.viewSelectElement.appendChild(dominatorsViewOption);
    this.views = ["Summary", "Comparison", "Containment", "Dominators"];
    this.views.current = 0;

    this._profileUid = profile.uid;

    this.baseSelectElement = document.createElement("select");
    this.baseSelectElement.className = "status-bar-item hidden";
    this.baseSelectElement.addEventListener("change", this._changeBase.bind(this), false);
    this._updateBaseOptions();

    this.percentButton = new WebInspector.StatusBarButton("", "percent-time-status-bar-item status-bar-item");
    this.percentButton.addEventListener("click", this._percentClicked.bind(this), false);
    this.helpButton = new WebInspector.StatusBarButton("", "heapshot-help-status-bar-item status-bar-item");
    this.helpButton.addEventListener("click", this._helpClicked.bind(this), false);

    var popoverHelper = new WebInspector.PopoverHelper(this.element, this._getHoverAnchor.bind(this), this._showStringContentPopup.bind(this));

    this._loadProfile(this._profileUid, profileCallback.bind(this));

    function profileCallback(profile)
    {
        var list = this._profiles();
        var profileIndex;
        for (var i = 0; i < list.length; ++i)
            if (list[i].uid === profile.uid) {
                profileIndex = i;
                break;
            }
        if (profileIndex > 0)
            this.baseSelectElement.selectedIndex = profileIndex - 1;
        else
            this.baseSelectElement.selectedIndex = profileIndex;
        this.dataGrid.setDataSource(this, this.profileWrapper); 
        this._updatePercentButton();
    }
}

WebInspector.DetailedHeapshotView.prototype = {
    dispose: function()
    {
        if (this._profileWrapper)
            this._profileWrapper.dispose();
        if (this._baseProfileWrapper)
            this._baseProfileWrapper.dispose();
    },

    get statusBarItems()
    {
        return [this.viewSelectElement, this.baseSelectElement, this.percentButton.element, this.helpButton.element];
    },

    get profile()
    {
        return this.parent.getProfile(WebInspector.HeapSnapshotProfileType.TypeId, this._profileUid);
    },

    get profileWrapper()
    {
        if (!this._profileWrapper)
            this._profileWrapper = new WebInspector.HeapSnapshot(this.profile);
        return this._profileWrapper;
    },

    get baseProfile()
    {
        return this.parent.getProfile(WebInspector.HeapSnapshotProfileType.TypeId, this._baseProfileUid);
    },

    get baseProfileWrapper()
    {
        if (!this._baseProfileWrapper) {
            if (this.baseProfile !== this.profile)
                this._baseProfileWrapper = new WebInspector.HeapSnapshot(this.baseProfile);
            else
                this._baseProfileWrapper = this.profileWrapper;
        }
        return this._baseProfileWrapper;
    },

    show: function(parentElement)
    {
        WebInspector.View.prototype.show.call(this, parentElement);
        if (!this.profile._loaded)
            this._loadProfile(this._profileUid, profileCallback1.bind(this));
        else
            profileCallback1.call(this, this.profile);

        function profileCallback1(profile) {
            this.profileWrapper.restore(profile);
            if (this.baseProfile && !this.baseProfile._loaded)
                this._loadProfile(this._baseProfileUid, profileCallback2.bind(this));
            else
                profileCallback2.call(this, this.baseProfile);
        }

        function profileCallback2(profile) {
            if (profile)
                this.baseProfileWrapper.restore(profile);
            this.currentView.show();
            this.dataGrid.updateWidths();
        }
    },

    hide: function()
    {
        WebInspector.View.prototype.hide.call(this);
        this._currentSearchResultIndex = -1;
    },

    resize: function()
    {
        if (this.dataGrid)
            this.dataGrid.updateWidths();
    },

    refreshShowAsPercents: function()
    {
        this._updatePercentButton();
        this.refreshVisibleData();
    },

    searchCanceled: function()
    {
        if (this._searchResults) {
            for (var i = 0; i < this._searchResults.length; ++i) {
                var node = this._searchResults[i].node;
                delete node._searchMatched;
                node.refresh();
            }
        }

        delete this._searchFinishedCallback;
        this._currentSearchResultIndex = -1;
        this._searchResults = [];
    },

    performSearch: function(query, finishedCallback)
    {
        // Call searchCanceled since it will reset everything we need before doing a new search.
        this.searchCanceled();

        query = query.trim();

        if (!query.length)
            return;
        if (this.currentView !== this.constructorsView && this.currentView !== this.diffView)
            return;

        this._searchFinishedCallback = finishedCallback;

        function matchesByName(gridNode) {
            return ("name" in gridNode) && gridNode.name.hasSubstring(query, true);
        }

        function matchesById(gridNode) {
            return ("snapshotNodeId" in gridNode) && gridNode.snapshotNodeId === query;
        }

        var matchPredicate;
        if (query.charAt(0) !== "@")
            matchPredicate = matchesByName;
        else {
            query = parseInt(query.substring(1), 10);
            matchPredicate = matchesById;
        }

        function matchesQuery(gridNode)
        {
            delete gridNode._searchMatched;
            if (matchPredicate(gridNode)) {
                gridNode._searchMatched = true;
                gridNode.refresh();
                return true;
            }
            return false;
        }

        var current = this.dataGrid.children[0];
        var depth = 0;
        var info = {};

        // Restrict to type nodes and instances.
        const maxDepth = 1;

        while (current) {
            if (matchesQuery(current))
                this._searchResults.push({ node: current });
            current = current.traverseNextNode(false, null, (depth >= maxDepth), info);
            depth += info.depthChange;
        }

        finishedCallback(this, this._searchResults.length);
    },

    jumpToFirstSearchResult: function()
    {
        if (!this._searchResults || !this._searchResults.length)
            return;
        this._currentSearchResultIndex = 0;
        this._jumpToSearchResult(this._currentSearchResultIndex);
    },

    jumpToLastSearchResult: function()
    {
        if (!this._searchResults || !this._searchResults.length)
            return;
        this._currentSearchResultIndex = (this._searchResults.length - 1);
        this._jumpToSearchResult(this._currentSearchResultIndex);
    },

    jumpToNextSearchResult: function()
    {
        if (!this._searchResults || !this._searchResults.length)
            return;
        if (++this._currentSearchResultIndex >= this._searchResults.length)
            this._currentSearchResultIndex = 0;
        this._jumpToSearchResult(this._currentSearchResultIndex);
    },

    jumpToPreviousSearchResult: function()
    {
        if (!this._searchResults || !this._searchResults.length)
            return;
        if (--this._currentSearchResultIndex < 0)
            this._currentSearchResultIndex = (this._searchResults.length - 1);
        this._jumpToSearchResult(this._currentSearchResultIndex);
    },

    showingFirstSearchResult: function()
    {
        return (this._currentSearchResultIndex === 0);
    },

    showingLastSearchResult: function()
    {
        return (this._searchResults && this._currentSearchResultIndex === (this._searchResults.length - 1));
    },

    _jumpToSearchResult: function(index)
    {
        var searchResult = this._searchResults[index];
        if (!searchResult)
            return;

        var node = searchResult.node;
        node.reveal();
        node.select();
    },

    refreshVisibleData: function()
    {
        var child = this.dataGrid.children[0];
        while (child) {
            child.refresh();
            child = child.traverseNextNode(false, null, true);
        }
    },

    _changeBase: function()
    {
        if (this._baseProfileUid === this._profiles()[this.baseSelectElement.selectedIndex].uid)
            return;

        this._baseProfileUid = this._profiles()[this.baseSelectElement.selectedIndex].uid;
        this._loadProfile(this._baseProfileUid, baseProfileLoaded.bind(this));

        function baseProfileLoaded(profile)
        {
            delete this._baseProfileWrapper;
            this.baseProfile._lastShown = Date.now();
            WebInspector.PleaseWaitMessage.prototype.startAction(this.currentView.element, showDiffData.bind(this));
        }

        function showDiffData()
        {
            this.diffDataGrid.setBaseDataSource(this.baseProfileWrapper);
        }

        if (!this.currentQuery || !this._searchFinishedCallback || !this._searchResults)
            return;

        // The current search needs to be performed again. First negate out previous match
        // count by calling the search finished callback with a negative number of matches.
        // Then perform the search again with the same query and callback.
        this._searchFinishedCallback(this, -this._searchResults.length);
        this.performSearch(this.currentQuery, this._searchFinishedCallback);
    },

    _profiles: function()
    {
        return WebInspector.panels.profiles.getProfiles(WebInspector.HeapSnapshotProfileType.TypeId);
    },

    _loadProfile: function(profileUid, callback)
    {
        WebInspector.panels.profiles.loadHeapSnapshot(profileUid, callback);
    },

    isDetailedSnapshot: function(snapshot)
    {
        var s = new WebInspector.HeapSnapshot(snapshot);
        for (var iter = s.rootNode.edges; iter.hasNext(); iter.next())
            if (iter.edge.node.name === "(GC roots)")
                return true;
        return false;
    },

    processLoadedSnapshot: function(profile, snapshot)
    {
        profile.nodes = snapshot.nodes;
        profile.strings = snapshot.strings;
        var s = new WebInspector.HeapSnapshot(profile);
        profile.sideBarElement.subtitle = Number.bytesToString(s.totalSize);
    },

    _mouseClickInContainmentGrid: function(event)
    {
        var cell = event.target.enclosingNodeOrSelfWithNodeName("td");
        if (!cell || (!cell.hasStyleClass("object-column") && !cell.hasStyleClass("shallowSize-column") && !cell.hasStyleClass("retainedSize-column")))
            return;
        var row = event.target.enclosingNodeOrSelfWithNodeName("tr");
        if (!row)
            return;
        var nodeItem = row._dataGridNode;
        if (!nodeItem || nodeItem.isEventWithinDisclosureTriangle(event) || !nodeItem.snapshotNodeIndex)
            return;

        this.retainmentDataGrid.setDataSource(this, nodeItem.isDeletedNode ? nodeItem.dataGrid.baseSnapshot : nodeItem.dataGrid.snapshot, nodeItem.snapshotNodeIndex, nodeItem.isDeletedNode ? this.baseSelectElement.childNodes[this.baseSelectElement.selectedIndex].label + " | " : "");
    },

    _changeView: function(event)
    {
        if (!event || !this._profileUid)
            return;
        if (event.target.selectedIndex === this.views.current)
            return;

        this.views.current = event.target.selectedIndex;
        this.currentView.hide();
        if (this.views[this.views.current] === "Containment") {
            this.currentView = this.containmentView;
            this.dataGrid = this.containmentDataGrid;
        } else if (this.views[this.views.current] === "Summary") {
            this.currentView = this.constructorsView;
            this.dataGrid = this.constructorsDataGrid;
        } else if (this.views[this.views.current] === "Comparison") {
            this.currentView = this.diffView;
            this.dataGrid = this.diffDataGrid;
        } else if (this.views[this.views.current] === "Dominators") {
            this.currentView = this.dominatorView;
            this.dataGrid = this.dominatorDataGrid;
        }
        this.currentView.show();
        this.refreshVisibleData();
        if (this.currentView === this.diffView) {
            this.baseSelectElement.removeStyleClass("hidden");
            if (!this.dataGrid.snapshotView) {
                this.dataGrid.setDataSource(this, this.profileWrapper);
                this._changeBase();
            }
        } else {
            this.baseSelectElement.addStyleClass("hidden");
            if (!this.dataGrid.snapshotView)
                WebInspector.PleaseWaitMessage.prototype.startAction(this.currentView.element, loadData.bind(this));
        }

        function loadData()
        {
            this.dataGrid.setDataSource(this, this.profileWrapper);
        }

        if (!this.currentQuery || !this._searchFinishedCallback || !this._searchResults)
            return;

        // The current search needs to be performed again. First negate out previous match
        // count by calling the search finished callback with a negative number of matches.
        // Then perform the search again the with same query and callback.
        this._searchFinishedCallback(this, -this._searchResults.length);
        this.performSearch(this.currentQuery, this._searchFinishedCallback);
    },

    _changeRetainingPathsRoot: function(event)
    {
        if (!event)
            return;
        this.retainmentDataGrid.refresh();
    },

    _getHoverAnchor: function(target)
    {
        var span = target.enclosingNodeOrSelfWithNodeName("span");
        if (!span || !span.hasStyleClass("console-formatted-string"))
            return;
        var row = target.enclosingNodeOrSelfWithNodeName("tr");
        if (!row)
            return;
        var gridNode = row._dataGridNode;
        if (!gridNode.snapshotNodeIndex)
            return;
        span.snapshotNodeIndex = gridNode.snapshotNodeIndex;
        return span;
    },

    get isTracingToWindowObjects()
    {
        return this.retainingPathsRoot.selectedIndex === 1;
    },

    get _isShowingAsPercent()
    {
        return this.showCountAsPercent && this.showShallowSizeAsPercent && this.showRetainedSizeAsPercent;
    },

    _percentClicked: function(event)
    {
        var currentState = this._isShowingAsPercent;
        this.showCountAsPercent = !currentState;
        this.showShallowSizeAsPercent = !currentState;
        this.showRetainedSizeAsPercent = !currentState;
        this.refreshShowAsPercents();
    },

    _showStringContentPopup: function(span)
    {
        var snapshotNode = new WebInspector.HeapSnapshotNode(this.profileWrapper, span.snapshotNodeIndex);
        var stringContentElement = document.createElement("span");
        stringContentElement.className = "monospace console-formatted-string";
        stringContentElement.style.whiteSpace = "pre";
        stringContentElement.textContent = "\"" + snapshotNode.name + "\"";
        var popover = new WebInspector.Popover(stringContentElement);
        popover.show(span);
        return popover;
    },

    _helpClicked: function(event)
    {
        if (!this.helpPopover) {
            var refTypes = ["a:", "console-formatted-name", WebInspector.UIString("property"),
                            "0:", "console-formatted-name", WebInspector.UIString("element"),
                            "a:", "console-formatted-number", WebInspector.UIString("context var"),
                            "a:", "console-formatted-null", WebInspector.UIString("system prop")];
            var objTypes = [" a ", "console-formatted-object", "Object",
                            "\"a\"", "console-formatted-string", "String",
                            "/a/", "console-formatted-string", "RegExp",
                            "a()", "console-formatted-function", "Function",
                            "a[]", "console-formatted-object", "Array",
                            "num", "console-formatted-number", "Number",
                            " a ", "console-formatted-null", "System"];

            var contentElement = document.createElement("table");
            contentElement.className = "heapshot-help";
            var headerRow = document.createElement("tr");
            var propsHeader = document.createElement("th");
            propsHeader.textContent = WebInspector.UIString("Property types:");
            headerRow.appendChild(propsHeader);
            var objsHeader = document.createElement("th");
            objsHeader.textContent = WebInspector.UIString("Object types:");
            headerRow.appendChild(objsHeader);
            contentElement.appendChild(headerRow);
            var len = Math.max(refTypes.length, objTypes.length);
            for (var i = 0; i < len; i += 3) {
                var row = document.createElement("tr");
                var refCell = document.createElement("td");
                if (refTypes[i])
                    appendHelp(refTypes, i, refCell);
                row.appendChild(refCell);
                var objCell = document.createElement("td");
                if (objTypes[i])
                    appendHelp(objTypes, i, objCell);
                row.appendChild(objCell);
                contentElement.appendChild(row);
            }
            this.helpPopover = new WebInspector.Popover(contentElement);

            function appendHelp(help, index, cell)
            {
                var div = document.createElement("div");
                div.className = "source-code event-properties";
                var name = document.createElement("span");
                name.textContent = help[index];
                name.className = help[index + 1];
                div.appendChild(name);
                var desc = document.createElement("span");
                desc.textContent = " " + help[index + 2];
                div.appendChild(desc);
                cell.appendChild(div);
            }
        }
        if (this.helpPopover.visible)
            this.helpPopover.hide();
        else
            this.helpPopover.show(this.helpButton.element);
    },

    _updateBaseOptions: function()
    {
        var list = this._profiles();
        // We're assuming that snapshots can only be added.
        if (this.baseSelectElement.length === list.length)
            return;

        for (var i = this.baseSelectElement.length, n = list.length; i < n; ++i) {
            var baseOption = document.createElement("option");
            var title = list[i].title;
            if (!title.indexOf(UserInitiatedProfileName))
                title = WebInspector.UIString("Snapshot %d", title.substring(UserInitiatedProfileName.length + 1));
            baseOption.label = title;
            this.baseSelectElement.appendChild(baseOption);
        }
    },

    _updatePercentButton: function()
    {
        if (this._isShowingAsPercent) {
            this.percentButton.title = WebInspector.UIString("Show absolute counts and sizes.");
            this.percentButton.toggled = true;
        } else {
            this.percentButton.title = WebInspector.UIString("Show counts and sizes as percentages.");
            this.percentButton.toggled = false;
        }
    }
};

WebInspector.DetailedHeapshotView.prototype.__proto__ = WebInspector.View.prototype;

WebInspector.DetailedHeapshotView.prototype.showHiddenData = true;

WebInspector.DetailedHeapshotProfileType = function()
{
    WebInspector.ProfileType.call(this, WebInspector.HeapSnapshotProfileType.TypeId, WebInspector.UIString("HEAP SNAPSHOTS"));
}

WebInspector.DetailedHeapshotProfileType.prototype = {
    get buttonTooltip()
    {
        return WebInspector.UIString("Take heap snapshot.");
    },

    get buttonStyle()
    {
        return "heap-snapshot-status-bar-item status-bar-item";
    },

    buttonClicked: function()
    {
        WebInspector.panels.profiles.takeHeapSnapshot(true);
    },

    get welcomeMessage()
    {
        return WebInspector.UIString("Get a heap snapshot by pressing the %s button on the status bar.");
    },

    createSidebarTreeElementForProfile: function(profile)
    {
        return new WebInspector.ProfileSidebarTreeElement(profile, WebInspector.UIString("Snapshot %d"), "heap-snapshot-sidebar-tree-item");
    },

    createView: function(profile)
    {
        return new WebInspector.DetailedHeapshotView(WebInspector.panels.profiles, profile);
    }
}

WebInspector.DetailedHeapshotProfileType.prototype.__proto__ = WebInspector.ProfileType.prototype;
