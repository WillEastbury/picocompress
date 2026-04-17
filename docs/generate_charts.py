#!/usr/bin/env python3
"""Generate comparison charts for picocompress multi-profile vs heatshrink on Pico 2W."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import os

OUT = r'C:\source\picocompress\docs'

# ---- Pico 2W data (RP2350, Cortex-M33 @ 150 MHz) ----
profiles = ['PC-Micro', 'PC-Minimal', 'PC-Balanced', 'PC-Q3', 'PC-Q4', 'heatshrink']
enc_ram_kb = [0.8, 1.2, 3.1, 5.6, 10.6, 2.0]
dec_ram_kb = [0.5, 1.1, 1.5, 2.0, 3.0, 2.3]
total_ram  = [e+d for e,d in zip(enc_ram_kb, dec_ram_kb)]

# Per-payload data  [Micro, Minimal, Balanced, Q3, Q4, heatshrink]
data = {
    'json-508':    {'ratio': [2.67, 2.92, 2.97, 2.97, 2.97, 2.13],
                    'enc_us': [2135, 2190, 2145, 3999, 2512, 30066],
                    'dec_mbs': [8.91, 9.58, 9.07, 8.61, 8.33, 1.62]},
    'pattern-508': {'ratio': [2.66, 3.71, 3.71, 3.71, 3.71, 3.46],
                    'enc_us': [2073, 1285, 1317, 1589, 1362, 14626],
                    'dec_mbs': [9.96, 10.37, 9.77, 9.24, 8.91, 2.17]},
    'prose-4K':    {'ratio': [5.00, 11.22, 11.38, 11.38, 11.38, 6.87],
                    'enc_us': [8245, 2242, 2804, 4550, 4303, 17887],
                    'dec_mbs': [10.32, 11.67, 11.22, 10.48, 9.89, 3.37]},
    'prose-32K':   {'ratio': [5.41, 13.77, 14.04, 14.04, 14.04, 7.84],
                    'enc_us': [59029, 10620, 14953, 24808, 29902, 40499],
                    'dec_mbs': [10.54, 12.04, 11.62, 10.82, 9.76, 3.63]},
    'random-508':  {'ratio': [0.98, 0.99, 0.99, 0.99, 0.99, 0.89],
                    'enc_us': [400, 604, 667, 715, 725, 90929],
                    'dec_mbs': [39.08, 39.08, 33.87, 28.22, 25.40, 0.90]},
}

COLS = ['#1565C0', '#42A5F5', '#2196F3', '#64B5F6', '#90CAF9', '#FF9800']
plt.rcParams.update({'font.size': 11, 'figure.facecolor': 'white'})

def save(fig, name):
    fig.tight_layout()
    for ext in ('svg', 'png'):
        fig.savefig(os.path.join(OUT, f'{name}.{ext}'), format=ext,
                    dpi=150 if ext=='png' else None, bbox_inches='tight')
    plt.close(fig)
    print(f'  {name}')

# ---- Chart 1: Ratio by payload (grouped bars) ----
fig, axes = plt.subplots(1, 4, figsize=(16, 5), sharey=True)
for ax, payload in zip(axes, ['json-508', 'pattern-508', 'prose-4K', 'prose-32K']):
    vals = data[payload]['ratio']
    bars = ax.barh(profiles, vals, color=COLS)
    ax.set_title(payload, fontsize=12, fontweight='bold')
    ax.set_xlim(0, 16)
    for b, v in zip(bars, vals):
        ax.text(v + 0.15, b.get_y() + b.get_height()/2, f'{v:.1f}x',
                va='center', fontsize=9)
axes[0].set_xlabel('Compression Ratio')
fig.suptitle('Compression Ratio — Pico 2W (RP2350, Cortex-M33 @ 150 MHz)', fontsize=14, fontweight='bold', y=1.02)
save(fig, 'chart_ratio_profiles')

# ---- Chart 2: Encode time (log bars) ----
fig, axes = plt.subplots(1, 4, figsize=(16, 5), sharey=True)
for ax, payload in zip(axes, ['json-508', 'pattern-508', 'prose-4K', 'prose-32K']):
    vals = data[payload]['enc_us']
    bars = ax.barh(profiles, vals, color=COLS)
    ax.set_title(payload, fontsize=12, fontweight='bold')
    ax.set_xscale('log')
    ax.set_xlim(100, 200000)
axes[0].set_xlabel('Encode Time (µs, lower=better)')
fig.suptitle('Encode Time — Pico 2W (RP2350, Cortex-M33 @ 150 MHz)', fontsize=14, fontweight='bold', y=1.02)
save(fig, 'chart_encode_profiles')

# ---- Chart 3: Decode throughput ----
fig, axes = plt.subplots(1, 4, figsize=(16, 5), sharey=True)
for ax, payload in zip(axes, ['json-508', 'pattern-508', 'prose-4K', 'prose-32K']):
    vals = data[payload]['dec_mbs']
    bars = ax.barh(profiles, vals, color=COLS)
    ax.set_title(payload, fontsize=12, fontweight='bold')
    ax.set_xlim(0, 14)
    for b, v in zip(bars, vals):
        ax.text(v + 0.1, b.get_y() + b.get_height()/2, f'{v:.1f}',
                va='center', fontsize=9)
axes[0].set_xlabel('Decode MB/s (higher=better)')
fig.suptitle('Decode Throughput — Pico 2W (RP2350, Cortex-M33 @ 150 MHz)', fontsize=14, fontweight='bold', y=1.02)
save(fig, 'chart_decode_profiles')

# ---- Chart 4: RAM efficiency scatter (ratio vs total RAM) ----
fig, ax = plt.subplots(figsize=(10, 6))
for payload, marker, ms in [('json-508','o',120), ('prose-32K','s',120)]:
    for i, prof in enumerate(profiles):
        ax.scatter(total_ram[i], data[payload]['ratio'][i],
                   s=ms, color=COLS[i], marker=marker, edgecolors='black', zorder=5)
        if payload == 'prose-32K':
            ax.annotate(prof, (total_ram[i], data[payload]['ratio'][i]),
                        textcoords="offset points", xytext=(6, -12), fontsize=8)
# Legend for payload markers
ax.scatter([], [], marker='o', c='gray', s=80, label='json-508')
ax.scatter([], [], marker='s', c='gray', s=80, label='prose-32K')
ax.legend(loc='upper left')
ax.set_xlabel('Total RAM (Encode + Decode, KB)')
ax.set_ylabel('Compression Ratio')
ax.set_title('RAM Efficiency — Pico 2W: Ratio vs Total RAM by Profile')
ax.grid(True, alpha=0.3)
ax.set_xlim(0, 15); ax.set_ylim(0, 16)
save(fig, 'chart_ram_efficiency')

# ---- Chart 5: Profile RAM breakdown (stacked bar) ----
fig, ax = plt.subplots(figsize=(10, 4))
x = np.arange(len(profiles))
ax.bar(x, enc_ram_kb, 0.6, label='Encode RAM', color='#1565C0')
ax.bar(x, dec_ram_kb, 0.6, bottom=enc_ram_kb, label='Decode RAM', color='#42A5F5')
ax.set_xticks(x); ax.set_xticklabels(profiles, rotation=15)
ax.set_ylabel('RAM (KB)')
ax.set_title('RAM Footprint by Profile — Pico 2W')
ax.legend()
for i in range(len(profiles)):
    ax.text(i, total_ram[i] + 0.1, f'{total_ram[i]:.1f}K', ha='center', fontsize=9, fontweight='bold')
save(fig, 'chart_ram_breakdown')

print('\nAll charts generated.')

