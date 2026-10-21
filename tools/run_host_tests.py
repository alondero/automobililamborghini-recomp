"""Run Python host tests, optionally requiring selected modules to execute."""
import argparse
from pathlib import Path
import sys
import unittest


def run(suite, *, require_all=False, required=()):
    def test_ids(tests):
        for test in tests:
            if isinstance(test, unittest.TestSuite):
                yield from test_ids(test)
            else:
                yield test.id()

    discovered = list(test_ids(suite))
    missing_modules = [module for module in required
                       if not any(name.startswith(module + ".") for name in discovered)]
    if not discovered or missing_modules:
        print(f"Required host checks not discovered: {missing_modules or 'empty suite'}", file=sys.stderr)
        return 1
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    missing = [(test, reason) for test, reason in result.skipped
               if require_all or any(test.id().startswith(module + ".") for module in required)]
    if missing:
        print("Required host checks skipped; install NumPy and initialize lib/rt64.", file=sys.stderr)
        return 1
    return 0 if result.wasSuccessful() else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-all", action="store_true", help="Fail on any skipped test")
    parser.add_argument("--require", action="append", default=[], metavar="MODULE",
                        help="Fail if a test in this module skips; may be repeated")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root))
    suite = unittest.defaultTestLoader.discover(str(root / "tests"), top_level_dir=str(root))
    return run(suite, require_all=args.require_all, required=args.require)


if __name__ == "__main__":
    raise SystemExit(main())
