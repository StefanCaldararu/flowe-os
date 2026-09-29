"""Notes engine on the host: vault listing, Obsidian link resolution, and
Markdown pagination/rendering over the real Gfx + UI fonts.

    python3 test/host/test_notes.py            # run the checks
    NOTES_PNG_DIR=/tmp/x python3 test/host/test_notes.py   # also save pages

Page images need Pillow; the checks themselves do not.
"""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

OS = Path(__file__).resolve().parents[2]
SDK = OS.parent / "freeink-sdk/libs/display/FreeInkDisplay"

VAULT = {
    "Inbox.md": "---\ntags: [inbox]\n---\n# Inbox\n\nSee [[Roadmap]] and [[projects/ideas/X3 Notes|the X3 idea]].\n",
    "Daily/2026-09-28.md": "Went to [[inbox]].\n- [ ] one\n- [x] two\n",
    ".obsidian/app.json": "{}",
    "Projects/Roadmap.md": "\n".join(
        ["# Roadmap", "", "Plan for **the firmware** and *friends*.", "", "## Steps"]
        + ["%d. Step %d with a longer line that should wrap onto the next line of the page" % (i, i) for i in range(1, 30)]
        + ["", "> [!tip] Remember", "> Quotes get a bar and [[Inbox#Top|a link]].", "",
           "```", "code_line = [[not a link]]", "```", "", "---", "Back to [notes](../Inbox.md).",
           "Unknown: [[Nope]]. External: [site](https://example.com). Emoji: done ✅ \U0001F680"]
    ),
    "Projects/Ideas/X3 Notes.md": "# X3 Notes\n\t- nested\n\t\t- deeper\n| a | b |\n|---|---|\n| 1 | 2 |\n",
}


class NotesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        cls.root = root / "sd"
        for rel, text in VAULT.items():
            p = cls.root / "notes" / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
        cls.exe = root / "notes_test"
        subprocess.run(
            ["c++", "-std=c++17", "-fsanitize=address,undefined", "-g", "-DFLOWE_TEST_DISPLAY_X3",
             "-I" + str(OS / "test/host/notes_stubs"), "-I" + str(OS / "test/host/sync_stubs"),
             "-I" + str(SDK / "include"), "-I" + str(OS / "lib/EpdFontCore"), "-I" + str(OS / "src"),
             str(OS / "test/host/notes_render_test.cpp"), str(OS / "src/Gfx.cpp"), str(OS / "src/Fonts.cpp"),
             str(OS / "src/notes/NoteLayout.cpp"), str(OS / "src/notes/NotesVault.cpp"),
             "-o", str(cls.exe)],
            check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_exe(self, *args):
        env = dict(os.environ, NOTES_SD_ROOT=str(self.root))
        return subprocess.run([str(self.exe), *args], env=env, check=True, capture_output=True,
                              text=True).stdout

    def resolve(self, frm, target):
        return self.run_exe("resolve", frm, target).strip()

    def test_list_folders_first_hidden_skipped(self):
        self.assertEqual(self.run_exe("list", "/notes").split("\n")[:4],
                         ["d Daily", "d Projects", "f Inbox.md", ""])

    def test_walk_lists_every_note_with_its_hash(self):
        def fnv1a32(data):
            h = 2166136261
            for b in data:
                h = ((h ^ b) * 16777619) & 0xFFFFFFFF
            return h

        want = set()
        for rel, text in VAULT.items():
            if rel.startswith("."):
                continue
            data = text.encode()
            want.add("f %s %d %08x" % (rel, len(data), fnv1a32(data)))
            parts = rel.split("/")[:-1]
            for i in range(len(parts)):
                want.add("d " + "/".join(parts[: i + 1]))
        self.assertEqual(set(self.run_exe("walk", "-").splitlines()), want)

    def test_resolve_links_like_obsidian(self):
        r = self.resolve
        self.assertEqual(r("/notes/Inbox.md", "Roadmap"), "/notes/Projects/Roadmap.md")  # anywhere
        self.assertEqual(r("/notes/Daily/2026-09-28.md", "inbox"), "/notes/Inbox.md")   # any case
        self.assertEqual(r("/notes/Inbox.md", "projects/ideas/X3 Notes"), "/notes/Projects/Ideas/X3 Notes.md")
        self.assertEqual(r("/notes/Projects/Roadmap.md", "Inbox#Top"), "/notes/Inbox.md")
        self.assertEqual(r("/notes/Projects/Roadmap.md", "../Inbox.md"), "/notes/Inbox.md")
        self.assertEqual(r("/notes/Projects/Roadmap.md", "Ideas/X3%20Notes.md"),
                         "/notes/Projects/Ideas/X3 Notes.md")
        self.assertEqual(r("/notes/Inbox.md", "Nope"), "NONE")
        self.assertEqual(r("/notes/Inbox.md", "https://example.com"), "NONE")

    def render(self, note):
        out = Path(self.tmp.name) / "pages" / Path(note).stem
        out.mkdir(parents=True, exist_ok=True)
        lines = self.run_exe("render", note, str(out)).splitlines()
        png_dir = os.environ.get("NOTES_PNG_DIR")
        if png_dir:
            from PIL import Image
            for raw in sorted(out.glob("page_*.raw")):
                native = Image.frombytes("1", (792, 528), raw.read_bytes())
                Path(png_dir).mkdir(parents=True, exist_ok=True)
                # Gfx draws portrait rotated 90 degrees CW into the landscape panel.
                native.rotate(-90, expand=True).save(Path(png_dir) / ("%s_%s.png" % (Path(note).stem, raw.stem)))
        return lines

    def test_inbox_hides_frontmatter_and_lists_links(self):
        lines = self.render("/notes/Inbox.md")
        self.assertEqual(lines[0], "pages 1")
        self.assertEqual(lines[1], "page 1 links: [Roadmap] [projects/ideas/X3 Notes]")

    def test_long_note_paginates_and_keeps_links_in_order(self):
        lines = self.render("/notes/Projects/Roadmap.md")
        pages = int(lines[0].split()[1])
        self.assertGreaterEqual(pages, 2)
        links = " ".join(lines[1:])
        # Code blocks and external URLs never link; vault links keep reading order.
        self.assertNotIn("not a link", links)
        self.assertNotIn("example.com", links)
        self.assertLess(links.index("[Inbox#Top]"), links.index("[../Inbox.md]"))
        self.assertLess(links.index("[../Inbox.md]"), links.index("[Nope]"))

    def test_tasks_render(self):
        self.assertEqual(self.render("/notes/Daily/2026-09-28.md"), ["pages 1", "page 1 links: [inbox]"])

    def test_nested_lists_and_tables_render(self):
        self.assertEqual(self.render("/notes/Projects/Ideas/X3 Notes.md")[0], "pages 1")


if __name__ == "__main__":
    unittest.main()
