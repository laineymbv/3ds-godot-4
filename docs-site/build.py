#!/usr/bin/env python3
"""Builds the documentation website: a guide (Markdown) and a class reference
(Godot class XML) as static HTML pages.

    python3 docs-site/build.py --out docs-site/site \
        --guide platform/3ds/README.md --guide-title "Nintendo 3DS platform" \
        --class-dir platform/3ds/doc_classes [--class-dir DIR ...] \
        [--extra-guide SLUG:TITLE:PATH ...] [--ref-title "..."] [--home-title "..."]

Every page is self-contained (one stylesheet, no scripts, no external fonts),
so the output can be served by any static host or opened from disk.
"""
import argparse
import html
import os
import re
import shutil
import textwrap
import xml.etree.ElementTree as ET

import markdown

CSS = """
:root { --bg:#f7f7fb; --fg:#1d2030; --muted:#5a6079; --accent:#c26a00; --link:#1f5fbf;
        --code:#eef0f6; --side:#ffffff; --border:#dde0ea; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --bg:#14161f; --fg:#e6e8f2; --muted:#9aa1bb; --accent:#ffb347;
        --link:#7cb2ff; --code:#1f2232; --side:#191c28; --border:#2a2e40; }
}
:root[data-theme="dark"] { --bg:#14161f; --fg:#e6e8f2; --muted:#9aa1bb; --accent:#ffb347;
        --link:#7cb2ff; --code:#1f2232; --side:#191c28; --border:#2a2e40; }
* { box-sizing: border-box; }
body { margin:0; background:var(--bg); color:var(--fg); font:16px/1.6 system-ui, "Open Sans", sans-serif; }
.layout { display:grid; grid-template-columns: 260px 1fr; min-height:100vh; }
nav { background:var(--side); border-right:1px solid var(--border); padding:24px 18px; position:sticky; top:0;
      height:100vh; overflow:auto; }
nav .brand { font-weight:700; color:var(--accent); font-size:18px; margin-bottom:18px; display:block; text-decoration:none; }
nav h3 { font-size:12px; text-transform:uppercase; letter-spacing:.08em; color:var(--muted); margin:20px 0 6px; }
nav a { display:block; color:var(--fg); text-decoration:none; padding:3px 6px; border-radius:6px; font-size:14px; }
nav a:hover, nav a.current { background:var(--code); color:var(--accent); }
main { padding:40px 56px; max-width:920px; }
h1 { font-size:32px; margin:0 0 8px; }
h2 { font-size:22px; margin:36px 0 10px; padding-top:6px; border-top:1px solid var(--border); }
h3 { font-size:17px; margin:24px 0 6px; }
a { color:var(--link); }
code { background:var(--code); padding:1px 5px; border-radius:4px; font:14px/1.4 ui-monospace, Menlo, Consolas, monospace; }
pre { background:var(--code); padding:14px 16px; border-radius:8px; overflow:auto; }
pre code { background:none; padding:0; font-size:13.5px; }
table { border-collapse:collapse; width:100%; margin:14px 0; font-size:15px; }
th, td { border:1px solid var(--border); padding:6px 10px; text-align:left; vertical-align:top; }
th { background:var(--code); }
.lede { color:var(--muted); font-size:18px; margin:0 0 24px; }
.sig { font:14px ui-monospace, Menlo, Consolas, monospace; background:var(--code); padding:8px 12px; border-radius:8px;
       margin:12px 0 6px; overflow:auto; }
.member { border-top:1px solid var(--border); padding-top:6px; margin-top:18px; }
.tag { font-size:12px; text-transform:uppercase; letter-spacing:.06em; color:var(--muted); margin-right:8px; }
.crumbs { font-size:13px; color:var(--muted); margin-bottom:10px; }
@media (max-width: 760px) {
  .layout { grid-template-columns: 1fr; }
  nav { position:static; height:auto; border-right:0; border-bottom:1px solid var(--border); }
  main { padding:24px 16px; }
}
"""


def esc(s):
    return html.escape(s, quote=False)


