"""Radiation shield for the two DHT22 sensors (stacked conical plates on 3 rods).

Generates STL files for printing and a preview image. All sizes are in mm.

Usage:
    pip install trimesh manifold3d numpy matplotlib
    python cad/make_shield.py

Output: cad/shield_plate.stl, cad/shield_cap.stl, cad/shield_spacer.stl,
        cad/shield_assembly.stl, docs/images/shield-3d.png
"""
import pathlib

import matplotlib
import numpy as np
import trimesh

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

# ---- parameters -------------------------------------------------------------
PLATE_R = 48.0       # outer radius of a louvre plate
HOLE_R = 18.0        # centre opening for the sensors and air flow
DROP = 6.0           # how much lower the rim is than the inner edge (rain runs off)
THICK = 2.0          # plate thickness
PITCH = 10.0         # distance between plates (vertical)
N_PLATES = 4         # open plates under the cap
CAP_R = 52.0         # the top cap is a bit wider than the plates
ROD_R = 1.6          # M3 rod
ROD_CIRCLE = 33.0    # rods sit on this circle, 3 of them at 120 degrees
SPACER_OUT_R = 3.5
SEGMENTS = 96
# cap profile (local z): top 3.0 at the centre, bottom 3.0 - THICK; the rod holes are blind
CAP_BOTTOM_AT_RODS = (3.0 - THICK) - DROP * ROD_CIRCLE / CAP_R   # about -2.8
CAP_HOLE_TOP = CAP_BOTTOM_AT_RODS + 1.0                           # leaves 1 mm roof above the rod

ROOT = pathlib.Path(__file__).resolve().parent.parent


def z_top(r, r_in, r_out):
    """Height of the top surface of a cone plate at radius r (inner edge at z = 0)."""
    return -DROP * (r - r_in) / (r_out - r_in)


def revolve(profile):
    """Revolve a closed (r, z) polygon around the z axis."""
    pts = list(profile)[::-1]          # reversed order gives outward-facing normals
    pts.append(pts[0])                 # close the polygon
    mesh = trimesh.creation.revolve(np.array(pts, dtype=float), sections=SEGMENTS)
    mesh.merge_vertices()              # join the seam so the mesh is watertight
    return mesh


def rod_positions():
    angles = np.radians([90, 210, 330])
    return [(ROD_CIRCLE * np.cos(a), ROD_CIRCLE * np.sin(a)) for a in angles]


def cylinder(radius, z0, z1, x=0.0, y=0.0):
    # 50 sections (not 48): a hole vertex must not land exactly on an edge of the 96-section cone
    m = trimesh.creation.cylinder(radius=radius, height=z1 - z0, sections=50)
    m.apply_translation([x, y, (z0 + z1) / 2])
    return m


def make_plate():
    """Conical open plate with a centre hole and 3 rod holes."""
    prof = [
        (HOLE_R, 0.0),
        (PLATE_R, -DROP),
        (PLATE_R, -DROP - THICK),
        (HOLE_R, -THICK),
    ]
    plate = revolve(prof)
    for x, y in rod_positions():
        plate = plate.difference(cylinder(ROD_R, -20, 5, x, y))
    return plate


def make_cap():
    """Closed cone roof with 3 rod holes (the holes do not go through the top)."""
    prof = [
        (0.0, 3.0),
        (CAP_R, -DROP + 3.0),
        (CAP_R, -DROP - THICK + 3.0),
        (0.0, 3.0 - THICK),
    ]
    cap = revolve(prof)
    for x, y in rod_positions():
        cap = cap.difference(cylinder(ROD_R, -20, CAP_HOLE_TOP, x, y))
    return cap


def spacer_length():
    """Gap between the top of one plate and the bottom of the next at the rod circle."""
    zt = z_top(ROD_CIRCLE, HOLE_R, PLATE_R)
    return (PITCH + zt - THICK) - zt


def make_spacer():
    length = spacer_length()
    tube = cylinder(SPACER_OUT_R, 0, length)
    return tube.difference(cylinder(ROD_R, -1, length + 1))


