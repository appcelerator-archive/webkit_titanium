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

"""Rebaselining tool that automatically produces baselines for all platforms.

The script does the following for each platform specified:
  1. Compile a list of tests that need rebaselining.
  2. Download test result archive from buildbot for the platform.
  3. Extract baselines from the archive file for all identified files.
  4. Add new baselines to SVN repository.
  5. For each test that has been rebaselined, remove this platform option from
     the test in test_expectation.txt. If no other platforms remain after
     removal, delete the rebaselined test from the file.

At the end, the script generates a html that compares old and new baselines.
"""

import copy
import logging
import optparse
import re
import sys
import time

from webkitpy.common.checkout import scm
from webkitpy.common.system import zipfileset
from webkitpy.common.system import path
from webkitpy.common.system import urlfetcher
from webkitpy.common.system.executive import ScriptError

from webkitpy.layout_tests import port
from webkitpy.layout_tests.layout_package import test_expectations

_log = logging.getLogger(__name__)

BASELINE_SUFFIXES = ['.txt', '.png', '.checksum']
REBASELINE_PLATFORM_ORDER = ['mac', 'win', 'win-xp', 'win-vista', 'linux']
ARCHIVE_DIR_NAME_DICT = {'win': 'Webkit_Win__deps_',
                         'win-vista': 'webkit-dbg-vista',
                         'win-xp': 'Webkit_Win__deps_',
                         'mac': 'Webkit_Mac10_5__deps_',
                         'linux': 'Webkit_Linux__deps_',

                         'win-canary': 'Webkit_Win',
                         'win-vista-canary': 'webkit-dbg-vista',
                         'win-xp-canary': 'Webkit_Win',
                         'mac-canary': 'Webkit_Mac10_5',
                         'linux-canary': 'Webkit_Linux',

                         'gpu-mac-canary': 'Webkit_Mac10_5_-_GPU',
                         'gpu-win-canary': 'Webkit_Win_-_GPU',
                         'gpu-linux-canary': 'Webkit_Linux_-_GPU',
}


def log_dashed_string(text, platform, logging_level=logging.INFO):
    """Log text message with dashes on both sides."""

    msg = text
    if platform:
        msg += ': ' + platform
    if len(msg) < 78:
        dashes = '-' * ((78 - len(msg)) / 2)
        msg = '%s %s %s' % (dashes, msg, dashes)

    if logging_level == logging.ERROR:
        _log.error(msg)
    elif logging_level == logging.WARNING:
        _log.warn(msg)
    else:
        _log.info(msg)


def setup_html_directory(filesystem, parent_directory):
    """Setup the directory to store html results.

       All html related files are stored in the "rebaseline_html" subdirectory of
       the parent directory. The path to the created directory is returned.
    """

    if not parent_directory:
        parent_directory = str(filesystem.mkdtemp())
    else:
        filesystem.maybe_make_directory(parent_directory)

    html_directory = filesystem.join(parent_directory, 'rebaseline_html')
    _log.info('Html directory: "%s"', html_directory)

    if filesystem.exists(html_directory):
        filesystem.rmtree(html_directory)
        _log.info('Deleted html directory: "%s"', html_directory)

    filesystem.maybe_make_directory(html_directory)
    return html_directory


def get_result_file_fullpath(filesystem, html_directory, baseline_filename, platform,
                             result_type):
    """Get full path of the baseline result file.

    Args:
      filesystem: wrapper object
      html_directory: directory that stores the html related files.
      baseline_filename: name of the baseline file.
      platform: win, linux or mac
      result_type: type of the baseline result: '.txt', '.png'.

    Returns:
      Full path of the baseline file for rebaselining result comparison.
    """

    base, ext = filesystem.splitext(baseline_filename)
    result_filename = '%s-%s-%s%s' % (base, platform, result_type, ext)
    fullpath = filesystem.join(html_directory, result_filename)
    _log.debug('  Result file full path: "%s".', fullpath)
    return fullpath


