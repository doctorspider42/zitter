"""Embed the isolated-world assistant bridge without runtime file access."""
import pathlib
import sys
source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
pathlib.Path(sys.argv[2]).write_text(
    '#pragma once\ninline constexpr char kAssistantPageScript[] = R"ZITTER_JS('
    + source + ')ZITTER_JS";\n', encoding="utf-8")
