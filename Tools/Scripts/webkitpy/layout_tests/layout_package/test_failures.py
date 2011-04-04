#!/usr/bin/env python
# Copyright (C) 2010 Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Classes for failures that occur during tests."""

import test_expectations

import cPickle


# FIXME: This is backwards.  Each TestFailure subclass should know what
# test_expectation type it corresponds too.  Then this method just
# collects them all from the failure list and returns the worst one.
def determine_result_type(failure_list):
    """Takes a set of test_failures and returns which result type best fits
    the list of failures. "Best fits" means we use the worst type of failure.

    Returns:
      one of the test_expectations result types - PASS, TEXT, CRASH, etc."""

    if not failure_list or len(failure_list) == 0:
        return test_expectations.PASS

    failure_types = [type(f) for f in failure_list]
    if FailureCrash in failure_types:
        return test_expectations.CRASH
    elif FailureTimeout in failure_types:
        return test_expectations.TIMEOUT
    elif (FailureMissingResult in failure_types or
          FailureMissingImage in failure_types or
          FailureMissingImageHash in failure_types):
        return test_expectations.MISSING
    else:
        is_text_failure = FailureTextMismatch in failure_types
        is_image_failure = (FailureImageHashIncorrect in failure_types or
                            FailureImageHashMismatch in failure_types)
        is_reftest_failure = (FailureReftestMismatch in failure_types or
                              FailureReftestMismatchDidNotOccur in failure_types)
        if is_text_failure and is_image_failure:
            return test_expectations.IMAGE_PLUS_TEXT
        elif is_text_failure:
            return test_expectations.TEXT
        elif is_image_failure or is_reftest_failure:
            return test_expectations.IMAGE
        else:
            raise ValueError("unclassifiable set of failures: "
                             + str(failure_types))


class TestFailure(object):
    """Abstract base class that defines the failure interface."""

    @staticmethod
    def loads(s):
        """Creates a TestFailure object from the specified string."""
        return cPickle.loads(s)

    @staticmethod
    def message():
        """Returns a string describing the failure in more detail."""
        raise NotImplementedError

    def __eq__(self, other):
        return self.__class__.__name__ == other.__class__.__name__

    def __ne__(self, other):
        return self.__class__.__name__ != other.__class__.__name__

    def __hash__(self):
        return hash(self.__class__.__name__)

    def dumps(self):
        """Returns the string/JSON representation of a TestFailure."""
        return cPickle.dumps(self)

    def result_html_output(self, filename):
        """Returns an HTML string to be included on the results.html page."""
        raise NotImplementedError

    def should_kill_dump_render_tree(self):
        """Returns True if we should kill DumpRenderTree before the next
        test."""
        return False

    def relative_output_filename(self, filename, modifier):
        """Returns a relative filename inside the output dir that contains
        modifier.

        For example, if filename is fast\dom\foo.html and modifier is
        "-expected.txt", the return value is fast\dom\foo-expected.txt

        Args:
          filename: relative filename to test file
          modifier: a string to replace the extension of filename with

        Return:
          The relative windows path to the output filename
        """
        # FIXME: technically this breaks if files don't use ".ext" to indicate
        # the extension, but passing in a Filesystem object here is a huge
        # hassle.
        return filename[:filename.rfind('.')] + modifier


class ComparisonTestFailure(TestFailure):
    """Base class that produces standard HTML output based on the result of the comparison test.

    Subclasses may commonly choose to override the ResultHtmlOutput, but still
    use the standard OutputLinks.
    """

    # Filename suffixes used by ResultHtmlOutput.
    OUT_FILENAMES = ()

    def output_links(self, filename, out_names):
        """Returns a string holding all applicable output file links.

        Args:
          filename: the test filename, used to construct the result file names
          out_names: list of filename suffixes for the files. If three or more
              suffixes are in the list, they should be [actual, expected, diff,
              wdiff]. Two suffixes should be [actual, expected], and a
              single item is the [actual] filename suffix.
              If out_names is empty, returns the empty string.
        """
        # FIXME: Seems like a bad idea to separate the display name data
        # from the path data by hard-coding the display name here
        # and passing in the path information via out_names.
        #
        # FIXME: Also, we don't know for sure that these files exist,
        # and we shouldn't be creating links to files that don't exist
        # (for example, if we don't actually have wdiff output).
        links = ['']
        uris = [self.relative_output_filename(filename, fn) for
                fn in out_names]
        if len(uris) > 1:
            links.append("<a href='%s'>expected</a>" % uris[1])
        if len(uris) > 0:
            links.append("<a href='%s'>actual</a>" % uris[0])
        if len(uris) > 2:
            links.append("<a href='%s'>diff</a>" % uris[2])
        if len(uris) > 3:
            links.append("<a href='%s'>wdiff</a>" % uris[3])
        if len(uris) > 4:
            links.append("<a href='%s'>pretty diff</a>" % uris[4])
        return ' '.join(links)

    def result_html_output(self, filename):
        return self.message() + self.output_links(filename, self.OUT_FILENAMES)


