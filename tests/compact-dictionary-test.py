import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "compact-dictionary.py"


class CompactDictionaryTest(unittest.TestCase):
    def test_ranks_inflections_and_preserves_flags_and_encoding(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            aff = root / "fr.aff"
            dic = root / "fr.dic"
            frequencies = root / "frequencies.tsv"
            output = root / "compact"
            rules = "SET UTF-8\nPFX U Y 1\nPFX U 0 re .\nSFX S Y 1\nSFX S 0 s [éa]\n"
            aff.write_text(rules, encoding="utf-8")
            dic.write_text("3\ncafé/US\nrecafés\nchat\n", encoding="utf-8")
            frequencies.write_text("recafés\t20\ncafés\t15\nchat\t25\n", encoding="utf-8")
            command = [sys.executable, str(SCRIPT), str(aff), str(dic), str(output),
                       "--limit", "2", "--frequencies", str(frequencies),
                       "--output-encoding", "latin-1", "--charset", "ISO8859-1"]
            subprocess.run(command, check=True, capture_output=True, text=True)
            self.assertEqual((output / dic.name).read_bytes(), b"2\ncaf\xe9/US\nchat\n")
            self.assertEqual((output / aff.name).read_text(encoding="latin-1"),
                             rules.replace("UTF-8", "ISO8859-1"))

            aff.write_text(rules + "NEEDAFFIX X\n", encoding="utf-8")
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Unsupported affix directive", result.stderr)


if __name__ == "__main__":
    unittest.main()