class Rebaseliner(object):
    """Class to produce new baselines for a given platform."""

    REVISION_REGEX = r'<a href=\"(\d+)/\">'

    def __init__(self, running_port, target_port, platform, options, url_fetcher, zip_factory, scm):
        """
        Args:
            running_port: the Port the script is running on.
            target_port: the Port the script uses to find port-specific
                configuration information like the test_expectations.txt
                file location and the list of test platforms.
            platform: the test platform to rebaseline
            options: the command-line options object.
            url_fetcher: object that can fetch objects from URLs
            zip_factory: optional object that can fetch zip files from URLs
            scm: scm object for adding new baselines
        """
        self._platform = platform
        self._options = options
        self._port = running_port
        self._filesystem = running_port._filesystem
        self._target_port = target_port

        # FIXME: See the comments in chromium_{win,mac}.py about why we need
        # the 'rebaselining' keyword.
        self._rebaseline_port = port.get(
            self._target_port.test_platform_name_to_name(platform), options,
            filesystem=self._filesystem, rebaselining=True)
        self._rebaselining_tests = []
        self._rebaselined_tests = []

        # Create tests and expectations helper which is used to:
        #   -. compile list of tests that need rebaselining.
        #   -. update the tests in test_expectations file after rebaseline
        #      is done.
        expectations_str = self._rebaseline_port.test_expectations()
        self._test_expectations = \
            test_expectations.TestExpectations(self._rebaseline_port,
                                               None,
                                               expectations_str,
                                               self._rebaseline_port.test_configuration(),
                                               False)
        self._url_fetcher = url_fetcher
        self._zip_factory = zip_factory
        self._scm = scm

    def run(self, backup):
        """Run rebaseline process."""

        log_dashed_string('Compiling rebaselining tests', self._platform)
        if not self._compile_rebaselining_tests():
            return True

        log_dashed_string('Downloading archive', self._platform)
        archive_file = self._download_buildbot_archive()
        _log.info('')
        if not archive_file:
            _log.error('No archive found.')
            return False

        log_dashed_string('Extracting and adding new baselines',
                          self._platform)
        if not self._extract_and_add_new_baselines(archive_file):
            archive_file.close()
            return False

        archive_file.close()

        log_dashed_string('Updating rebaselined tests in file',
                          self._platform)
        self._update_rebaselined_tests_in_file(backup)
        _log.info('')

        if len(self._rebaselining_tests) != len(self._rebaselined_tests):
            _log.warning('NOT ALL TESTS THAT NEED REBASELINING HAVE BEEN '
                         'REBASELINED.')
            _log.warning('  Total tests needing rebaselining: %d',
                         len(self._rebaselining_tests))
            _log.warning('  Total tests rebaselined: %d',
                         len(self._rebaselined_tests))
            return False

        _log.warning('All tests needing rebaselining were successfully '
                     'rebaselined.')

        return True

    def get_rebaselining_tests(self):
        return self._rebaselining_tests

    def _compile_rebaselining_tests(self):
        """Compile list of tests that need rebaselining for the platform.

        Returns:
          List of tests that need rebaselining or
          None if there is no such test.
        """

        self._rebaselining_tests = \
            self._test_expectations.get_rebaselining_failures()
        if not self._rebaselining_tests:
            _log.warn('No tests found that need rebaselining.')
            return None

        _log.info('Total number of tests needing rebaselining '
                  'for "%s": "%d"', self._platform,
                  len(self._rebaselining_tests))

        test_no = 1
        for test in self._rebaselining_tests:
            _log.info('  %d: %s', test_no, test)
            test_no += 1

        return self._rebaselining_tests

    def _get_latest_revision(self, url):
        """Get the latest layout test revision number from buildbot.

        Args:
          url: Url to retrieve layout test revision numbers.

        Returns:
          latest revision or
          None on failure.
        """

        _log.debug('Url to retrieve revision: "%s"', url)

        content = self._url_fetcher.fetch(url)

        revisions = re.findall(self.REVISION_REGEX, content)
        if not revisions:
            _log.error('Failed to find revision, content: "%s"', content)
            return None

        revisions.sort(key=int)
        _log.info('Latest revision: "%s"', revisions[len(revisions) - 1])
        return revisions[len(revisions) - 1]

    def _get_archive_dir_name(self, platform):
        """Get name of the layout test archive directory.

        Returns:
          Directory name or
          None on failure
        """

        if platform in ARCHIVE_DIR_NAME_DICT:
            return ARCHIVE_DIR_NAME_DICT[platform]
        else:
            _log.error('Cannot find platform key %s in archive '
                       'directory name dictionary', platform)
            return None

    def _get_archive_url(self):
        """Generate the url to download latest layout test archive.

        Returns:
          Url to download archive or
          None on failure
        """

        if self._options.force_archive_url:
            return self._options.force_archive_url

        platform = self._platform
        if self._options.webkit_canary:
            platform += '-canary'
        if self._options.gpu:
            platform = 'gpu-' + platform

        dir_name = self._get_archive_dir_name(platform)
        if not dir_name:
            return None

        _log.debug('Buildbot platform dir name: "%s"', dir_name)

        url_base = '%s/%s/' % (self._options.archive_url, dir_name)
        latest_revision = self._get_latest_revision(url_base)
        if latest_revision is None or latest_revision <= 0:
            return None
        archive_url = ('%s%s/layout-test-results.zip' % (url_base,
                                                         latest_revision))
        _log.info('Archive url: "%s"', archive_url)
        return archive_url

    def _download_buildbot_archive(self):
        """Download layout test archive file from buildbot and return a handle to it."""
        url = self._get_archive_url()
        if url is None:
            return None

        archive_file = zipfileset.ZipFileSet(url, filesystem=self._filesystem,
                                         zip_factory=self._zip_factory)
        _log.info('Archive downloaded')
        return archive_file

    def _extract_and_add_new_baselines(self, zip_file):
        """Extract new baselines from the zip file and add them to SVN repository.

        Returns:
          List of tests that have been rebaselined or
          None on failure.
        """

        zip_namelist = zip_file.namelist()

        _log.debug('zip file namelist:')
        for name in zip_namelist:
            _log.debug('  ' + name)

        platform = self._rebaseline_port.test_platform_name_to_name(
            self._platform)
        _log.debug('Platform dir: "%s"', platform)

        test_no = 1
        self._rebaselined_tests = []
        for test in self._rebaselining_tests:
            _log.info('Test %d: %s', test_no, test)

            found = False
            scm_error = False
            test_basename = self._filesystem.splitext(test)[0]
            for suffix in BASELINE_SUFFIXES:
                archive_test_name = ('layout-test-results/%s-actual%s' %
                                      (test_basename, suffix))
                _log.debug('  Archive test file name: "%s"',
                           archive_test_name)
                if not archive_test_name in zip_namelist:
                    _log.info('  %s file not in archive.', suffix)
                    continue

                found = True
                _log.info('  %s file found in archive.', suffix)

                # Extract new baseline from archive and save it to a temp file.
                data = zip_file.read(archive_test_name)
                tempfile, temp_name = self._filesystem.open_binary_tempfile(suffix)
                tempfile.write(data)
                tempfile.close()

                expected_filename = '%s-expected%s' % (test_basename, suffix)
                expected_fullpath = self._filesystem.join(
                    self._rebaseline_port.baseline_path(), expected_filename)
                expected_fullpath = self._filesystem.normpath(expected_fullpath)
                _log.debug('  Expected file full path: "%s"',
                           expected_fullpath)

                # TODO(victorw): for now, the rebaselining tool checks whether
                # or not THIS baseline is duplicate and should be skipped.
                # We could improve the tool to check all baselines in upper
                # and lower
                # levels and remove all duplicated baselines.
                if self._is_dup_baseline(temp_name,
                                        expected_fullpath,
                                        test,
                                        suffix,
                                        self._platform):
                    self._filesystem.remove(temp_name)
                    self._delete_baseline(expected_fullpath)
                    continue

                self._filesystem.maybe_make_directory(self._filesystem.dirname(expected_fullpath))

                self._filesystem.move(temp_name, expected_fullpath)

                if 0 != self._scm.add(expected_fullpath, return_exit_code=True):
                    # FIXME: print detailed diagnose messages
                    scm_error = True
                elif suffix != '.checksum':
                    self._create_html_baseline_files(expected_fullpath)

            if not found:
                _log.warn('  No new baselines found in archive.')
            else:
                if scm_error:
                    _log.warn('  Failed to add baselines to your repository.')
                else:
                    _log.info('  Rebaseline succeeded.')
                    self._rebaselined_tests.append(test)

            test_no += 1

        zip_file.close()

        return self._rebaselined_tests

    def _is_dup_baseline(self, new_baseline, baseline_path, test, suffix,
                         platform):
        """Check whether a baseline is duplicate and can fallback to same
           baseline for another platform. For example, if a test has same
           baseline on linux and windows, then we only store windows
           baseline and linux baseline will fallback to the windows version.

        Args:
          expected_filename: baseline expectation file name.
          test: test name.
          suffix: file suffix of the expected results, including dot;
                  e.g. '.txt' or '.png'.
          platform: baseline platform 'mac', 'win' or 'linux'.

        Returns:
          True if the baseline is unnecessary.
          False otherwise.
        """
        test_filepath = self._filesystem.join(self._target_port.layout_tests_dir(),
                                     test)
        all_baselines = self._rebaseline_port.expected_baselines(
            test_filepath, suffix, True)
        for (fallback_dir, fallback_file) in all_baselines:
            if fallback_dir and fallback_file:
                fallback_fullpath = self._filesystem.normpath(
                    self._filesystem.join(fallback_dir, fallback_file))
                if fallback_fullpath.lower() != baseline_path.lower():
                    new_output = self._filesystem.read_binary_file(new_baseline)
                    fallback_output = self._filesystem.read_binary_file(fallback_fullpath)
                    is_image = baseline_path.lower().endswith('.png')
                    if not self._diff_baselines(new_output, fallback_output,
                                                is_image):
                        _log.info('  Found same baseline at %s',
                                  fallback_fullpath)
                        return True
                    else:
                        return False

        return False

    def _diff_baselines(self, output1, output2, is_image):
        """Check whether two baselines are different.

        Args:
          output1, output2: contents of the baselines to compare.

        Returns:
          True if two files are different or have different extensions.
          False otherwise.
        """

        if is_image:
            return self._port.diff_image(output1, output2, None)
        else:
            return self._port.compare_text(output1, output2)

    def _delete_baseline(self, filename):
        """Remove the file from repository and delete it from disk.

        Args:
          filename: full path of the file to delete.
        """

        if not filename or not self._filesystem.isfile(filename):
            return
        self._scm.delete(filename)

    def _update_rebaselined_tests_in_file(self, backup):
        """Update the rebaselined tests in test expectations file.

        Args:
          backup: if True, backup the original test expectations file.

        Returns:
          no
        """

        if self._rebaselined_tests:
            new_expectations = (
                self._test_expectations.remove_platform_from_expectations(
                self._rebaselined_tests, self._platform))
            path = self._target_port.path_to_test_expectations_file()
            if backup:
                date_suffix = time.strftime('%Y%m%d%H%M%S',
                                            time.localtime(time.time()))
                backup_file = ('%s.orig.%s' % (path, date_suffix))
                if self._filesystem.exists(backup_file):
                    self._filesystem.remove(backup_file)
                _log.info('Saving original file to "%s"', backup_file)
                self._filesystem.move(path, backup_file)

            self._filesystem.write_text_file(path, new_expectations)
            # self._scm.add(path)
        else:
            _log.info('No test was rebaselined so nothing to remove.')

    def _create_html_baseline_files(self, baseline_fullpath):
        """Create baseline files (old, new and diff) in html directory.

           The files are used to compare the rebaselining results.

        Args:
          baseline_fullpath: full path of the expected baseline file.
        """

        if not baseline_fullpath or not self._filesystem.exists(baseline_fullpath):
            return

        # Copy the new baseline to html directory for result comparison.
        baseline_filename = self._filesystem.basename(baseline_fullpath)
        new_file = get_result_file_fullpath(self._filesystem, self._options.html_directory,
                                            baseline_filename, self._platform,
                                            'new')
        self._filesystem.copyfile(baseline_fullpath, new_file)
        _log.info('  Html: copied new baseline file from "%s" to "%s".',
                  baseline_fullpath, new_file)

        # Get the old baseline from the repository and save to the html directory.
        try:
            output = self._scm.show_head(baseline_fullpath)
        except ScriptError, e:
            _log.info(e)
            output = ""

        if (not output) or (output.upper().rstrip().endswith(
            'NO SUCH FILE OR DIRECTORY')):
            _log.info('  No base file: "%s"', baseline_fullpath)
            return
        base_file = get_result_file_fullpath(self._filesystem, self._options.html_directory,
                                             baseline_filename, self._platform,
                                             'old')
        if base_file.upper().endswith('.PNG'):
            self._filesystem.write_binary_file(base_file, output)
        else:
            self._filesystem.write_text_file(base_file, output)
        _log.info('  Html: created old baseline file: "%s".',
                  base_file)

        # Get the diff between old and new baselines and save to the html dir.
        if baseline_filename.upper().endswith('.TXT'):
            output = self._scm.diff_for_file(baseline_fullpath, log=_log)
            if output:
                diff_file = get_result_file_fullpath(self._filesystem,
                    self._options.html_directory, baseline_filename,
                    self._platform, 'diff')
                self._filesystem.write_text_file(diff_file, output)
                _log.info('  Html: created baseline diff file: "%s".',
                          diff_file)


