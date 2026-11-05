"""Embed an isolated-world page script without runtime file access."""
import pathlib
import sys
source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
name = sys.argv[3] if len(sys.argv) > 3 else "kAssistantPageScript"
pathlib.Path(sys.argv[2]).write_text(
    '#pragma once\ninline constexpr char ' + name + '[] = R"ZITTER_JS('
    + source + ')ZITTER_JS";\n', encoding="utf-8")
