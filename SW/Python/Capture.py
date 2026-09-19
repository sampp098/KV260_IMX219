import numpy as np
import matplotlib.pyplot as plt
import cv2

WIDTH = 640
HEIGHT = 480

words = np.fromfile(
    "D:/KV260_Camera/MOHSEN_Camera/capture.bin",
    dtype="<u4"
)

# --------------------------------------------------
# Unpack two RAW10 pixels from each 32-bit BRAM word
# --------------------------------------------------
p0 = words & 0x3FF
p1 = (words >> 10) & 0x3FF

pixels = np.empty(words.size * 2, dtype=np.uint16)

pixels[0::2] = p0
pixels[1::2] = p1

raw10 = pixels[:WIDTH * HEIGHT].reshape(HEIGHT, WIDTH)

print("RAW min =", raw10.min())
print("RAW max =", raw10.max())
print("RAW mean =", raw10.mean())


# --------------------------------------------------
# Remove approximate black level
# IMX219 RAW10 black level is often around 64
# --------------------------------------------------
raw = raw10.astype(np.int32)

raw = raw - 64

raw[raw < 0] = 0


# --------------------------------------------------
# Contrast normalization
#
# Use percentiles so a few extreme pixels do not
# dominate the scaling.
# --------------------------------------------------
low = np.percentile(raw, 1)
high = np.percentile(raw, 99)

print("1% percentile  =", low)
print("99% percentile =", high)

raw_normalized = np.clip(
    (raw - low) * 255.0 / max(high - low, 1),
    0,
    255
).astype(np.uint8)


# --------------------------------------------------
# First show normalized Bayer image
# --------------------------------------------------
plt.figure()
plt.imshow(raw_normalized, cmap="gray")
plt.title("Normalized RAW Bayer")
plt.axis("off")


# --------------------------------------------------
# Demosaic
#
# Try BG first.
# --------------------------------------------------
rgb = cv2.cvtColor(
    raw_normalized,
    cv2.COLOR_BayerBG2RGB
)


# --------------------------------------------------
# Optional gamma correction
# --------------------------------------------------
gamma = 0.6

rgb_float = rgb.astype(np.float32) / 255.0
rgb_gamma = np.power(rgb_float, gamma)
rgb_gamma = np.clip(rgb_gamma * 255, 0, 255).astype(np.uint8)


plt.figure()
plt.imshow(rgb_gamma)
plt.title("Demosaiced IMX219")
plt.axis("off")

plt.show()