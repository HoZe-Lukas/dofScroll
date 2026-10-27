#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
"""Checks the static website in site/ (called by scripts/check-site.sh, ctest and the Pages workflow).

Checked: every page parses with balanced tags; local links, images, icons and anchors exist; every
page has a title and a meta description (index pages: title 50-60, description 150-160 characters);
no external script, style, font or image URLs and no style attributes; exactly one inline script,
allowed by the hash in the Content-Security-Policy meta tag; canonical, hreflang and og tags;
language parity between site/ (English) and site/de/ (German): same pages, same sections and FAQ
entries, same outgoing link targets, reciprocal alternate links; sitemap and robots.txt; size
budgets; the plain-text reading of every page (no words glued together across inline elements, no
double spaces, no straight quotes in visible text); and WCAG contrast of the colour variables in site/style.css for light and dark.

Usage: scripts/check-site.py [site-dir]
"""
import base64
import hashlib
import json
import os
import re
import struct
import sys
from collections import Counter
from html.parser import HTMLParser
from urllib.parse import urldefrag, urlparse

BASE = "https://hoze-lukas.github.io/dofScroll/"
VOID = {"meta", "link", "img", "br", "hr", "input", "source", "area", "base", "col", "wbr"}
LANGS = {"en": "", "de": "de"}
BUDGET_LANG = 384 * 1024      # shared files + that language's files (the main picture is the Joystick modes page, about 10 KB more than the old profile editor)
BUDGET_TOTAL = 480 * 1024     # whole site folder

errors = []


def err(msg):
    errors.append(msg)


class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.title = ""
        self._in_title = False
        self.meta = {}        # name or property -> content
        self.http_equiv = {}
        self.links = []       # (rel, attrs)
        self.anchors = []     # (attrs)
        self.imgs = []
        self.ids = []
        self.scripts = []     # (attrs, text)
        self._script = None
        self.sections = []
        self.faq_h3 = 0
        self._in_faq = False
        self.html_lang = None
        self.style_attrs = 0
        self.ext_resources = []
        self.h1 = 0
        self.buttons = []
        self.langsel = []     # (summary aria-label) of every details.langsel
        self._in_langsel = False

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a:
            self.ids.append(a["id"])
        if "style" in a:
            self.style_attrs += 1
        if tag == "html":
            self.html_lang = a.get("lang")
        if tag == "details" and a.get("class") == "langsel":
            self._in_langsel = True
            self.langsel.append(None)
        if tag == "summary" and self._in_langsel:
            self.langsel[-1] = a.get("aria-label", "")
            self._in_langsel = False
        if tag == "title":
            self._in_title = True
        elif tag == "meta":
            if "name" in a:
                self.meta[a["name"]] = a.get("content", "")
            if "property" in a:
                self.meta.setdefault(a["property"], a.get("content", ""))
            if "http-equiv" in a:
                self.http_equiv[a["http-equiv"].lower()] = a.get("content", "")
        elif tag == "link":
            self.links.append(a)
        elif tag == "a":
            self.anchors.append(a)
        elif tag == "img":
            self.imgs.append(a)
        elif tag == "button":
            self.buttons.append(a)
        elif tag == "script":
            self._script = (a, [])
        elif tag == "section":
            if "id" in a:
                self.sections.append(a["id"])
            if a.get("id") == "faq":
                self._in_faq = True
        elif tag == "h3" and self._in_faq:
            self.faq_h3 += 1
        elif tag == "h1":
            self.h1 += 1
        if tag not in VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if tag == "title":
            self._in_title = False
        if tag == "script" and self._script:
            self.scripts.append((self._script[0], "".join(self._script[1])))
            self._script = None
        if tag == "section":
            self._in_faq = False
        if tag in VOID:
            return
        if not self.stack or self.stack[-1] != tag:
            err("unbalanced tag </%s> (open: %s)" % (tag, self.stack[-3:]))
            if tag in self.stack:
                while self.stack and self.stack.pop() != tag:
                    pass
        else:
            self.stack.pop()

    def handle_data(self, data):
        if self._in_title:
            self.title += data
        if self._script:
            self._script[1].append(data)


