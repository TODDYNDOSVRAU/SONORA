from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1] / "assets" / "images"
ROOT.mkdir(parents=True, exist_ok=True)
BASE = 1024
SCALE = 4

BG = (8, 13, 15, 255)
ACCENT = (88, 230, 208, 255)
ACCENT_DIM = (42, 131, 121, 255)
ACCENT_SOFT = (28, 86, 80, 255)
WHITE = (235, 255, 251, 255)


def mark(size: int, scale: float = 1.0, color=ACCENT, bg=None) -> Image.Image:
    hi = size * SCALE
    image = Image.new("RGBA", (hi, hi), bg or (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    c = hi / 2
    u = size * SCALE / BASE * scale

    def px(n: float) -> int:
        return round(n * u)

    # Open circular field: an intentional right-side gap suggests a movable sound field.
    radius = px(248)
    width = max(1, px(22))
    box = (c - radius, c - radius, c + radius, c + radius)
    draw.arc(box, start=48, end=312, fill=color, width=width)

    # Three short wave arcs open to the right; the five dots are the selectable positions.
    for r, col, w in ((304, ACCENT_SOFT, 10), (354, ACCENT_DIM, 9), (401, ACCENT_SOFT, 8)):
        rr = px(r)
        draw.arc((c - rr, c - rr, c + rr, c + rr), start=302, end=418, fill=col, width=max(1, px(w)))

    dot_specs = [
        (0, -264, 22, ACCENT_DIM),
        (0, 264, 22, ACCENT_DIM),
        (-264, 0, 22, ACCENT_DIM),
        (264, 0, 22, ACCENT_DIM),
        (0, 0, 30, color),
    ]
    for dx, dy, radius_px, dot_color in dot_specs:
        x, y, r = c + px(dx), c + px(dy), px(radius_px)
        draw.ellipse((x - r, y - r, x + r, y + r), fill=dot_color)
        if radius_px >= 30:
            inner = px(9)
            draw.ellipse((x - inner, y - inner, x + inner, y + inner), fill=WHITE)

    return image.resize((size, size), Image.Resampling.LANCZOS)


def solid(size: int, color) -> Image.Image:
    return Image.new("RGBA", (size, size), color)


def save(image: Image.Image, filename: str) -> None:
    image.save(ROOT / filename, format="PNG", optimize=True)


# Full square icon has no baked-in rounded corners; the launcher applies its own mask.
full = mark(BASE, 0.9, ACCENT, BG)
save(full, "icon.png")

# Adaptive foreground uses transparency and a centered safe-area mark; background is separate.
save(mark(BASE, 0.68, ACCENT), "android-icon-foreground.png")
save(solid(BASE, BG), "android-icon-background.png")
save(mark(432, 0.68, (255, 255, 255, 255)), "android-icon-monochrome.png")
save(mark(512, 0.76, ACCENT), "splash-icon.png")
save(mark(64, 0.62, ACCENT, BG), "favicon.png")