class HtmlGenerator(object):
    """Class to generate rebaselining result comparison html."""

    HTML_REBASELINE = ('<html>'
                       '<head>'
                       '<style>'
                       'body {font-family: sans-serif;}'
                       '.mainTable {background: #666666;}'
                       '.mainTable td , .mainTable th {background: white;}'
                       '.detail {margin-left: 10px; margin-top: 3px;}'
                       '</style>'
                       '<title>Rebaselining Result Comparison (%(time)s)'
                       '</title>'
                       '</head>'
                       '<body>'
                       '<h2>Rebaselining Result Comparison (%(time)s)</h2>'
                       '%(body)s'
                       '</body>'
                       '</html>')
    HTML_NO_REBASELINING_TESTS = (
        '<p>No tests found that need rebaselining.</p>')
    HTML_TABLE_TEST = ('<table class="mainTable" cellspacing=1 cellpadding=5>'
                       '%s</table><br>')
    HTML_TR_TEST = ('<tr>'
                    '<th style="background-color: #CDECDE; border-bottom: '
                    '1px solid black; font-size: 18pt; font-weight: bold" '
                    'colspan="5">'
                    '<a href="%s">%s</a>'
                    '</th>'
                    '</tr>')
    HTML_TEST_DETAIL = ('<div class="detail">'
                        '<tr>'
                        '<th width="100">Baseline</th>'
                        '<th width="100">Platform</th>'
                        '<th width="200">Old</th>'
                        '<th width="200">New</th>'
                        '<th width="150">Difference</th>'
                        '</tr>'
                        '%s'
                        '</div>')
    HTML_TD_NOLINK = '<td align=center><a>%s</a></td>'
    HTML_TD_LINK = '<td align=center><a href="%(uri)s">%(name)s</a></td>'
    HTML_TD_LINK_IMG = ('<td><a href="%(uri)s">'
                        '<img style="width: 200" src="%(uri)s" /></a></td>')
    HTML_TR = '<tr>%s</tr>'

    def __init__(self, port, target_port, options, platforms, rebaselining_tests):
        self._html_directory = options.html_directory
        self._port = port
        self._target_port = target_port
        self._options = options
        self._platforms = platforms
        self._rebaselining_tests = rebaselining_tests
        self._filesystem = port._filesystem
        self._html_file = self._filesystem.join(options.html_directory,
                                                'rebaseline.html')

    def abspath_to_uri(self, filename):
        """Converts an absolute path to a file: URI."""
        return path.abspath_to_uri(filename, self._port._executive)

    def generate_html(self):
        """Generate html file for rebaselining result comparison."""

        _log.info('Generating html file')

        html_body = ''
        if not self._rebaselining_tests:
            html_body += self.HTML_NO_REBASELINING_TESTS
        else:
            tests = list(self._rebaselining_tests)
            tests.sort()

            test_no = 1
            for test in tests:
                _log.info('Test %d: %s', test_no, test)
                html_body += self._generate_html_for_one_test(test)

        html = self.HTML_REBASELINE % ({'time': time.asctime(),
                                        'body': html_body})
        _log.debug(html)

        self._filesystem.write_text_file(self._html_file, html)
        _log.info('Baseline comparison html generated at "%s"',
                  self._html_file)

    def show_html(self):
        """Launch the rebaselining html in brwoser."""

        _log.info('Launching html: "%s"', self._html_file)
        self._port._user.open_url(self._html_file)
        _log.info('Html launched.')

    def _generate_baseline_links(self, test_basename, suffix, platform):
        """Generate links for baseline results (old, new and diff).

        Args:
          test_basename: base filename of the test
          suffix: baseline file suffixes: '.txt', '.png'
          platform: win, linux or mac

        Returns:
          html links for showing baseline results (old, new and diff)
        """

        baseline_filename = '%s-expected%s' % (test_basename, suffix)
        _log.debug('    baseline filename: "%s"', baseline_filename)

        new_file = get_result_file_fullpath(self._filesystem, self._html_directory,
                                            baseline_filename, platform, 'new')
        _log.info('    New baseline file: "%s"', new_file)
        if not self._filesystem.exists(new_file):
            _log.info('    No new baseline file: "%s"', new_file)
            return ''

        old_file = get_result_file_fullpath(self._filesystem, self._html_directory,
                                            baseline_filename, platform, 'old')
        _log.info('    Old baseline file: "%s"', old_file)
        if suffix == '.png':
            html_td_link = self.HTML_TD_LINK_IMG
        else:
            html_td_link = self.HTML_TD_LINK

        links = ''
        if self._filesystem.exists(old_file):
            links += html_td_link % {
                'uri': self.abspath_to_uri(old_file),
                'name': baseline_filename}
        else:
            _log.info('    No old baseline file: "%s"', old_file)
            links += self.HTML_TD_NOLINK % ''

        links += html_td_link % {'uri': self.abspath_to_uri(new_file),
                                 'name': baseline_filename}

        diff_file = get_result_file_fullpath(self._filesystem, self._html_directory,
                                             baseline_filename, platform,
                                             'diff')
        _log.info('    Baseline diff file: "%s"', diff_file)
        if self._filesystem.exists(diff_file):
            links += html_td_link % {'uri': self.abspath_to_uri(diff_file),
                                     'name': 'Diff'}
        else:
            _log.info('    No baseline diff file: "%s"', diff_file)
            links += self.HTML_TD_NOLINK % ''

        return links

    def _generate_html_for_one_test(self, test):
        """Generate html for one rebaselining test.

        Args:
          test: layout test name

        Returns:
          html that compares baseline results for the test.
        """

        test_basename = self._filesystem.basename(self._filesystem.splitext(test)[0])
        _log.info('  basename: "%s"', test_basename)
        rows = []
        for suffix in BASELINE_SUFFIXES:
            if suffix == '.checksum':
                continue

            _log.info('  Checking %s files', suffix)
            for platform in self._platforms:
                links = self._generate_baseline_links(test_basename, suffix,
                    platform)
                if links:
                    row = self.HTML_TD_NOLINK % self._get_baseline_result_type(
                        suffix)
                    row += self.HTML_TD_NOLINK % platform
                    row += links
                    _log.debug('    html row: %s', row)

                    rows.append(self.HTML_TR % row)

        if rows:
            test_path = self._filesystem.join(self._target_port.layout_tests_dir(), test)
            html = self.HTML_TR_TEST % (self.abspath_to_uri(test_path), test)
            html += self.HTML_TEST_DETAIL % ' '.join(rows)

            _log.debug('    html for test: %s', html)
            return self.HTML_TABLE_TEST % html

        return ''

    def _get_baseline_result_type(self, suffix):
        """Name of the baseline result type."""

        if suffix == '.png':
            return 'Pixel'
        elif suffix == '.txt':
            return 'Render Tree'
        else:
            return 'Other'


