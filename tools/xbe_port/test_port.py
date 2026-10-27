"""python3 tools/xbe_port/test_port.py (from the repo root)"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import port  # noqa: E402

SRC = """extern void sub_0009C7A0_gen(void);
void sub_0009C7A0(void) { sub_0009C7A0_gen(); }
switch (va) { case VA(0x000D7460): break; }
"""
M = {0x0009C7A0: 0x0009C760, 0x000D7460: 0x000D83F0}
BOUNDS = {"CODE_END": 0x27C860, "VTABLE_LO": 0x27C860, "VTABLE_HI": 0x2EF080, "OVERLAY_PAGE": 0x290000}


class PortTest(unittest.TestCase):
    def test_pal_header_is_identity(self):
        h, missing = port.make_header([SRC], None, "pal")
        self.assertEqual(missing, [])
        self.assertIn("#define VA_0x000D7460 0x000D7460u", h)
        self.assertNotIn("#define sub_", h)
        self.assertIn("#define SSX3_CODE_END 0x00280000u", h)

    def test_port_header_renames_both_names_and_literals(self):
        h, missing = port.make_header([SRC], M, "ntsc", BOUNDS)
        self.assertEqual(missing, [])
        self.assertIn("#define SSX3_CODE_END 0x0027C860u", h)
        self.assertIn('#define SSX3_BUILD_NAME "PAL"', port.make_header([SRC], None, "pal")[0])
        self.assertIn("#define sub_0009C7A0 sub_0009C760", h)
        self.assertIn("#define sub_0009C7A0_gen sub_0009C760_gen", h)
        self.assertIn("#define VA_0x000D7460 0x000D83F0u", h)

    def test_unmapped_address_is_reported(self):
        _, missing = port.make_header([SRC], {0x0009C7A0: 0x0009C760}, "ntsc", BOUNDS)
        self.assertEqual(missing, [0x000D7460])

    def test_absent_addresses_get_distinct_values_above_guest_memory(self):
        m = port.port_lookup({"map": {}, "absent": ["0x00288F08", "0x00289530"]})
        self.assertEqual(m[0x00288F08], 0xFF288F08)
        self.assertNotEqual(m[0x00288F08], m[0x00289530])

    def test_comment_only_addresses_are_not_required(self):
        src = 'int x; /* sub_00012345 */ // VA(0x00204400)\nconst char *s = "/* not a comment */";\n'
        _, missing = port.make_header([src], {}, "ntsc", BOUNDS)
        self.assertEqual(missing, [])
        self.assertIn('"/* not a comment */"', port.strip_comments(src))

    def test_pal_json_makes_an_unknown_build_an_error(self):
        import json, tempfile
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(port.find_port("abc", d))            # no pal.json: unknown = PAL
            pathlib.Path(d, "pal.json").write_text(json.dumps({"variant": "pal", "text_sha256": "abc"}))
            self.assertIsNone(port.find_port("abc", d))            # PAL itself: identity
            with self.assertRaises(SystemExit):
                port.find_port("def", d)

    def test_missing_bound_is_reported(self):
        _, missing = port.make_header([SRC], M, "ntsc", {"CODE_END": 0x27C860})
        self.assertEqual(missing, ["OVERLAY_PAGE", "VTABLE_HI", "VTABLE_LO"])

    def test_translate_renames_for_the_manual_scan(self):
        out = port.translate(SRC, M)
        self.assertIn("void sub_0009C760(void) { sub_0009C760_gen(); }", out)
        self.assertNotIn("sub_0009C7A0", out)
        self.assertEqual(port.translate(SRC, None), SRC)

    def test_seeds_are_ported_and_unmapped_ones_dropped(self):
        seeds = [{"start": "0x0009C7A0", "note": "a"}, {"start": "0x00123450", "note": "b"}]
        out, dropped = port.port_seeds(seeds, M)
        self.assertEqual(out, [{"start": "0x0009C760", "note": "a"}])
        self.assertEqual(dropped, ["0x00123450"])


if __name__ == "__main__":
    unittest.main()