class Site:
    def __init__(self, out, title):
        self.out = out
        self.title = title
        self.guides = []  # (slug, title, html_file)
        self.classes = []  # (name, inherits, brief, file)

    def page(self, rel, title, body, active, crumbs=""):
        nav = ['<nav><a class="brand" href="{}">{}</a>'.format(self.rel(rel, "index.html"), esc(self.title))]
        if self.guides:
            nav.append("<h3>Guides</h3>")
            for slug, gtitle, gfile in self.guides:
                cls = ' class="current"' if active == "guide:" + slug else ""
                nav.append('<a{} href="{}">{}</a>'.format(cls, self.rel(rel, gfile), esc(gtitle)))
        if self.classes:
            nav.append("<h3>Class reference</h3>")
            for name, _, _, cfile in sorted(self.classes):
                cls = ' class="current"' if active == "class:" + name else ""
                nav.append('<a{} href="{}">{}</a>'.format(cls, self.rel(rel, cfile), esc(name)))
        nav.append("</nav>")
        doc = (
            "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<title>{} - {}</title><link rel=\"stylesheet\" href=\"{}style.css\"></head>\n"
            "<body><div class=\"layout\">{}<main>{}{}</main></div></body></html>\n"
        ).format(esc(title), esc(self.title), self.rel(rel, ""), "".join(nav),
                 ('<div class="crumbs">%s</div>' % crumbs) if crumbs else "", body)
        path = os.path.join(self.out, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(doc)

    @staticmethod
    def rel(from_file, to_file):
        depth = from_file.count("/")
        return "../" * depth + to_file


# ---- Markdown guides -------------------------------------------------------

def lift_fences(text):
    """Python-Markdown doesn't see fenced code inside list items. Move each such
    block out to the left margin, with a blank line before it, so it renders."""
    out, lines, i = [], text.split("\n"), 0
    while i < len(lines):
        m = re.match(r"^(\s*)```(\w*)\s*$", lines[i])
        if m and m.group(1):
            indent = len(m.group(1))
            body = []
            i += 1
            while i < len(lines) and not re.match(r"^\s*```\s*$", lines[i]):
                body.append(lines[i][min(indent, len(lines[i]) - len(lines[i].lstrip())):])
                i += 1
            out += ["", "```" + m.group(2)] + body + ["```", ""]
        else:
            out.append(lines[i])
        i += 1
    return "\n".join(out)


def render_guide(site, slug, title, path, index_title):
    text = open(path, encoding="utf-8").read()
    # Drop the leading "# Title" line; the page shows its own title.
    text = re.sub(r"^# [^\n]*\n", "", text, count=1)
    text = lift_fences(text)
    body = markdown.markdown(text, extensions=["fenced_code", "tables", "toc", "sane_lists"],
                             extension_configs={"toc": {"permalink": False}})
    rel = "%s.html" % slug
    site.page(rel, title, "<h1>%s</h1>%s" % (esc(title), body), "guide:" + slug)
    site.guides.append((slug, title, rel))


# ---- Godot class XML -------------------------------------------------------

LINK_CLASS = re.compile(r"\[([A-Z][A-Za-z0-9_]*)\]")


def bbcode(text, known):
    """Turns Godot's description markup into HTML. Input is plain text."""
    blocks = []

    def stash_block(m):
        blocks.append("<pre><code>%s</code></pre>" % esc(textwrap.dedent(m.group(1)).strip("\n")))
        return "\x00%d\x00" % (len(blocks) - 1)

    text = re.sub(r"\[codeblock\](.*?)\[/codeblock\]", stash_block, text, flags=re.S)
    text = esc(text)
    text = re.sub(r"\[code\](.*?)\[/code\]", r"<code>\1</code>", text, flags=re.S)
    text = re.sub(r"\[b\](.*?)\[/b\]", r"<b>\1</b>", text, flags=re.S)
    text = re.sub(r"\[i\](.*?)\[/i\]", r"<i>\1</i>", text, flags=re.S)
    text = re.sub(r"\[url=([^\]]+)\](.*?)\[/url\]", r'<a href="\1">\2</a>', text, flags=re.S)
    text = re.sub(r"\[(method|member|signal|constant|param|enum) ([^\]]+)\]",
                  lambda m: "<code>%s</code>" % m.group(2), text)

    def class_link(m):
        name = m.group(1)
        if name in known:
            return '<a href="%s.html"><code>%s</code></a>' % (name, name)
        return "<code>%s</code>" % name

    text = LINK_CLASS.sub(class_link, text)
    text = text.replace("[br]", "<br>")
    # Godot's XML has one paragraph per line; code blocks are already stashed.
    paras = [line.strip() for line in text.split("\n") if line.strip()]
    text = "".join("<p>%s</p>" % p for p in paras)
    for i, b in enumerate(blocks):
        text = text.replace("\x00%d\x00" % i, b)
    return text


def signature(m_el):
    ret = m_el.find("return")
    params = []
    for p in m_el.findall("param"):
        s = "%s: %s" % (p.get("name"), p.get("type"))
        if p.get("default") is not None:
            s += " = " + p.get("default")
        params.append(s)
    rtype = ret.get("type") if ret is not None else "void"
    return "%s %s(%s)" % (rtype, m_el.get("name"), ", ".join(params))


def render_class(site, path, known):
    root = ET.parse(path).getroot()
    name = root.get("name")
    inherits = root.get("inherits", "")
    brief = (root.findtext("brief_description") or "").strip()
    desc = root.findtext("description") or ""
    parts = ['<h1><code>%s</code></h1>' % esc(name)]
    if brief:
        parts.append('<p class="lede">%s</p>' % esc(brief))
    if inherits:
        parts.append('<p class="crumbs">Inherits: <a href="%s.html"><code>%s</code></a></p>'
                     % (inherits, inherits) if inherits in known else
                     '<p class="crumbs">Inherits: <code>%s</code></p>' % esc(inherits))
    if desc.strip():
        parts.append(bbcode(desc, known))
    signals = root.findall("signals/signal")
    if signals:
        parts.append("<h2>Signals</h2>")
        for s in signals:
            args = ", ".join("%s: %s" % (p.get("name"), p.get("type")) for p in s.findall("param"))
            parts.append('<div class="member"><div class="sig">signal %s(%s)</div>%s</div>' % (
                esc(s.get("name")), esc(args), bbcode(s.findtext("description") or "", known)))
    methods = root.findall("methods/method")
    if methods:
        parts.append("<h2>Methods</h2>")
        for m in methods:
            parts.append('<div class="member" id="%s"><div class="sig">%s</div>%s</div>' % (
                esc(m.get("name")), esc(signature(m)), bbcode(m.findtext("description") or "", known)))
    fname = "%s.html" % name
    site.page(fname, name, "".join(parts), "class:" + name, crumbs="Class reference")
    site.classes.append((name, inherits, brief, fname))


def render_index(site, home_title, intro):
    cards = []
    for slug, gtitle, gfile in site.guides:
        cards.append('<li><a href="%s">%s</a></li>' % (gfile, esc(gtitle)))
    body = ['<h1>%s</h1>' % esc(home_title), '<p class="lede">%s</p>' % esc(intro)]
    if cards:
        body.append("<h2>Guides</h2><ul>%s</ul>" % "".join(cards))
    if site.classes:
        items = "".join('<li><a href="%s"><code>%s</code></a> &mdash; %s</li>' % (f, esc(n), esc(b))
                        for n, _, b, f in sorted(site.classes))
        body.append("<h2>Class reference</h2><ul>%s</ul>" % items)
    site.page("index.html", home_title, "".join(body), "home")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--title", default="Godot for the Nintendo 3DS")
    ap.add_argument("--intro", default="Documentation for the Godot 4.7 fork that runs on the Nintendo 3DS: "
                                       "building, making a game, stereo 3D, the renderer, and the classes it adds.")
    ap.add_argument("--guide", action="append", nargs=3, metavar=("SLUG", "TITLE", "PATH"), default=[],
                    help="a Markdown guide: slug, page title, source file")
    ap.add_argument("--class-dir", action="append", default=[], help="folder of Godot class XML files")
    ap.add_argument("--home-title", default="Documentation")
    args = ap.parse_args()

    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
    os.makedirs(args.out)
    site = Site(args.out, args.title)
    with open(os.path.join(args.out, "style.css"), "w") as f:
        f.write(CSS)

    for slug, gtitle, path in args.guide:
        render_guide(site, slug, gtitle, path, args.home_title)

    class_files = []
    for d in args.class_dir:
        class_files += [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith(".xml")]
    known = {ET.parse(p).getroot().get("name") for p in class_files}
    for p in class_files:
        render_class(site, p, known)

    render_index(site, args.home_title, args.intro)
    print("wrote %d guide(s) and %d class page(s) to %s" % (len(args.guide), len(class_files), args.out))


if __name__ == "__main__":
    main()