def get_host_port_object(options):
    """Return a port object for the platform we're running on."""
    # The only thing we really need on the host is a way to diff
    # text files and image files, which means we need to check that some
    # version of ImageDiff has been built. We will look for either Debug
    # or Release versions of the default port on the platform.
    options.configuration = "Release"
    port_obj = port.get(None, options)
    if not port_obj.check_image_diff(override_step=None, logging=False):
        _log.debug('No release version of the image diff binary was found.')
        options.configuration = "Debug"
        port_obj = port.get(None, options)
        if not port_obj.check_image_diff(override_step=None, logging=False):
            _log.error('No version of image diff was found. Check your build.')
            return None
        else:
            _log.debug('Found the debug version of the image diff binary.')
    else:
        _log.debug('Found the release version of the image diff binary.')
    return port_obj


def parse_options(args):
    """Parse options and return a pair of host options and target options."""
    option_parser = optparse.OptionParser()
    option_parser.add_option('-v', '--verbose',
                             action='store_true',
                             default=False,
                             help='include debug-level logging.')

    option_parser.add_option('-q', '--quiet',
                             action='store_true',
                             help='Suppress result HTML viewing')

    option_parser.add_option('-g', '--gpu',
                            action='store_true', default=False,
                            help='Rebaseline the GPU versions')

    option_parser.add_option('-p', '--platforms',
                             default=None,
                             help=('Comma delimited list of platforms '
                                   'that need rebaselining.'))

    option_parser.add_option('-u', '--archive_url',
                             default=('http://build.chromium.org/f/chromium/'
                                      'layout_test_results'),
                             help=('Url to find the layout test result archive'
                                   ' file.'))
    option_parser.add_option('-U', '--force_archive_url',
                             help=('Url of result zip file. This option is for debugging '
                                   'purposes'))

    option_parser.add_option('-w', '--webkit_canary',
                             action='store_true',
                             default=False,
                             help=('If True, pull baselines from webkit.org '
                                   'canary bot.'))

    option_parser.add_option('-b', '--backup',
                             action='store_true',
                             default=False,
                             help=('Whether or not to backup the original test'
                                   ' expectations file after rebaseline.'))

    option_parser.add_option('-d', '--html_directory',
                             default='',
                             help=('The directory that stores the results for '
                                   'rebaselining comparison.'))

    option_parser.add_option('', '--use_drt',
                             action='store_true',
                             default=False,
                             help=('Use ImageDiff from DumpRenderTree instead '
                                   'of image_diff for pixel tests.'))

    option_parser.add_option('', '--target-platform',
                             default='chromium',
                             help=('The target platform to rebaseline '
                                   '("mac", "chromium", "qt", etc.). Defaults '
                                   'to "chromium".'))
    options = option_parser.parse_args(args)[0]
    if options.platforms == None:
        if options.gpu:
            options.platforms = 'mac,win,linux'
        else:
            options.platforms = 'mac,win,win-xp,win-vista,linux'

    target_options = copy.copy(options)
    if options.target_platform == 'chromium':
        target_options.chromium = True
    options.tolerance = 0

    return (options, target_options)


