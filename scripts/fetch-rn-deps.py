#!/usr/bin/env python3
"""Fetches React Native's C++ sources and third-party dependencies for the
Linux host build, without Gradle or the Android SDK.

Mirrors what React Native's Fantom tester build does through Gradle
(private/react-native-fantom/build.gradle.kts and the ReactAndroid
prepare* tasks), laid out the same way so Fantom's CMake files work as is:

  <deps>/react-native          React Native source at the pinned tag
  <deps>/third-party-ndk/...   boost, double-conversion, fast_float, fmt, glog
  <deps>/fantom-third-party/...folly, gflags, nlohmann_json

Usage: scripts/fetch-rn-deps.py [--deps DIR] [--offline]

--offline downloads nothing: the archives and checkouts must be in place
already (<deps>/react-native, <deps>/hermes, <deps>/downloads/...), as a
Flatpak manifest's sources put them (package-linux --format flatpak), and
npm installs from its cache.
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSIONS = dict(
    (k.strip(), v.strip().strip('"'))
    for k, v in (
        line.split("=", 1)
        for line in (ROOT / "rn-version.properties").read_text().splitlines()
        if "=" in line and not line.startswith("#")
    )
)


OFFLINE = False


def log(msg):
    print(f"[fetch-rn-deps] {msg}", flush=True)


def download(url, dest: Path):
    if dest.exists():
        return dest
    if OFFLINE:
        sys.exit(f"[fetch-rn-deps] offline, and {dest} is missing ({url})")
    dest.parent.mkdir(parents=True, exist_ok=True)
    log(f"downloading {url}")
    tmp = dest.with_suffix(dest.suffix + ".part")
    with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f)
    tmp.rename(dest)
    return dest


def extract(source: Path, members_re: str, out: Path, strip_first=False,
            rename=None):
    """Copies files whose path matches members_re into out.

    source is a tarball, or a checkout whose files are matched as
    "<checkout dir name>/<path>" so both look like the same archive.
    """
    pattern = re.compile(members_re)

    def place(name, open_src):
        if not pattern.match(name):
            return
        rel = name.split("/", 1)[1] if strip_first else name
        if rename:
            rel = rename(rel)
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        with open_src() as src, open(target, "wb") as dst:
            shutil.copyfileobj(src, dst)

    if source.is_dir():
        for f in source.rglob("*"):
            if f.is_file() and ".git" not in f.parts:
                name = f"{source.name}/{f.relative_to(source).as_posix()}"
                place(name, lambda f=f: open(f, "rb"))
        return
    with tarfile.open(source) as tar:
        for m in tar.getmembers():
            if m.isfile():
                place(m.name, lambda m=m: tar.extractfile(m))


def github_checkout(repo, tag, top, downloads: Path):
    """Shallow-clones a GitHub tag into downloads/<top>. Archive downloads are
    not always reachable from CI proxies; git always is."""
    dest = downloads / top
    git_clone(f"https://github.com/{repo}", tag, dest)
    return dest


def git_clone(url, tag, dest: Path):
    if (dest / ".git").exists():
        return
    if OFFLINE:
        # An extracted archive of the tag stands in for the checkout.
        if dest.is_dir() and any(dest.iterdir()):
            return
        sys.exit(f"[fetch-rn-deps] offline, and {dest} is missing ({url} at {tag})")
    log(f"cloning {url} at {tag}")
    subprocess.run(["git", "clone", "-q", "--depth", "1", "--branch", tag,
                    url, str(dest)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deps", default=os.environ.get(
        "RNGTK_DEPS_DIR", str(ROOT / "third-party" / "deps")))
    ap.add_argument("--offline", action="store_true")
    args = ap.parse_args()
    global OFFLINE
    OFFLINE = args.offline
    deps = Path(args.deps).resolve()
    downloads = deps / "downloads"
    ndk = deps / "third-party-ndk"
    fantom3p = deps / "fantom-third-party"

    rn = deps / "react-native"
    git_clone("https://github.com/facebook/react-native",
              f"v{VERSIONS['reactNative']}", rn)
    rn_pkg = rn / "packages" / "react-native"
    jni3p = rn_pkg / "ReactAndroid" / "src" / "main" / "jni" / "third-party"
    tester3p = rn / "private" / "react-native-fantom" / "tester" / "third-party"
    toml = (rn_pkg / "gradle" / "libs.versions.toml").read_text()

    def v(name):
        return re.search(rf'^{name}\s*=\s*"([^"]+)"', toml, re.M).group(1)

    boost, dc, ff, fmt = v("boost"), v("doubleconversion"), v("fastFloat"), v("fmt")
    folly, glog, gflags, nl = v("folly"), v("glog"), v("gflags"), v("nlohmannjson")

    # boost: headers only (plus RN's CMakeLists).
    out = ndk / "boost"
    if not out.exists():
        # GitHub's release tarball is modular (libs/<lib>/include/boost/...);
        # merge the headers into boost_<ver>/boost/ like the release RN uses.
        dotted = boost.replace("_", ".")
        t = download(f"https://github.com/boostorg/boost/releases/download/"
                     f"boost-{dotted}/boost-{dotted}.tar.gz",
                     downloads / f"boost-{dotted}.tar.gz")
        header = re.compile(rf"boost-{dotted}/libs/.+?/include/(boost/.*\.(hpp|ipp|h))$")
        extract(t, header.pattern, out,
                rename=lambda rel: f"boost_{boost}/" + header.match(rel).group(1))
        shutil.copy(jni3p / "boost" / "CMakeLists.txt", out)

    for name, repo, tag, members, ver in [
        ("double-conversion", "google/double-conversion", f"v{dc}",
         rf"double-conversion-{dc}/src/", dc),
        ("fast_float", "fastfloat/fast_float", f"v{ff}",
         rf"fast_float-{ff}/include/", ff),
        ("fmt", "fmtlib/fmt", fmt, rf"fmt-{fmt}/(src|include)/", fmt),
    ]:
        out = ndk / name
        if out.exists():
            continue
        t = github_checkout(repo, tag, f"{name}-{ver}", downloads)
        if name == "double-conversion":
            # Sources are flattened into double-conversion/ (RN's CMakeLists).
            extract(t, members, out,
                    rename=lambda rel: "double-conversion/" + rel.rsplit("/", 1)[1])
        else:
            extract(t, members, out, strip_first=True)
        shutil.copy(jni3p / name / "CMakeLists.txt", out)

    # glog: fill in config tokens the way PrepareGlogTask does.
    out = ndk / "glog"
    if not out.exists():
        t = github_checkout("google/glog", f"v{glog}", f"glog-{glog}", downloads)
        extract(t, rf"glog-{glog}/src/", out)
        shutil.copy(jni3p / "glog" / "CMakeLists.txt", out)
        shutil.copy(jni3p / "glog" / "config.h", out)
        tokens = {
            "ac_cv_have_unistd_h": "1", "ac_cv_have_stdint_h": "1",
            "ac_cv_have_systypes_h": "1", "ac_cv_have_inttypes_h": "1",
            "ac_cv_have_libgflags": "0",
            "ac_google_start_namespace": "namespace google {",
            "ac_cv_have_uint16_t": "1", "ac_cv_have_u_int16_t": "1",
            "ac_cv_have___uint16": "0", "ac_google_end_namespace": "}",
            "ac_cv_have___builtin_expect": "1", "ac_google_namespace": "google",
            "ac_cv___attribute___noinline": "__attribute__ ((noinline))",
            "ac_cv___attribute___noreturn": "__attribute__ ((noreturn))",
            "ac_cv___attribute___printf_4_5":
                "__attribute__((__format__ (__printf__, 4, 5)))",
        }
        # Like PrepareGlogTask: each *.h.in is filled in and flattened into
        # the glog root, except config.h.in (RN ships its own config.h).
        for f in list(out.rglob("*.h.in")):
            if f.name != "config.h.in":
                text = f.read_text()
                for k, val in tokens.items():
                    text = text.replace(f"@{k}@", val)
                (out / f.name[:-3]).write_text(text)
            f.unlink()
        exported = out / "exported" / "glog"
        exported.mkdir(parents=True)
        for name in ("stl_logging.h", "logging.h", "raw_logging.h", "vlog_is_on.h"):
            shutil.copy(out / name, exported / name)
        shutil.copy(out / f"glog-{glog}" / "src" / "glog" / "log_severity.h",
                    exported / "log_severity.h")

    # Fantom's desktop third-party (folly, gflags, nlohmann_json).
    out = fantom3p / "folly"
    if not out.exists():
        t = github_checkout("facebook/folly", f"v{folly}", f"folly-{folly}",
                            downloads)
        extract(t, rf"folly-{folly}/folly/", out, strip_first=True)
        shutil.copy(tester3p / "folly" / "CMakeLists.txt", out)

    out = fantom3p / "nlohmann_json"
    if not out.exists():
        t = github_checkout("nlohmann/json", f"v{nl}", f"json-{nl}", downloads)
        extract(t, rf"json-{nl}/(src|include)/", out, strip_first=True)
        shutil.copy(tester3p / "nlohmann_json" / "CMakeLists.txt", out)

    out = fantom3p / "gflags"
    if not out.exists():
        t = github_checkout("gflags/gflags", f"v{gflags}", f"gflags-{gflags}",
                            downloads)
        staging = deps / "gflags-src"
        extract(t, rf"gflags-{gflags}/src/", staging)
        src = staging / f"gflags-{gflags}" / "src"
        dst = out / "gflags"
        dst.mkdir(parents=True)
        shutil.copy(tester3p / "gflags" / "CMakeLists.txt", out)
        for f in list(src.glob("*.h")) + list(src.glob("*.cc")):
            shutil.copy(f, dst / f.name)

        def sub(name, fn, target=None):
            text = fn((src / name).read_text())
            (dst / (target or name[:-3])).write_text(text)

        sub("gflags_declare.h.in", lambda s: re.sub(r"@([A-Z0-9_]+)@", "1", re.sub(
            r"@(HAVE_STDINT_H|HAVE_SYS_TYPES_H|HAVE_INTTYPES_H|GFLAGS_INTTYPES_FORMAT_C99)@",
            "1", s.replace("@GFLAGS_NAMESPACE@", "gflags"))))
        sub("config.h.in", lambda s: re.sub(r"^#cmakedefine", "//cmakedefine", s,
                                            flags=re.M))
        sub("gflags_ns.h.in", lambda s: s.replace("@ns@", "google").replace(
            "@NS@", "GOOGLE"), "gflags_google.h")
        sub("gflags.h.in", lambda s: s.replace("@GFLAGS_ATTRIBUTE_UNUSED@", "").replace(
            "@INCLUDE_GFLAGS_NS_H@", '#include "gflags/gflags_google.h"'))
        sub("gflags_completions.h.in",
            lambda s: s.replace("@GFLAGS_NAMESPACE@", "gflags"))
        shutil.rmtree(staging)

    # Codegen for RN's core specs (what ReactAndroid's
    # generateCodegenArtifactsFromSchema task produces), from the git source
    # so internal specs are included, using the published codegen CLI.
    out = deps / "codegen"
    if not out.exists():
        node = deps / "node"
        node.mkdir(exist_ok=True)
        # react-native and @react-native/codegen, as scripts/codegen-deps'
        # lock pins them.
        for f in ("package.json", "package-lock.json"):
            shutil.copy(ROOT / "scripts" / "codegen-deps" / f, node / f)
        log("installing react-native and @react-native/codegen from npm")
        subprocess.run(["npm", "ci", "--no-audit", "--no-fund", "--ignore-scripts"]
                       + (["--offline"] if OFFLINE else []), cwd=node, check=True)
        nm = node / "node_modules"
        schema = deps / "codegen-schema.json"
        subprocess.run(["node", str(nm / "@react-native/codegen/lib/cli/combine/"
                                    "combine-js-to-schema-cli.js"),
                        "--platform", "android", str(schema), str(rn_pkg / "src")],
                       check=True)
        raw = deps / "codegen-raw"
        subprocess.run(["node", str(nm / "react-native/scripts/generate-specs-cli.js"),
                        "--platform", "android", "--schemaPath", str(schema),
                        "--outputDir", str(raw), "--libraryName", "FBReactNativeSpec",
                        "--javaPackageName", "com.facebook.fbreact.specs"], check=True)
        for f in (raw / "jni").rglob("*"):
            rel = f.relative_to(raw / "jni")
            if f.suffix in (".h", ".cpp") and rel.parts[0] == "react":
                (out / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(f, out / rel)
        shutil.copy(rn / "private/react-native-fantom/tester/codegen/CMakeLists.txt", out)
        shutil.rmtree(raw)

    # Hermes source at the tag React Native pins (built by build-hermes.sh).
    git_clone("https://github.com/facebook/hermes", VERSIONS["hermes"],
              deps / "hermes")

    log(f"done: {deps}")


if __name__ == "__main__":
    sys.exit(main())
