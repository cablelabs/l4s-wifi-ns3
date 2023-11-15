#!/usr/bin/env python3

# This is a basic plotting program that uses Python Matplotlib to plot five time-series:
# 1) Wifi throughput (wifi-dequeue-throughput.dat)
# 2) Cubic primary foreground TCP throughput (cubic-throughput.dat)
# 3) Prague primary foreground TCP throughput (prague-throughput.dat)
# 4) Wifi AC_BE queue bytes after dequeue (wifi-dequeue-bytes.dat)
# 5) Overlying DualPI2 bytes in queue (both C- and L- queues) (wifi-dualpi2-bytes.dat)
#
# The output file is named 'l4s-wifi.pdf'

import matplotlib
matplotlib.use("agg")
import matplotlib.pyplot as plt
import sys
import numpy as np
import re
import os

plotname = 'l4s-wifi.pdf'
# Plot title will be any arguments passed to this script
if len(sys.argv) > 1 and sys.argv[1] is not None:
    title = ' '.join(sys.argv[1:])
else:
    title = "Untitled"
space_between_subplots = 1.0

f = open('wifi-dequeue-throughput.dat', 'r')
wifi_time = []
wifi_samples_plot = []
wifi_samples_avg = []
for line in f:
    columns = line.split()
    t = float(columns[0])
    wifi_time.append(t)
    wifi_samples_plot.append(float(columns[1]))
    if (t >= 2 and t <= 8):
        wifi_samples_avg.append (float(columns[1]))
f.close()

cubic_time = []
cubic_samples_plot = []
cubic_samples_avg = []
if os.path.exists('cubic-throughput.dat'):
    f = open('cubic-throughput.dat', 'r')
    for line in f:
        columns = line.split()
        t = float(columns[0])
        cubic_time.append(t)
        cubic_samples_plot.append (float(columns[1]))
        if (t >= 2 and t <= 8):
            cubic_samples_avg.append (float(columns[1]))
    f.close()
    
prague_time = []
prague_samples_plot = []
prague_samples_avg = []
if os.path.exists('prague-throughput.dat'):
    f = open('prague-throughput.dat', 'r')
    for line in f:
        columns = line.split()
        t = float(columns[0])
        prague_time.append(t)
        prague_samples_plot.append (float(columns[1]))
        if (t >= 2 and t <= 8):
            prague_samples_avg.append (float(columns[1]))
    f.close()

f = open('wifi-queue-bytes.dat', 'r')
bytes_time = []
bytes_samples_plot = []
bytes_samples_avg = []
for line in f:
    columns = line.split()
    t = float(columns[0])
    bytes_time.append(t)
    bytes_samples_plot.append (float(columns[1]))
    if (t >= 2 and t <= 8):
        bytes_samples_avg.append (float(columns[1]))
f.close()

f = open('wifi-dualpi2-bytes.dat', 'r')
dualpi2_time = []
dualpi2_samples_plot = []
dualpi2_samples_avg = []
for line in f:
    columns = line.split()
    t = float(columns[0])
    dualpi2_time.append(t)
    dualpi2_samples_plot.append (float(columns[1]))
    if (t >= 2 and t <= 8):
        dualpi2_samples_avg.append (float(columns[1]))
f.close()

# Create subplots and orient the page in portrait mode
fig, (ax1, ax2, ax3, ax4, ax5) = plt.subplots(nrows=5, figsize=(8,11), sharex=False, sharey=False)
fig.subplots_adjust(hspace=space_between_subplots)
ax1.plot(wifi_time, wifi_samples_plot, marker='', color='black', label='100ms throughput samples')
ax1.set_xlim(0,)
ax1.set_ylim(0,120)
ax1.set_xlabel('Time (s)')
ax1.set_ylabel('Throughput (Mbps)')
wifi_avg = round(sum(wifi_samples_avg)/len(wifi_samples_avg),1)
wifi_label = r'Avg {} Mbps (2s < t < 8s)'.format(wifi_avg)
ax1.axhline(y = wifi_avg, color = 'r', linestyle = 'dashed', label=wifi_label)
ax1.set_title("Wifi throughput")
ax1.legend(loc='upper right', framealpha=1, prop={"size" : 6})

ax2.plot(cubic_time, cubic_samples_plot, marker='', color='black', label='100ms throughput samples')
ax2.set_xlim(0,)
ax2.set_ylim(0,120)
ax2.set_xlabel('Time (s)')
ax2.set_ylabel('Throughput (Mbps)')
if len(cubic_samples_avg):
    cubic_avg = round(sum(cubic_samples_avg)/len(cubic_samples_avg),1)
    cubic_label = r'Avg {} Mbps (2s < t < 8s)'.format(cubic_avg)
    ax2.axhline(y = cubic_avg, color = 'r', linestyle = 'dashed', label=cubic_label)
ax2.set_title("Cubic throughput")
ax2.legend(loc='upper right', framealpha=1, prop={"size" : 6})

ax3.plot(prague_time, prague_samples_plot, marker='', color='black', label='100ms throughput samples')
ax3.set_xlim(0,)
ax3.set_ylim(0,120)
ax3.set_xlabel('Time (s)')
ax3.set_ylabel('Throughput (Mbps)')
if len(prague_samples_avg):
    prague_avg = round(sum(prague_samples_avg)/len(prague_samples_avg),1)
    prague_label = r'Avg {} Mbps (2s < t < 8s)'.format(prague_avg)
    ax3.axhline(y = prague_avg, color = 'r', linestyle = 'dashed', label=prague_label)
ax3.set_title("Prague throughput")
ax3.legend(loc='upper right', prop={"size" : 6})

ax4.plot(bytes_time, bytes_samples_plot, marker='', color='black', label='Bytes')
ax4.set_ylim(0,600000)
ax4.set_xlabel('Time (s)')
ax4.set_ylabel('Bytes in AC_BE')
ax4.set_title("Bytes in Wi-Fi device AC_BE queue")
ax4.legend(loc='upper right', prop={"size" : 6})

ax5.plot(dualpi2_time, dualpi2_samples_plot, marker='', color='black', label='Bytes')
ax5.set_ylim(0,600000)
ax5.set_xlabel('Time (s)')
ax5.set_ylabel('Bytes in DualPI2')
ax5.set_title("Bytes in overlying DualPI2 AC_BE queue")
ax5.legend(loc='upper right', prop={"size": 6})

fig.suptitle(title)
plt.savefig(plotname, format='pdf')
plt.close()