class FailureTimeout(TestFailure):
    """Test timed out.  We also want to restart DumpRenderTree if this
    happens."""

    def __init__(self, reference_filename=None):
        self.reference_filename = reference_filename

    @staticmethod
    def message():
        return "Test timed out"

    def result_html_output(self, filename):
        if self.reference_filename:
            return "<strong>%s</strong> (occured in <a href=%s>expected html</a>)" % (
                self.message(), self.reference_filename)
        return "<strong>%s</strong>" % self.message()

    def should_kill_dump_render_tree(self):
        return True


class FailureCrash(TestFailure):
    """DumpRenderTree crashed."""

    def __init__(self, reference_filename=None):
        self.reference_filename = reference_filename

    @staticmethod
    def message():
        return "DumpRenderTree crashed"

    def result_html_output(self, filename):
        # FIXME: create a link to the minidump file
        stack = self.relative_output_filename(filename, "-stack.txt")
        if self.reference_filename:
            return "<strong>%s</strong> <a href=%s>stack</a> (occured in <a href=%s>expected html</a>)" % (
                self.message(), stack, self.reference_filename)
        else:
            return "<strong>%s</strong> <a href=%s>stack</a>" % (self.message(), stack)

    def should_kill_dump_render_tree(self):
        return True


class FailureMissingResult(ComparisonTestFailure):
    """Expected result was missing."""
    OUT_FILENAMES = ("-actual.txt",)

    @staticmethod
    def message():
        return "No expected results found"

    def result_html_output(self, filename):
        return ("<strong>%s</strong>" % self.message() +
                self.output_links(filename, self.OUT_FILENAMES))


class FailureTextMismatch(ComparisonTestFailure):
    """Text diff output failed."""
    # Filename suffixes used by ResultHtmlOutput.
    # FIXME: Why don't we use the constants from TestTypeBase here?
    OUT_FILENAMES = ("-actual.txt", "-expected.txt", "-diff.txt",
                     "-wdiff.html", "-pretty-diff.html")

    @staticmethod
    def message():
        return "Text diff mismatch"


class FailureMissingImageHash(ComparisonTestFailure):
    """Actual result hash was missing."""
    # Chrome doesn't know to display a .checksum file as text, so don't bother
    # putting in a link to the actual result.

    @staticmethod
    def message():
        return "No expected image hash found"

    def result_html_output(self, filename):
        return "<strong>%s</strong>" % self.message()


class FailureMissingImage(ComparisonTestFailure):
    """Actual result image was missing."""
    OUT_FILENAMES = ("-actual.png",)

    @staticmethod
    def message():
        return "No expected image found"

    def result_html_output(self, filename):
        return ("<strong>%s</strong>" % self.message() +
                self.output_links(filename, self.OUT_FILENAMES))


class FailureImageHashMismatch(ComparisonTestFailure):
    """Image hashes didn't match."""
    OUT_FILENAMES = ("-actual.png", "-expected.png", "-diff.png")

    @staticmethod
    def message():
        # We call this a simple image mismatch to avoid confusion, since
        # we link to the PNGs rather than the checksums.
        return "Image mismatch"


class FailureImageHashIncorrect(ComparisonTestFailure):
    """Actual result hash is incorrect."""
    # Chrome doesn't know to display a .checksum file as text, so don't bother
    # putting in a link to the actual result.

    @staticmethod
    def message():
        return "Images match, expected image hash incorrect. "

    def result_html_output(self, filename):
        return "<strong>%s</strong>" % self.message()


class FailureReftestMismatch(ComparisonTestFailure):
    """The result didn't match the reference rendering."""

    OUT_FILENAMES = ("-expected.html", "-expected.png", "-actual.png",
                     "-diff.png",)

    @staticmethod
    def message():
        return "Mismatch with reference"

    def output_links(self, filename, out_names):
        links = ['']
        uris = [self.relative_output_filename(filename, output_filename)
                for output_filename in out_names]
        for text, uri in zip(['-expected.html', 'expected', 'actual', 'diff'], uris):
            links.append("<a href='%s'>%s</a>" % (uri, text))
        return ' '.join(links)


class FailureReftestMismatchDidNotOccur(ComparisonTestFailure):
    """Unexpected match between the result and the reference rendering."""

    OUT_FILENAMES = ("-expected-mismatch.html", "-actual.png",)

    @staticmethod
    def message():
        return "Mismatch with the reference did not occur"

    def output_links(self, filename, out_names):
        links = ['']
        uris = [self.relative_output_filename(filename, output_filename)
                for output_filename in out_names]
        for text, uri in zip(['-expected-mismatch.html', 'image'], uris):
            links.append("<a href='%s'>%s</a>" % (uri, text))
        return ' '.join(links)


# Convenient collection of all failure classes for anything that might
# need to enumerate over them all.
ALL_FAILURE_CLASSES = (FailureTimeout, FailureCrash, FailureMissingResult,
                       FailureTextMismatch, FailureMissingImageHash,
                       FailureMissingImage, FailureImageHashMismatch,
                       FailureImageHashIncorrect, FailureReftestMismatch,
                       FailureReftestMismatchDidNotOccur)
