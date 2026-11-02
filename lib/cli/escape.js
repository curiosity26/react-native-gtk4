/**
 * Escaping helpers. Build regular expressions and generated files (CMake,
 * RPM specs, desktop entries, C++) from outside strings only through these,
 * or through a helper that escapes every character the target syntax treats
 * specially, the escape character itself first. A hand-rolled
 * `.replace(/\./g, '\\.')` or `.replace(/"/g, '\\"')` misses characters and
 * is what CodeQL's js/incomplete-sanitization flags. See AGENTS.md.
 */
'use strict';

/**
 * `s` with every RegExp metacharacter escaped, for `new RegExp(...)` and for
 * POSIX extended regexes (rpm's dependency filters): only the characters
 * both treat as special, since ERE leaves `\-` or `\/` undefined.
 */
function escapeRegExp(s) {
  return String(s).replace(/[\\^$.*+?()[\]{}|]/g, '\\$&');
}

module.exports = {escapeRegExp};
