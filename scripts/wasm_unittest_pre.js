// SPDX-License-Identifier: GPL-2.0-or-later
// W1 shim for emscripten issue #22264 (present in 3.1.71): under -sNODERAWFS=1 the standard
// streams are created with `tty: true` (a boolean) but no tty ops, so ioctl(TIOCGWINSZ), which
// DuckDB's Printer::TerminalWidth and Catch's TerminalWidth call on fd 0, dereferences
// `stream.tty.ops` and throws a JS TypeError. Give fds 0-2 a tty object whose TIOCGWINSZ
// reports the real Node terminal size (or 24x80), so isatty() is unchanged.
// Drop this file once the toolchain fixes the bug.
Module['onRuntimeInitialized'] = function () {
  for (var fd = 0; fd <= 2; fd++) {
    var s = FS.getStream(fd);
    if (s && s.tty === true) {
      s.tty = { ops: { ioctl_tiocgwinsz: function () {
        var o = process.stdout;
        return [(o && o.rows) || 24, (o && o.columns) || 80];
      } } };
    }
  }
};
