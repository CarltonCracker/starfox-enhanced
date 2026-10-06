"""Fail if any starfox_scene source can reach a graphics/XR API header."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[1]
forbidden = re.compile(r'(^|/)(vulkan|openxr|SDL3?|d3d|dxgi|Metal)[^/]*(/|\.h|$)|vulkan_|openxr_', re.IGNORECASE)
include = re.compile(rb'^\s*#\s*include\s*([<"])([^>"\r\n]+)[>"]', re.MULTILINE)
cmake = (root / 'cmake/Scene.cmake').read_text(encoding='utf-8')
library = cmake[cmake.index('add_library(starfox_scene'):]
sources = re.findall(r'src/[\w/]+\.cpp', library[:library.index(')')])
if not sources:
    sys.exit('No starfox_scene sources found in cmake/Scene.cmake')


def resolve(name, current):
    for base in (root / 'include', current.parent):
        candidate = (base / name).resolve()
        if candidate.is_file():
            return candidate
    return None


failures = []
for source in sources:
    seen = set()
    stack = [((root / source).resolve(), (source,))]
    while stack:
        path, chain = stack.pop()
        if path in seen:
            continue
        seen.add(path)
        for _, name in include.findall(path.read_bytes()):
            name = name.decode('utf-8')
            if forbidden.search(name):
                failures.append(' -> '.join(chain + (name,)))
                continue
            resolved = resolve(name, path)
            if resolved is not None:
                stack.append((resolved, chain + (resolved.relative_to(root).as_posix(),)))
for failure in failures:
    print('graphics API header reachable from starfox_scene:', failure)
print(f'{len(sources)} starfox_scene sources checked')
sys.exit(1 if failures else 0)
