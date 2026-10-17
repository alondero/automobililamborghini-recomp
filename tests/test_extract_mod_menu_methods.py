import unittest

from tests.extract_mod_menu_methods import extract_method


class ExtractModMenuMethodsTests(unittest.TestCase):
    def test_literals_and_comments_do_not_end_a_method(self):
        for content in (
            'const char* label = "}";',
            "char brace = '}';",
            '// }\n    do_work();',
            '/* } and an unfinished { */ do_work();',
            'const char* label = R"text(} ") {\n} )text";',
            r'const char* label = "escaped quote: \" }";',
        ):
            with self.subTest(content=content):
                method = "void ModMenu::refresh_mods(bool scan) {\n    " + content + "\n    done();\n}"
                source = "// void ModMenu::refresh_mods(fake) { }\n" + method + "\nvoid other() {}\n"
                self.assertEqual(extract_method(source, "refresh_mods"), method)

    def test_nested_blocks_stay_in_the_method(self):
        method = "void ModMenu::process_event(const Event& e) { if (enabled) { callback(); } }"
        self.assertEqual(extract_method(method + "\nvoid other() {}", "process_event"), method)

    def test_unterminated_method_reports_an_error(self):
        with self.assertRaises(ValueError):
            extract_method("void ModMenu::refresh_mods(bool scan) {", "refresh_mods")


if __name__ == "__main__":
    unittest.main()