def parse(path):
    p = Page()
    with open(path, encoding="utf-8") as f:
        text = f.read()
    p.feed(text)
    p.close()
    if p.stack:
        err("%s: unclosed tags %s" % (path, p.stack))
    if not text.lower().startswith("<!doctype html>"):
        err("%s: missing <!doctype html>" % path)
    return p

BLOCK = {"address", "article", "aside", "blockquote", "body", "br", "dd", "details", "div", "dl", "dt",
         "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6",
         "head", "header", "hr", "html", "li", "main", "nav", "ol", "p", "pre", "section", "summary",
         "table", "td", "th", "tr", "ul", "title", "button"}
NO_TEXT = {"script", "style", "svg", "title"}


class PlainText(HTMLParser):
    """Mimics textContent without CSS: only block-level tags separate text, inline tags insert nothing.
    Whatever is glued together here is read glued together by copy and paste, search snippets, screen
    readers and the plain-text view, however much space the CSS puts between the elements."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out = []         # text pieces; "\n" marks a block boundary
        self.skip = 0
        self.pre = 0
        self.problems = []

    def handle_starttag(self, tag, attrs):
        if tag in NO_TEXT:
            self.skip += 1
        if tag == "pre":
            self.pre += 1
        if tag in BLOCK:
            self.out.append("\n")

    def handle_startendtag(self, tag, attrs):
        if tag in BLOCK:
            self.out.append("\n")

    def handle_endtag(self, tag):
        if tag in NO_TEXT and self.skip:
            self.skip -= 1
        if tag == "pre" and self.pre:
            self.pre -= 1
        if tag in BLOCK:
            self.out.append("\n")

    def handle_data(self, data):
        if self.skip or not data:
            return
        prev = next((x[-1] for x in reversed(self.out) if x), "")
        if not self.pre:
            if "  " in data:
                self.problems.append("double space in %r" % data.strip()[:40])
            if '"' in data or "'" in data:
                self.problems.append("straight quote in %r (use typographic quotes)" % data.strip()[:40])
            if re.search(r"\S·|·\S", data):
                self.problems.append("'·' without spaces in %r" % data.strip()[:40])
            first = data[0]
            if prev and not prev.isspace() and not first.isspace() and \
                    first not in ".,:;!?)]»”’%-" and prev not in "([«“‘":
                before = "".join(self.out)[-20:].replace("\n", " ")
                self.problems.append("text glued across an element boundary: %r + %r" % (before, data.strip()[:20]))
        self.out.append(data)


def plain_text_check(path, tag):
    pt = PlainText()
    pt.feed(open(path, encoding="utf-8").read())
    for m in pt.problems:
        err("%s: %s" % (tag, m))


def rel(root, path):
    return os.path.relpath(path, root).replace(os.sep, "/")


def resolve(root, page_path, href):
    """Map a link to a local file path inside root, or None when it is external."""
    href, _frag = urldefrag(href)
    u = urlparse(href)
    if u.scheme in ("http", "https"):
        if href.startswith(BASE):
            local = href[len(BASE):]
            target = os.path.join(root, local)
        else:
            return None
    elif u.scheme:
        return None
    elif href == "":
        return page_path
    else:
        target = os.path.normpath(os.path.join(os.path.dirname(page_path), u.path))
    if os.path.isdir(target) or target.endswith("/"):
        target = os.path.join(target, "index.html")
    return target


def contrast_check(css_path):
    css = open(css_path, encoding="utf-8").read()

    def block(pattern):
        m = re.search(pattern + r"\s*\{([^}]*)\}", css)
        if not m:
            err("style.css: block %s not found" % pattern)
            return {}
        return dict(re.findall(r"--([a-z0-9-]+):\s*(#[0-9a-fA-F]{6})", m.group(1)))

    light = block(r"(?m)^:root")
    dark_media = block(r':root:not\(\[data-theme="light"\]\)')
    dark_attr = block(r':root\[data-theme="dark"\]')
    if dark_media != dark_attr:
        err("style.css: the two dark colour blocks differ")
    if "color-scheme" not in css:
        err("style.css: no color-scheme property")

    def lum(h):
        c = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
        c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
        return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]

    def ratio(a, b):
        la, lb = sorted((lum(a), lum(b)), reverse=True)
        return (la + 0.05) / (lb + 0.05)

    text_pairs = [("text", "bg"), ("text", "surface"), ("muted", "bg"), ("muted", "surface"),
                  ("link", "bg"), ("link", "surface"), ("text", "code-bg"), ("on-accent", "accent"),
                  ("link", "code-bg"), ("on-accent", "accent-2")]
    ui_pairs = [("border", "bg"), ("focus", "bg"), ("focus", "surface"), ("accent", "bg"), ("accent", "surface")]
    for name, theme in (("light", light), ("dark", dark_attr)):
        for pairs, need in ((text_pairs, 4.5), (ui_pairs, 3.0)):
            for fg, bg in pairs:
                if fg not in theme or bg not in theme:
                    err("style.css: %s theme lacks --%s or --%s" % (name, fg, bg))
                    continue
                r = ratio(theme[fg], theme[bg])
                if r < need:
                    err("style.css: %s theme contrast %s on %s is %.2f, needs %.1f" % (name, fg, bg, r, need))
                else:
                    print("  contrast %-5s %-9s on %-8s %.2f (>= %.1f)" % (name, fg, bg, r, need))
        glow_check(css, name, theme)
    motion_check(css)



def glow_check(css, name, theme):
    """Text on the translucent --glow tint (hero, closing section): composite it at full strength over
    --bg and --surface and require 4.5:1 for every text colour."""
    blocks = {"light": r"(?m)^:root\s*\{([^}]*)\}", "dark": r':root\[data-theme="dark"\]\s*\{([^}]*)\}'}
    m = re.search(blocks[name], css)
    g = re.search(r"--glow:\s*rgba\((\d+),\s*(\d+),\s*(\d+),\s*([0-9.]+)\)", m.group(1)) if m else None
    if not g:
        err("style.css: %s theme lacks --glow: rgba(r, g, b, a)" % name)
        return
    r, gg, b, al = int(g.group(1)), int(g.group(2)), int(g.group(3)), float(g.group(4))
    if al > 0.35:
        err("style.css: %s --glow alpha %.2f is above 0.35" % (name, al))

    def lum(h):
        c = [int(h[i:i + 2], 16) / 255 for i in (1, 3, 5)]
        c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
        return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
    for base in ("bg", "surface"):
        bh = theme[base]
        mix = "#%02x%02x%02x" % tuple(round(al * f + (1 - al) * int(bh[i:i + 2], 16)) for f, i in ((r, 1), (gg, 3), (b, 5)))
        for fg in ("text", "muted", "link"):
            la, lb = sorted((lum(theme[fg]), lum(mix)), reverse=True)
            ratio = (la + 0.05) / (lb + 0.05)
            if ratio < 4.5:
                err("style.css: %s theme: %s on glow over %s (%s) is %.2f, needs 4.5" % (name, fg, base, mix, ratio))
            else:
                print("  contrast %-5s %-6s on glow/%-7s %.2f (>= 4.5)" % (name, fg, base, ratio))


def motion_check(css):
    """Animations live in style.css only and are switched off for reduced motion."""
    if "@keyframes" in css:
        m = re.search(r"@media \(prefers-reduced-motion: reduce\)\s*\{(.*)\}\s*$", css, re.S)
        if not m or "animation: none" not in m.group(1) or "transition: none" not in m.group(1):
            err("style.css: animations or transitions are not disabled under prefers-reduced-motion: reduce")


def png_size(path):
    with open(path, "rb") as f:
        d = f.read(24)
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", d[16:24])


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else
                           os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "site"))
    if not os.path.isdir(root):
        print("check-site: %s does not exist" % root, file=sys.stderr)
        return 1

    # files per language
    pages = {}
    for lang, sub in LANGS.items():
        d = os.path.join(root, sub)
        pages[lang] = sorted(f for f in os.listdir(d) if f.endswith(".html")) if os.path.isdir(d) else []
    if pages["en"] != pages["de"]:
        err("page sets differ: en=%s de=%s" % (pages["en"], pages["de"]))
    for need in ("index.html", "404.html"):
        for lang in LANGS:
            if need not in pages[lang]:
                err("%s lacks %s" % (lang, need))
    for need in ("favicon.svg", "robots.txt", "sitemap.xml", "style.css", "img/og.png", "img/og-de.png"):
        if not os.path.isfile(os.path.join(root, need)):
            err("missing site/%s" % need)

    parsed = {}
    for lang, sub in LANGS.items():
        for name in pages[lang]:
            path = os.path.join(root, sub, name)
            p = parse(path)
            parsed[(lang, name)] = p
            tag = "%s/%s" % (lang, name)
            if p.html_lang != lang:
                err("%s: <html lang> is %r, expected %r" % (tag, p.html_lang, lang))
            title = p.title.strip()
            desc = p.meta.get("description", "").strip()
            if not title:
                err("%s: no <title>" % tag)
            if not desc:
                err("%s: no meta description" % tag)
            if name == "index.html":
                if not 50 <= len(title) <= 60:
                    err("%s: title is %d characters, expected 50-60" % (tag, len(title)))
                if not 150 <= len(desc) <= 160:
                    err("%s: description is %d characters, expected 150-160" % (tag, len(desc)))
            plain_text_check(path, tag)
            if p.h1 != 1:
                err("%s: expected exactly one <h1>, found %d" % (tag, p.h1))
            if p.meta.get("color-scheme") != "light dark":
                err("%s: missing <meta name=color-scheme content='light dark'>" % tag)
            if re.search(r"<style|<animate|<set ", open(path, encoding="utf-8").read()):
                err("%s: inline <style> or SMIL animation (animations belong in style.css)" % tag)
            if p.style_attrs:
                err("%s: %d style attribute(s); the CSP forbids inline styles" % (tag, p.style_attrs))
            if not any(b.get("id") == "theme" and b.get("aria-pressed") in ("true", "false") and b.get("aria-label")
                       for b in p.buttons):
                err("%s: theme toggle button (id=theme, aria-pressed, aria-label) missing" % tag)
            if not any(a.get("class") == "lang" and a.get("hreflang") and a.get("href") for a in p.anchors):
                err("%s: language switch link missing" % tag)
            if len(p.langsel) != 1 or not p.langsel[0]:
                err("%s: expected one details.langsel with a summary aria-label" % tag)
            flags = [os.path.basename(i.get("src", "")) for i in p.imgs if "/flags/" in i.get("src", "")]
            flag_of = {"en": "gb.svg", "de": "de.svg"}
            if flags != [flag_of[lang], flag_of["de" if lang == "en" else "en"]]:
                err("%s: language selector must show the flag of the current language first, then the other one; found %s" % (tag, flags))
            # CSP and the single inline script
            csp = p.http_equiv.get("content-security-policy", "")
            want = ("default-src 'none'", "img-src 'self'", "style-src 'self'", "base-uri 'none'", "form-action 'none'")
            for w in want:
                if w not in csp:
                    err("%s: CSP lacks %s" % (tag, w))
            js = [(a, t) for a, t in p.scripts if a.get("type") != "application/ld+json"]
            if len(js) != 1 or js[0][0].get("src"):
                err("%s: expected exactly one inline script and no external script" % tag)
            else:
                h = base64.b64encode(hashlib.sha256(js[0][1].encode("utf-8")).digest()).decode()
                if "script-src 'sha256-%s'" % h not in csp:
                    err("%s: CSP script hash does not match the inline script (sha256-%s)" % (tag, h))
            for a, t in p.scripts:
                if a.get("type") == "application/ld+json":
                    try:
                        data = json.loads(t)
                    except ValueError as e:
                        err("%s: JSON-LD is invalid: %s" % (tag, e))
                        continue
                    for k in ("name", "description", "operatingSystem", "applicationCategory", "license",
                              "downloadUrl", "offers", "inLanguage"):
                        if k not in data:
                            err("%s: JSON-LD lacks %s" % (tag, k))
                    if data.get("inLanguage") != lang:
                        err("%s: JSON-LD inLanguage is %r" % (tag, data.get("inLanguage")))
                    if data.get("offers", {}).get("price") != "0":
                        err("%s: JSON-LD offer price is not 0" % tag)
            # resources: only local
            for l in p.links:
                href = l.get("href", "")
                if l.get("rel") in ("stylesheet", "icon", "apple-touch-icon", "preload") or l.get("rel") == "stylesheet":
                    t = resolve(root, path, href)
                    if t is None:
                        err("%s: external resource %s" % (tag, href))
                    elif not os.path.isfile(t):
                        err("%s: missing resource %s" % (tag, href))
            for im in p.imgs:
                src = im.get("src", "")
                t = resolve(root, path, src)
                if t is None:
                    err("%s: external image %s" % (tag, src))
                elif not os.path.isfile(t):
                    err("%s: missing image %s" % (tag, src))
                if im.get("alt") is None:
                    err("%s: image %s has no alt attribute" % (tag, src))
                if not im.get("width") or not im.get("height"):
                    err("%s: image %s has no width/height" % (tag, src))
            # links
            ids = set(p.ids)
            for a in p.anchors:
                href = a.get("href", "")
                if href.startswith("#"):
                    if href[1:] not in ids:
                        err("%s: anchor %s has no target" % (tag, href))
                    continue
                t = resolve(root, path, href)
                if t is None:
                    if not href.startswith("https://"):
                        err("%s: link %s is not https" % (tag, href))
                    continue
                if not os.path.isfile(t):
                    err("%s: broken local link %s" % (tag, href))
                else:
                    _, frag = urldefrag(href)
                    if frag and t.endswith(".html"):
                        pt = parsed.get((lang, os.path.basename(t)))
                        # anchors into other pages are checked when both are parsed
                        if pt is not None and frag not in pt.ids:
                            err("%s: link %s points to a missing id" % (tag, href))
            if len(set(p.ids)) != len(p.ids):
                err("%s: duplicate ids" % tag)
            # index pages: canonical, alternates, og
            if name == "index.html":
                canon = BASE + ("" if lang == "en" else "de/")
                cl = [l for l in p.links if l.get("rel") == "canonical"]
                if len(cl) != 1 or cl[0].get("href") != canon:
                    err("%s: canonical link must be %s" % (tag, canon))
                alts = {l.get("hreflang"): l.get("href") for l in p.links if l.get("rel") == "alternate"}
                if alts != {"en": BASE, "de": BASE + "de/", "x-default": BASE}:
                    err("%s: alternate links wrong: %s" % (tag, alts))
                for hl, href in alts.items():
                    t = resolve(root, path, href)
                    if t is None or not os.path.isfile(t):
                        err("%s: alternate %s -> %s does not exist" % (tag, hl, href))
                for k in ("og:title", "og:description", "og:url", "og:image", "og:locale", "og:locale:alternate",
                          "og:type", "twitter:card", "twitter:image", "og:image:alt"):
                    if not p.meta.get(k):
                        err("%s: missing %s" % (tag, k))
                if p.meta.get("og:url") != canon:
                    err("%s: og:url is not the canonical URL" % tag)
                if p.meta.get("og:locale", "")[:2] != lang:
                    err("%s: og:locale does not match the language" % tag)
                img = p.meta.get("og:image", "")
                if img.startswith(BASE):
                    f = os.path.join(root, img[len(BASE):])
                    if png_size(f) != (1200, 630):
                        err("%s: og image %s is not a 1200x630 PNG" % (tag, img))
                else:
                    err("%s: og:image must be an absolute URL below %s" % (tag, BASE))
            else:
                if p.meta.get("robots") != "noindex":
                    err("%s: non-index pages should carry robots noindex" % tag)

    # the language switch must point to the same page in the other language
    for lang, sub in LANGS.items():
        for name in pages[lang]:
            p = parsed.get((lang, name))
            if p is None:
                continue
            other = "de" if lang == "en" else "en"
            for a in p.anchors:
                if a.get("class") == "lang":
                    t = resolve(root, os.path.join(root, sub, name), a["href"])
                    expect = os.path.join(root, LANGS[other], name)
                    if t is None or os.path.normpath(t) != os.path.normpath(expect):
                        err("%s/%s: language switch points to %s, expected the same page in %s" % (lang, name, a["href"], other))
                    if a.get("hreflang") != other:
                        err("%s/%s: language switch hreflang must be %s" % (lang, name, other))

    # parity of the two index pages
    en, de = parsed.get(("en", "index.html")), parsed.get(("de", "index.html"))
    if en and de:
        if en.sections != de.sections:
            err("section ids differ: en=%s de=%s" % (en.sections, de.sections))
        if en.faq_h3 != de.faq_h3:
            err("FAQ entries differ: en=%d de=%d" % (en.faq_h3, de.faq_h3))

        def targets(p):
            out = Counter()
            for a in p.anchors:
                h = a.get("href", "")
                if h.startswith("#") or a.get("class") == "lang":
                    continue
                if h.startswith("./") or not h.startswith("http"):
                    continue
                out[h] += 1
            return out
        if targets(en) != targets(de):
            diff = (targets(en) - targets(de)) + (targets(de) - targets(en))
            err("outgoing link targets differ between en and de: %s" % dict(diff))
        if Counter(a.get("href") for a in en.anchors if a.get("href", "").startswith("#")) != \
                Counter(a.get("href") for a in de.anchors if a.get("href", "").startswith("#")):
            err("in-page links differ between en and de")
        names = lambda p: sorted(os.path.basename(i.get("src", "")) for i in p.imgs)
        if names(en) != names(de):
            err("images differ between en and de")
        # a German page must not be a verbatim copy
        if en.title == de.title:
            err("titles of en and de are identical")

    for f in ("gb.svg", "de.svg"):
        fp = os.path.join(root, "img", "flags", f)
        if not os.path.isfile(fp):
            err("img/flags/%s is missing" % f)
        elif os.path.getsize(fp) > 2048:
            err("img/flags/%s is larger than 2 KB" % f)

    # sitemap
    sm = os.path.join(root, "sitemap.xml")
    if os.path.isfile(sm):
        text = open(sm, encoding="utf-8").read()
        locs = re.findall(r"<loc>([^<]+)</loc>", text)
        if sorted(locs) != sorted([BASE, BASE + "de/"]):
            err("sitemap.xml must list exactly %s and %sde/" % (BASE, BASE))
        for loc in locs:
            t = resolve(root, os.path.join(root, "index.html"), loc)
            if t is None or not os.path.isfile(t):
                err("sitemap.xml: %s does not exist" % loc)
        for url in re.findall(r"<url>(.*?)</url>", text, re.S):
            alts = dict(re.findall(r'hreflang="([^"]+)" href="([^"]+)"', url))
            if alts != {"en": BASE, "de": BASE + "de/", "x-default": BASE}:
                err("sitemap.xml: url entry lacks the alternates: %s" % alts)
    rb = os.path.join(root, "robots.txt")
    if os.path.isfile(rb) and "Sitemap: " + BASE + "sitemap.xml" not in open(rb, encoding="utf-8").read():
        err("robots.txt does not name the sitemap")

    # css must not pull anything external
    css_path = os.path.join(root, "style.css")
    if os.path.isfile(css_path):
        css = open(css_path, encoding="utf-8").read()
        if re.search(r"@import|url\(\s*['\"]?(https?:)?//", css):
            err("style.css loads an external resource")
        contrast_check(css_path)

    # size budgets
    sizes = {}
    for dp, _dn, fn in os.walk(root):
        for f in fn:
            sizes[rel(root, os.path.join(dp, f))] = os.path.getsize(os.path.join(dp, f))
    total = sum(sizes.values())
    if total > BUDGET_TOTAL:
        err("site is %d bytes, budget %d" % (total, BUDGET_TOTAL))
    for lang, sub in LANGS.items():
        other = "de/" if lang == "en" else None
        other_og = "img/og-de.png" if lang == "en" else "img/og.png"
        n = sum(s for f, s in sizes.items()
                if not (lang == "en" and f.startswith("de/")) and f != other_og
                and not (lang == "de" and f in ("index.html", "404.html")))
        print("  size set %s: %d bytes (budget %d)" % (lang, n, BUDGET_LANG))
        if n > BUDGET_LANG:
            err("%s page set is %d bytes, budget %d" % (lang, n, BUDGET_LANG))
    print("  size total: %d bytes (budget %d)" % (total, BUDGET_TOTAL))

    if errors:
        for e in errors:
            print("check-site: " + e, file=sys.stderr)
        print("check-site: %d problem(s)" % len(errors), file=sys.stderr)
        return 1
    print("check-site: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
