#!/usr/bin/env python3
"""Convert the low-poly asteroid models into engine shape data.

The models in assets/models/asteroids replace the cartridge's whole-object
asteroid sprites. Each JSON file lists a palette measured from the sprite it
replaces; this tool maps those colours onto the Super FX palette slots the
sprite's texels use, so the replacement follows every level palette exactly
as the sprite does.

    python tools/generate_asteroid_models.py          # rewrite the header
    python tools/generate_asteroid_models.py --check  # fail if it is stale
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/models/asteroids"
DESTINATION = ROOT / "src/render/generated/asteroid_models_data.hpp"

# Model space is +X right, +Y up, +Z toward the viewer, with the sprite's
# square spanning [-1, 1]. Engine space is +Y down and +Z away from the
# camera. UNIT engine units span one model unit; the renderer scales the
# shape to each sprite's world size.
UNIT = 256

# Sprite texel slots in the Super FX palette, measured from the retail
# textures (the same texels appear in Original and EX).
GREY_RAMP = {0x182818: 9, 0x406048: 10, 0x709080: 11, 0xA8B8A8: 12, 0xD0E0D0: 13, 0xF0F8F8: 14}
ACCENTS = {0x881008: 1, 0xD04828: 2, 0xE8A840: 3, 0xF8D868: 4}

# name, source file, FNV-1a of the retail texels it replaces, lit palette
# entries (dark to light; the rest are unlit accents such as glowing eyes).
MODELS = [
    ("grey", "grey.json", 0x69637EFD, 6),
    ("orange", "orange.json", 0xA3B45AC9, 6),
    ("face", "face.json", 0x496C9F82, 6),
    ("crater", "crater.json", 0x21683FCC, 6),
]


def rgb(entry: list[int]) -> int:
    return (entry[0] << 16) | (entry[1] << 8) | entry[2]


def slot(colour: int) -> int:
    for table in (GREY_RAMP, ACCENTS):
        if colour in table:
            return table[colour]
    raise ValueError(f"colour {colour:06x} is not a known asteroid texel colour")


def cross(u, v):
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


def convert(name: str, data: dict, texture_hash: int, lit: int) -> str:
    palette = [rgb(entry) for entry in data["palette_rgb"]]
    ramp = [slot(colour) for colour in palette[:lit]]
    accents = [slot(colour) for colour in palette[lit:]]
    vertices = [(round(x * UNIT), round(-y * UNIT), round(-z * UNIT)) for x, y, z in data["vertices"]]
    if len(vertices) > 256:
        raise ValueError(f"{name}: shapes address at most 256 vertices")
    faces, normals = [], []
    for (a, b, c), (nx, ny, nz) in zip(data["faces"], data["face_normals"]):
        normal = (nx, -ny, -nz)
        pa, pb, pc = (vertices[i] for i in (a, b, c))
        winding = cross([pb[i] - pa[i] for i in range(3)], [pc[i] - pa[i] for i in range(3)])
        # The renderer shows a face when (b-a)x(c-a) points away from the
        # centre of view, so order every triangle by its outward normal.
        if sum(winding[i] * normal[i] for i in range(3)) < 0:
            b, c = c, b
        faces.append((a, b, c))
        # Retail face normals point inward: every MYSHIP/BASE/FACE triangle's
        # (b-a)x(c-a) opposes its stored normal. SHADESTAB2 lighting assumes it.
        normals.append(tuple(max(-127, min(127, round(-n * 127))) for n in normal))
    colours = data["face_palette_index"]
    if len(colours) != len(faces) or max(colours) >= len(palette):
        raise ValueError(f"{name}: face colours do not match the palette")

    def rows(values, per_line):
        items = ["{" + ",".join(str(v) for v in value) + "}" for value in values]
        return ",\n".join("    " + ",".join(items[i:i + per_line]) for i in range(0, len(items), per_line))

    return (
        f"inline constexpr std::array<std::array<std::int16_t,3>,{len(vertices)}> {name}_vertices{{{{\n"
        f"{rows(vertices, 8)}}}}};\n"
        f"inline constexpr std::array<std::array<std::uint8_t,3>,{len(faces)}> {name}_faces{{{{\n"
        f"{rows(faces, 10)}}}}};\n"
        f"inline constexpr std::array<std::array<std::int8_t,3>,{len(normals)}> {name}_normals{{{{\n"
        f"{rows(normals, 10)}}}}};\n"
        f"inline constexpr std::array<std::uint8_t,{len(colours)}> {name}_colours{{\n"
        + ",\n".join("    " + ",".join(str(c) for c in colours[i:i + 40]) for i in range(0, len(colours), 40))
        + "};\n"
        f"inline constexpr ModelData {name}{{0x{texture_hash:08x}U,"
        f"{{{','.join(str(s) for s in ramp)}}},{len(ramp)},"
        f"{{{','.join(str(s) for s in accents) or '0'}}},{len(accents)},"
        f"{name}_vertices,{name}_faces,{name}_normals,{name}_colours}};\n"
    )


def generate() -> str:
    digest = hashlib.sha256()
    body = []
    for name, filename, texture_hash, lit in MODELS:
        data = json.loads((SOURCE / filename).read_text(encoding="utf-8"))
        digest.update(json.dumps(data, sort_keys=True).encode())
        body.append(convert(name, data, texture_hash, lit))
    return (
        "// Generated by tools/generate_asteroid_models.py; do not edit.\n"
        f"// Source SHA-256: {digest.hexdigest()}\n"
        "#pragma once\n"
        "#include <array>\n#include <cstdint>\n#include <span>\n\n"
        "namespace starfox::render::asteroid_data {\n"
        f"inline constexpr int unit={UNIT};\n"
        "struct ModelData {\n"
        "    std::uint32_t texture_hash;\n"
        "    std::array<std::uint8_t,6> ramp; std::size_t ramp_size;\n"
        "    std::array<std::uint8_t,4> accents; std::size_t accent_count;\n"
        "    std::span<const std::array<std::int16_t,3>> vertices;\n"
        "    std::span<const std::array<std::uint8_t,3>> faces;\n"
        "    std::span<const std::array<std::int8_t,3>> normals;\n"
        "    std::span<const std::uint8_t> colours;\n"
        "};\n"
        + "\n".join(body)
        + f"inline constexpr std::array<const ModelData*,{len(MODELS)}> models{{"
        + ",".join(f"&{name}" for name, *_ in MODELS)
        + "};\n}\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = generate()
    if args.check:
        if not DESTINATION.exists() or DESTINATION.read_text(encoding="utf-8") != text:
            print(f"{DESTINATION.relative_to(ROOT)} is stale; run tools/generate_asteroid_models.py", file=sys.stderr)
            return 1
        return 0
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    DESTINATION.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