def assemble():
    plate, cap, spacer = make_plate(), make_cap(), make_spacer()
    parts = []
    zt = z_top(ROD_CIRCLE, HOLE_R, PLATE_R)
    # plate i has its inner edge at z = i * PITCH, the cap sits above the last plate
    for i in range(N_PLATES):
        p = plate.copy()
        p.apply_translation([0, 0, i * PITCH])
        parts.append(("plate", p))
        s_z = i * PITCH + zt
        for x, y in rod_positions():
            s = spacer.copy()
            s.apply_translation([x, y, s_z])
            parts.append(("spacer", s))
    # the cap rests on the last spacers: its underside at the rod circle = top of those spacers
    spacer_top = (N_PLATES - 1) * PITCH + zt + spacer_length()
    cap_z = spacer_top - CAP_BOTTOM_AT_RODS
    c = cap.copy()
    c.apply_translation([0, 0, cap_z])
    parts.append(("cap", c))
    # three rods through everything, ending inside the blind holes of the cap
    z_bottom = -DROP - THICK - 4
    z_top_rod = cap_z + CAP_HOLE_TOP - 0.2
    for x, y in rod_positions():
        parts.append(("rod", cylinder(ROD_R * 0.95, z_bottom, z_top_rod, x, y)))
    return parts


def render(parts, out_png):
    colors = {"plate": (0.93, 0.93, 0.93), "cap": (0.85, 0.85, 0.88),
              "spacer": (0.45, 0.45, 0.5), "rod": (0.3, 0.3, 0.33),
              "dht22": (0.2, 0.45, 0.9)}
    light = np.array([0.4, -0.5, 0.75])
    light /= np.linalg.norm(light)
    fig = plt.figure(figsize=(8, 8), dpi=130)
    ax = fig.add_subplot(111, projection="3d")
    for kind, m in parts:
        tri = m.triangles
        n = m.face_normals
        shade = 0.45 + 0.55 * np.clip(n @ light, 0, 1)
        fc = np.array([colors[kind]]) * shade[:, None]
        pc = Poly3DCollection(tri, facecolors=fc, edgecolors="none")
        ax.add_collection3d(pc)
    allv = np.vstack([m.vertices for _, m in parts])
    mid = (allv.max(0) + allv.min(0)) / 2
    span = (allv.max(0) - allv.min(0)).max() / 2
    ax.set_xlim(mid[0] - span, mid[0] + span)
    ax.set_ylim(mid[1] - span, mid[1] + span)
    ax.set_zlim(mid[2] - span * 0.8, mid[2] + span * 0.8)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=14, azim=-65)
    ax.set_axis_off()
    fig.savefig(out_png, bbox_inches="tight", facecolor="white")


def main():
    plate, cap, spacer = make_plate(), make_cap(), make_spacer()
    for name, m in (("plate", plate), ("cap", cap), ("spacer", spacer)):
        path = ROOT / "cad" / f"shield_{name}.stl"
        m.export(path)
        # STL stores float32: check the file itself, not only the mesh in memory
        check = trimesh.load(path)
        assert check.is_watertight and check.is_volume, f"{name}.stl is not a closed solid"

    parts = assemble()
    trimesh.util.concatenate([m for _, m in parts]).export(ROOT / "cad" / "shield_assembly.stl")

    # two DHT22 bodies (15.1 x 7.7 x 25) only for the preview, to show the scale
    preview = list(parts)
    for x in (-9.0, 9.0):
        box = trimesh.creation.box(extents=(15.1, 7.7, 25.0))
        box.apply_translation([x, 0, 12.0])
        preview.append(("dht22", box))
    # cut away the front half (y < 0) so the inside is visible in the preview
    cut = []
    for kind, m in preview:
        half = m.slice_plane([0, 0, 0], [0, 1, 0], cap=True)
        if len(half.faces):
            cut.append((kind, half))
    render(cut, ROOT / "docs" / "images" / "shield-3d.png")

    print("plate  :", plate.bounds.round(1).tolist())
    print("spacer length:", round(spacer_length(), 2))
    print("assembly bounds:", trimesh.util.concatenate([m for _, m in parts]).bounds.round(1).tolist())


if __name__ == "__main__":
    main()
