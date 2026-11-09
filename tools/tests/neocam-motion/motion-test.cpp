/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * NeoCam's MotionDetector on the build host with synthetic frames: what
 * should be motion (squares that move or appear) and what shouldn't (noise,
 * exposure, light, contrast and focus changes). Build and run (SupportDefs.h
 * here stands in for Haiku's):
 *   g++ -O2 -I tools/tests/neocam-motion -I haiku/src/apps/neocam
 *     -o run/motion-test tools/tests/neocam-motion/motion-test.cpp
 *     haiku/src/apps/neocam/MotionDetector.cpp && run/motion-test
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "MotionDetector.h"

static const int W = 640, H = 480;
static std::vector<float> scene(W * H);

static float clampf(float v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void make_scene()
{
	srand(1);
	// shapes, edges, a bright window
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float v = 90 + 60 * sinf(x * 0.02f) * cosf(y * 0.015f);
			if ((x / 37 + y / 29) % 3 == 0) v += 50;
			if (x > 480 && y < 200) v = 252;	// clipped window
			if (x > 100 && x < 260 && y > 250) v = 30;	// dark bag
			scene[y * W + x] = v;
		}
}

// gain, offset, contrast, blur, a square (x, y, size, value)
static void frame(std::vector<uint8>& bits, float gain, float offset,
	float contrast, bool blur, int sx, int sy, int size, float value)
{
	std::vector<float> f(scene);
	if (blur) {
		std::vector<float> b(f);
		for (int y = 1; y < H - 1; y++)
			for (int x = 1; x < W - 1; x++) {
				float s = 0;
				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++)
						s += f[(y + dy) * W + x + dx];
				b[y * W + x] = s / 9;
			}
		f = b;
	}
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float v = f[y * W + x];
			if (size > 0 && x >= sx && x < sx + size && y >= sy
				&& y < sy + size)
				v = value;
			v = (v - 128) * contrast + 128;
			v = v * gain + offset;
			v += (rand() % 9) - 4;	// noise
			uint8 c = (uint8)clampf(v);
			uint8* p = &bits[(y * W + x) * 4];
			p[0] = p[1] = p[2] = c; p[3] = 255;
		}
}

static int run(const char* name, int sensitivity, int frames,
	float gain, float offset, float contrast, bool blur, int size,
	bool moving, bool expectMotion)
{
	MotionDetector detector;
	detector.SetSensitivity(sensitivity);
	std::vector<uint8> bits(W * H * 4);
	// warm up on the plain scene
	for (int i = 0; i < 40; i++) {
		frame(bits, 1, 0, 1, false, 0, 0, 0, 0);
		detector.Analyze(bits.data(), W, H, W * 4);
	}
	int motion = 0;
	float peak = 0;
	for (int i = 0; i < frames; i++) {
		int sx = moving ? 20 + i * 12 : 300;
		frame(bits, gain, offset, contrast, blur, sx, 150, size, 220);
		float level = detector.Analyze(bits.data(), W, H, W * 4);
		if (level > peak) peak = level;
		if (level >= detector.Threshold()) motion++;
	}
	bool ok = (motion > 0) == expectMotion;
	printf("%-44s sens %3d: %2d/%d analyses motion, peak %.4f, threshold "
		"%.4f  %s\n", name, sensitivity, motion, frames, peak,
		detector.Threshold(), ok ? "ok" : "WRONG");
	return ok ? 0 : 1;
}

int main()
{
	make_scene();
	int bad = 0;
	bad += run("still scene, noise", 100, 40, 1, 0, 1, false, 0, false, false);
	bad += run("exposure: gain 0.8", 100, 40, 0.8f, 0, 1, false, 0, false, false);
	bad += run("exposure: gain 1.25", 50, 40, 1.25f, 0, 1, false, 0, false, false);
	bad += run("offset +25", 50, 40, 1, 25, 1, false, 0, false, false);
	bad += run("contrast 1.5", 50, 40, 1, 0, 1.5f, false, 0, false, false);
	bad += run("focus: 3x3 blur", 50, 40, 1, 0, 1, true, 0, false, false);
	bad += run("moving square 120 (4.7%)", 50, 20, 1, 0, 1, false, 120, true, true);
	bad += run("moving square 60 (1.2%)", 50, 20, 1, 0, 1, false, 60, true, false);
	bad += run("moving square 60 (1.2%)", 80, 20, 1, 0, 1, false, 60, true, true);
	bad += run("moving square 30 (0.3%)", 100, 20, 1, 0, 1, false, 30, true, true);
	bad += run("square that stays (appears)", 50, 60, 1, 0, 1, false, 120, false, true);
	bad += run("moving square + gain 0.9", 50, 20, 0.9f, 0, 1, false, 120, true, true);
	printf("%d wrong\n", bad);
	return bad;
}
