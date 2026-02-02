#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Copyright 2026 Ned Konz <ned@metamagix.tech>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""
This script automatically adds MIT license headers to source files.
It handles C/C++, Python, CMake, and Markdown files.
"""

import os

# --- Configuration ---
YEAR = "2026"
COPYRIGHT_HOLDER = "Ned Konz <ned@metamagix.tech>"
SPDX_ID = "MIT"

# --- License Text ---
LICENSE_TEXT = f"""Copyright {YEAR} {COPYRIGHT_HOLDER}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

# --- Header Formats ---
C_HEADER = f"""/*
 * SPDX-License-Identifier: {SPDX_ID}
 *
 * {LICENSE_TEXT.strip().replace(os.linesep, os.linesep + " * ")}
 */"""

HASH_COMMENT_HEADER = f"""# SPDX-License-Identifier: {SPDX_ID}
#
# {LICENSE_TEXT.strip().replace(os.linesep, os.linesep + "# ")}"""

MARKDOWN_HEADER = f"""<!--
SPDX-License-Identifier: {SPDX_ID}

{LICENSE_TEXT.strip()}
-->"""

# --- File Mappings ---
FILE_MAPPINGS = {
    # ".c": C_HEADER,   #  clang-format rules remove C headers
    ".h": C_HEADER,
    ".py": HASH_COMMENT_HEADER,
    "CMakeLists.txt": HASH_COMMENT_HEADER,
    ".md": MARKDOWN_HEADER,
}


# --- Script Logic ---
def has_header(file_path):
    """Check if a file already has the SPDX identifier."""
    try:
        with open(file_path, "r", encoding="utf-8") as f:
            for _ in range(10):
                line = f.readline()
                if not line:
                    break
                if f"SPDX-License-Identifier: {SPDX_ID}" in line:
                    return True
    except (IOError, UnicodeDecodeError):
        return False
    return False


def add_header(file_path, header):
    """Prepend a header to a file."""
    try:
        with open(file_path, "r", encoding="utf-8", newline="") as f:
            original_content = f.read()

        if original_content.startswith("#!"):
            lines = original_content.splitlines(True)
            new_content = f"{lines[0]}\n{header}\n{''.join(lines[1:])}"
        else:
            new_content = f"{header}\n\n{original_content}"

        with open(file_path, "w", encoding="utf-8", newline="") as f:
            f.write(new_content)
        print(f"Added header to: {file_path}")
    except (IOError, UnicodeDecodeError) as e:
        print(f"Error processing {file_path}: {e}")


def main():
    """Main function to walk through directories and add headers."""
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for root, dirs, files in os.walk(project_root):
        dirs[:] = [d for d in dirs if d not in [".git", "build"]]
        for file in files:
            file_path = os.path.join(root, file)
            _, ext = os.path.splitext(file)
            header = FILE_MAPPINGS.get(ext) or FILE_MAPPINGS.get(file)
            if header and not has_header(file_path):
                add_header(file_path, header)


if __name__ == "__main__":
    main()
