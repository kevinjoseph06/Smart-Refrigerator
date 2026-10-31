import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

fig, ax = plt.subplots(figsize=(9, 9.6))
ax.set_xlim(0, 10)
ax.set_ylim(1.0, 12.6)
ax.axis("off")

def box(x, y, w, h, title, lines, facecolor, title_size=11.5, body_size=9.3, text_color="black"):
    b = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.02,rounding_size=0.08",
                        linewidth=1.3, edgecolor="black", facecolor=facecolor, zorder=2)
    ax.add_patch(b)
    ax.text(x + w/2, y + h - 0.22, title, ha="center", va="top",
             fontsize=title_size, fontweight="bold", zorder=3, color=text_color)
    text = "\n".join(lines)
    ax.text(x + w/2, y + h - 0.58, text, ha="center", va="top",
             fontsize=body_size, zorder=3, linespacing=1.55, color=text_color)

def arrow(x1, y1, x2, y2):
    a = FancyArrowPatch((x1, y1), (x2, y2), arrowstyle="-|>", mutation_scale=16,
                         linewidth=1.3, color="black", zorder=1)
    ax.add_patch(a)

ax.text(5, 12.45, "Smart Refrigerator Health Monitoring & Energy Optimization\nSystem Architecture",
         ha="center", va="top", fontsize=13.5, fontweight="bold")

# Row 1: Sensors
sw, sh = 2.7, 1.65
sy = 9.9
box(0.5, sy, sw, sh, "DS18B20", ["Waterproof probe", "Compartment temp.", "(GPIO 4)"], "#DDEBF7")
box(3.65, sy, sw, sh, "DHT22", ["Humidity + ambient", "temperature", "(GPIO 16)"], "#DDEBF7")
box(6.8, sy, sw, sh, "MC-38 Reed Switch", ["Door open / closed", "state", "(GPIO 17, PULLUP)"], "#DDEBF7")

cy_top = 8.05
arrow(1.85, sy, 5.0, cy_top + 1.5)
arrow(5.0, sy, 5.0, cy_top + 1.5)
arrow(8.15, sy, 5.0, cy_top + 1.5)

# Row 2: controller
cw, ch = 9.0, 1.5
box(0.5, cy_top, cw, ch, "ESP32-WROOM-32 Edge Controller",
    ["Non-blocking 2s sensor sampling  •  Validity / staleness check (10s timeout)",
     "Door debounce & dual-threshold event detection (30s warning / 60s alert)"],
    "#E2DAF2", title_size=12, body_size=9.2)

arrow(5.0, cy_top, 5.0, cy_top - 1.5)

# Row 3: analytics
ay = cy_top - 2.95
ah = 1.5
box(0.5, ay, cw, ah, "On-Device Analytics Engine", [], "#2B2F38", title_size=12, text_color="white")
ax.text(5.0, ay + ah - 0.62,
        "Health Score (40/20/20/20)   •   Recovery-Time Analysis (GOOD/MOD/SLOW)\n"
        "Software Energy / Duty-Cycle Model   •   Predictive Maintenance (3-day, ≥15%)",
        ha="center", va="top", fontsize=9.1, color="white", linespacing=1.6)

arrow(1.9, ay, 1.9, ay - 1.15)
arrow(5.0, ay, 5.0, ay - 1.15)
arrow(8.1, ay, 8.1, ay - 1.15)

# Row 4: outputs
oy = ay - 2.65
ow, oh = 2.7, 1.5
box(0.5, oy, ow, oh, "Local Web Dashboard",
    ["ESP32 SoftAP + HTTP", "Self-contained HTML/CSS/JS", "No cloud, no CDN"], "#DCE9D5", title_size=10.2, body_size=8.5)
box(3.65, oy, ow, oh, "OLED + Buzzer",
    ["On-device status display", "Audible door-open", "alert (active buzzer)"], "#DCE9D5", title_size=10.2, body_size=8.5)
box(6.8, oy, ow, oh, "Persistence",
    ["Preferences/NVS:", "software day-counter", "LittleFS: 14-day history"], "#DCE9D5", title_size=10.2, body_size=8.5)

a2 = FancyArrowPatch((8.15, oy + oh), (8.65, cy_top), connectionstyle="arc3,rad=0.35",
                      arrowstyle="-|>", mutation_scale=14, linewidth=1.1, color="gray",
                      linestyle="--", zorder=1)
ax.add_patch(a2)
ax.text(9.5, (oy+oh+cy_top)/2 - 0.1, "restored\non boot", fontsize=7.4, color="gray", ha="center", style="italic")

ax.text(5.0, oy - 0.35, "Figure 1. Smart Refrigerator staged edge-analytics architecture: sensing → validation/event detection → scoring/analytics → local presentation & persistence.",
        ha="center", va="top", fontsize=8.8, style="italic")

import os
out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "architecture.png")
plt.savefig(out_path, dpi=220, bbox_inches="tight", facecolor="white")
print(f"saved to {out_path}")