def main(args):
    """Bootstrap function that sets up the object references we need and calls real_main()."""
    options, target_options = parse_options(args)

    # Set up our logging format.
    log_level = logging.INFO
    if options.verbose:
        log_level = logging.DEBUG
    logging.basicConfig(level=log_level,
                        format=('%(asctime)s %(filename)s:%(lineno)-3d '
                                '%(levelname)s %(message)s'),
                        datefmt='%y%m%d %H:%M:%S')

    target_port_name = None
    if options.gpu and options.target_platform == 'chromium':
        target_port_name = 'chromium-gpu'
    target_port_obj = port.get(target_port_name, target_options)
    host_port_obj = get_host_port_object(options)
    if not host_port_obj or not target_port_obj:
        return 1

    url_fetcher = urlfetcher.UrlFetcher(host_port_obj._filesystem)
    scm_obj = scm.default_scm()

    # We use the default zip factory method.
    zip_factory = None

    return real_main(options, target_options, host_port_obj, target_port_obj, url_fetcher,
                     zip_factory, scm_obj)


def real_main(options, target_options, host_port_obj, target_port_obj, url_fetcher,
              zip_factory, scm_obj):
    """Main function to produce new baselines. The Rebaseliner object uses two
    different Port objects - one to represent the machine the object is running
    on, and one to represent the port whose expectations are being updated.
    E.g., you can run the script on a mac and rebaseline the 'win' port.

    Args:
        options: command-line argument used for the host_port_obj (see below)
        target_options: command_line argument used for the target_port_obj.
            This object may have slightly different values than |options|.
        host_port_obj: a Port object for the platform the script is running
            on. This is used to produce image and text diffs, mostly, and
            is usually acquired from get_host_port_obj().
        target_port_obj: a Port obj representing the port getting rebaselined.
            This is used to find the expectations file, the baseline paths,
            etc.
        url_fetcher: object used to download the build archives from the bots
        zip_factory: factory function used to create zip file objects for
            the archives.
        scm_obj: object used to add new baselines to the source control system.
    """
    # Verify 'platforms' option is valid.
    if not options.platforms:
        _log.error('Invalid "platforms" option. --platforms must be '
                   'specified in order to rebaseline.')
        return 1
    platforms = [p.strip().lower() for p in options.platforms.split(',')]
    for platform in platforms:
        if not platform in REBASELINE_PLATFORM_ORDER:
            _log.error('Invalid platform: "%s"' % (platform))
            return 1

    # Adjust the platform order so rebaseline tool is running at the order of
    # 'mac', 'win' and 'linux'. This is in same order with layout test baseline
    # search paths. It simplifies how the rebaseline tool detects duplicate
    # baselines. Check _IsDupBaseline method for details.
    rebaseline_platforms = []
    for platform in REBASELINE_PLATFORM_ORDER:
        if platform in platforms:
            rebaseline_platforms.append(platform)

    options.html_directory = setup_html_directory(host_port_obj._filesystem, options.html_directory)

    rebaselining_tests = set()
    backup = options.backup
    for platform in rebaseline_platforms:
        rebaseliner = Rebaseliner(host_port_obj, target_port_obj,
                                  platform, options, url_fetcher, zip_factory,
                                  scm_obj)

        _log.info('')
        log_dashed_string('Rebaseline started', platform)
        if rebaseliner.run(backup):
            # Only need to backup one original copy of test expectation file.
            backup = False
            log_dashed_string('Rebaseline done', platform)
        else:
            log_dashed_string('Rebaseline failed', platform, logging.ERROR)

        rebaselining_tests |= set(rebaseliner.get_rebaselining_tests())

    _log.info('')
    log_dashed_string('Rebaselining result comparison started', None)
    html_generator = HtmlGenerator(host_port_obj,
                                   target_port_obj,
                                   options,
                                   rebaseline_platforms,
                                   rebaselining_tests)
    html_generator.generate_html()
    if not options.quiet:
        html_generator.show_html()
    log_dashed_string('Rebaselining result comparison done', None)

    return 0


if '__main__' == __name__:
    sys.exit(main(sys.argv[1:]))
