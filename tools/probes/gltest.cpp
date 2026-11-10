/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * gltest <directory>: fixed OpenGL scenes in a 256x256 BGLView, each read
 * back with glReadPixels and written as <directory>/<scene>.ppm, to compare
 * renderers pixel by pixel (milestone M43): run it on Lima Pipe and with
 * LIBGL_ALWAYS_SOFTWARE=1. Scenes:
 *   lit       a sphere of quad strips, two lights with specular highlights,
 *             a local viewer (as GLTeapot lights its teapot)
 *   prims     the same quad drawn five ways: triangles, quads, quad strip,
 *             polygon, triangle fan
 *   texture   a checkerboard texture on a quad, nearest and linear
 *   lines     lines and points of several widths
 *   fog       depth-tested overlapping quads in linear fog
 * gltest bench <width> <height> <seconds>: the "lit" scene with nine
 * spheres, drawn and swapped as fast as it goes in a window of that size;
 * prints frames per second.
 * gltest cells <n> <seconds>: as the GLife screen saver draws, in a
 * 400x400 window: half of an n x n grid of lit cubes, each two quad strips
 * with a colour and a material change inside them (four draws a cube);
 * prints frames per second and the team's CPU time per frame.
 * Prints the renderer string. Build: $CXX -O2 $HAIKU_CPPFLAGS -o gltest
 * gltest.cpp -lbe -lGL.
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <GLView.h>
#include <OS.h>
#include <Window.h>

#include <GL/gl.h>


static const int kSize = 256;
static const char* sDirectory;
static int sBenchWidth;
static int sBenchHeight;
static int sBenchSeconds;
static int sCells;		// "cells": the grid's side
static bool sThread;	// the bench in a thread of its own, not the window's


static void
save(const char* name)
{
	glFinish();
	static uint8 pixels[kSize * kSize * 4];
	glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.ppm", sDirectory, name);
	FILE* file = fopen(path, "wb");
	if (file == NULL) {
		perror(path);
		return;
	}
	fprintf(file, "P6\n%d %d\n255\n", kSize, kSize);
	// top row first
	for (int y = kSize - 1; y >= 0; y--) {
		for (int x = 0; x < kSize; x++)
			fwrite(pixels + (y * kSize + x) * 4, 1, 3, file);
	}
	fclose(file);
}


static void
sphere(float radius, int slices, int stacks)
{
	for (int i = 0; i < stacks; i++) {
		float a0 = M_PI * (-0.5 + (float)i / stacks);
		float a1 = M_PI * (-0.5 + (float)(i + 1) / stacks);
		glBegin(GL_QUAD_STRIP);
		for (int j = 0; j <= slices; j++) {
			float b = 2 * M_PI * j / slices;
			float x = cosf(b);
			float y = sinf(b);
			glNormal3f(x * cosf(a1), y * cosf(a1), sinf(a1));
			glVertex3f(radius * x * cosf(a1), radius * y * cosf(a1),
				radius * sinf(a1));
			glNormal3f(x * cosf(a0), y * cosf(a0), sinf(a0));
			glVertex3f(radius * x * cosf(a0), radius * y * cosf(a0),
				radius * sinf(a0));
		}
		glEnd();
	}
}


static void
reset()
{
	glViewport(0, 0, kSize, kSize);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_FOG);
	glDisable(GL_TEXTURE_2D);
	glShadeModel(GL_SMOOTH);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}


static void
lit_setup()
{
	glMatrixMode(GL_PROJECTION);
	glFrustum(-1, 1, -1, 1, 2, 10);
	glMatrixMode(GL_MODELVIEW);
	glTranslatef(0, 0, -4);
	glRotatef(30, 1, 0.5, 0);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);
	glEnable(GL_NORMALIZE);
	glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
	GLfloat white[] = { 1, 1, 1, 1 };
	GLfloat blue[] = { 0.3, 0.3, 1, 1 };
	GLfloat position0[] = { -3, 3, 3, 1 };
	GLfloat position1[] = { 3, -2, 2, 1 };
	glLightfv(GL_LIGHT0, GL_POSITION, position0);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
	glLightfv(GL_LIGHT0, GL_SPECULAR, white);
	glLightfv(GL_LIGHT1, GL_POSITION, position1);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, blue);
	glLightfv(GL_LIGHT1, GL_SPECULAR, blue);
	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHT1);
	GLfloat red[] = { 0.7, 0.05, 0.1, 1 };
	GLfloat shine[] = { 0.8, 0.8, 0.8, 1 };
	glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
	glMaterialfv(GL_FRONT, GL_SPECULAR, shine);
	glMaterialf(GL_FRONT, GL_SHININESS, 40);
}


static void
lit()
{
	reset();
	lit_setup();
	sphere(1.4, 24, 16);
	save("lit");
}


/*!	Nine lit spheres, turning a little each frame. */
static void
bench_frame(int frame)
{
	glViewport(0, 0, sBenchWidth, sBenchHeight);
	glClearColor(0, 0, 0.1f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	lit_setup();
	for (int i = 0; i < 9; i++) {
		glPushMatrix();
		glTranslatef((i % 3 - 1) * 1.2f, (i / 3 - 1) * 1.2f, 0);
		glRotatef(frame * 2 + i * 40, 0, 1, 0.3f);
		sphere(0.55, 24, 16);
		glPopMatrix();
	}
}


/*!	One cube as GLife draws its cells. */
static void
cell(bool border)
{
	static const GLfloat kGreen[] = { 0.05, 0.8, 0.15, 1.0 };
	static const GLfloat kOrange[] = { 0.65, 0.3, 0.05, 1.0 };
	// each strip: three sides, a normal and four corners each
	static const GLfloat kNormals[2][3][3] = {
		{ { 0, 1, 0 }, { 0, 0, 1 }, { 0, -1, 0 } },
		{ { -1, 0, 0 }, { 0, 0, -1 }, { 1, 0, 0 } }
	};
	static const GLfloat kCorners[2][3][4][3] = {
		{
			{ { 1, 1, -1 }, { -1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 } },
			{ { -1, 1, 1 }, { 1, 1, 1 }, { -1, -1, 1 }, { 1, -1, 1 } },
			{ { -1, -1, 1 }, { 1, -1, 1 }, { -1, -1, -1 }, { 1, -1, -1 } }
		},
		{
			{ { -1, 1, 1 }, { -1, -1, 1 }, { -1, 1, -1 }, { -1, -1, -1 } },
			{ { -1, 1, -1 }, { -1, -1, -1 }, { 1, 1, -1 }, { 1, -1, -1 } },
			{ { 1, 1, -1 }, { 1, -1, -1 }, { 1, 1, 1 }, { 1, -1, 1 } }
		}
	};
	for (int strip = 0; strip < 2; strip++) {
		glBegin(GL_QUAD_STRIP);
		if (border)
			glColor3f(0.65, 0.3, 0.05);
		else
			glColor3f(0.05, 0.8, 0.15);
		glMaterialfv(GL_FRONT, GL_DIFFUSE, border ? kOrange : kGreen);
		for (int side = 0; side < 3; side++) {
			glNormal3fv(kNormals[strip][side]);
			for (int corner = 0; corner < 4; corner++)
				glVertex3fv(kCorners[strip][side][corner]);
		}
		glEnd();
	}
}


/*!	Half of an sCells x sCells grid of cubes, turning. */
static void
cells_frame(int frame)
{
	glViewport(0, 0, 400, 400);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-1, 1, -1, 1, 2, 4.0f * sCells);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	lit_setup();
	glTranslatef(0, 0, -1.2f * sCells);
	glRotatef(frame * 3, 1, 0, 0);
	glRotatef(frame, 0, 0, 1);
	glRotatef(frame * 2, 0, 1, 0);
	for (int row = 0; row < sCells; row++) {
		for (int column = 0; column < sCells; column++) {
			if (((row + column + frame) & 1) != 0)
				continue;
			glPushMatrix();
			glTranslatef(column - sCells / 2.0f, row - sCells / 2.0f, 0);
			glScalef(0.45, 0.45, 0.45);
			cell(row == 0 || column == 0);
			glPopMatrix();
		}
	}
}


static void
prims()
{
	reset();
	// five copies of one quad, in a column each
	const float width = 2.0f / 5;
	for (int i = 0; i < 5; i++) {
		float left = -1 + i * width + 0.02f;
		float right = left + width - 0.04f;
		float bottom = -0.6f;
		float top = 0.6f;
		glColor3f(0.2f + 0.2f * i, 1 - 0.2f * i, 0.5f);
		switch (i) {
			case 0:
				glBegin(GL_TRIANGLES);
				glVertex2f(left, bottom);
				glVertex2f(right, bottom);
				glVertex2f(right, top);
				glVertex2f(left, bottom);
				glVertex2f(right, top);
				glVertex2f(left, top);
				break;
			case 1:
				glBegin(GL_QUADS);
				glVertex2f(left, bottom);
				glVertex2f(right, bottom);
				glVertex2f(right, top);
				glVertex2f(left, top);
				break;
			case 2:
				glBegin(GL_QUAD_STRIP);
				glVertex2f(left, bottom);
				glVertex2f(right, bottom);
				glVertex2f(left, top);
				glVertex2f(right, top);
				break;
			case 3:
				glBegin(GL_POLYGON);
				glVertex2f(left, bottom);
				glVertex2f(right, bottom);
				glVertex2f(right, top);
				glVertex2f(left, top);
				break;
			case 4:
				glBegin(GL_TRIANGLE_FAN);
				glVertex2f(left, bottom);
				glVertex2f(right, bottom);
				glVertex2f(right, top);
				glVertex2f(left, top);
				break;
		}
		glEnd();
	}
	save("prims");
}


static void
texture()
{
	reset();
	static uint8 texels[64 * 64 * 3];
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 64; x++) {
			bool white = ((x / 8) + (y / 8)) % 2 == 0;
			uint8* texel = texels + (y * 64 + x) * 3;
			texel[0] = white ? 255 : 40;
			texel[1] = white ? 255 : (uint8)(x * 4);
			texel[2] = white ? 255 : (uint8)(y * 4);
		}
	}
	GLuint name;
	glGenTextures(1, &name);
	glBindTexture(GL_TEXTURE_2D, name);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 64, 64, 0, GL_RGB,
		GL_UNSIGNED_BYTE, texels);
	glEnable(GL_TEXTURE_2D);
	glColor3f(1, 1, 1);
	for (int half = 0; half < 2; half++) {
		GLint filter = half == 0 ? GL_NEAREST : GL_LINEAR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
		float left = half == 0 ? -0.95f : 0.05f;
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0);
		glVertex2f(left, -0.45f);
		glTexCoord2f(1, 0);
		glVertex2f(left + 0.9f, -0.45f);
		glTexCoord2f(1, 1);
		glVertex2f(left + 0.9f, 0.45f);
		glTexCoord2f(0, 1);
		glVertex2f(left, 0.45f);
		glEnd();
	}
	glDeleteTextures(1, &name);
	save("texture");
}


static void
lines()
{
	reset();
	for (int i = 0; i < 4; i++) {
		glLineWidth(1 + i * 2);
		glColor3f(1, 1 - 0.25f * i, 0.25f * i);
		glBegin(GL_LINES);
		glVertex2f(-0.9f, -0.8f + i * 0.2f);
		glVertex2f(0.9f, -0.6f + i * 0.2f);
		glEnd();
	}
	glLineWidth(1);
	glColor3f(0.3f, 0.8f, 1);
	glBegin(GL_LINE_LOOP);
	glVertex2f(-0.8f, 0.1f);
	glVertex2f(0.8f, 0.1f);
	glVertex2f(0, 0.9f);
	glEnd();
	for (int i = 0; i < 4; i++) {
		glPointSize(1 + i * 3);
		glColor3f(1, 1, 1);
		glBegin(GL_POINTS);
		glVertex2f(-0.6f + i * 0.4f, 0.4f);
		glEnd();
	}
	glPointSize(1);
	save("lines");
}


static void
fog()
{
	reset();
	glMatrixMode(GL_PROJECTION);
	glOrtho(-1, 1, -1, 1, 1, 10);
	glMatrixMode(GL_MODELVIEW);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_FOG);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogf(GL_FOG_START, 2);
	glFogf(GL_FOG_END, 8);
	GLfloat fogColor[] = { 0.5, 0.5, 0.6, 1 };
	glFogfv(GL_FOG_COLOR, fogColor);
	for (int i = 0; i < 4; i++) {
		float depth = -2 - i * 1.8f;
		float offset = -0.7f + i * 0.35f;
		glColor3f(i == 0 ? 1 : 0.2f, i == 1 ? 1 : 0.2f, i >= 2 ? 1 : 0.2f);
		glBegin(GL_QUADS);
		glVertex3f(offset, offset, depth);
		glVertex3f(offset + 0.8f, offset, depth);
		glVertex3f(offset + 0.8f, offset + 0.8f, depth);
		glVertex3f(offset, offset + 0.8f, depth);
		glEnd();
	}
	save("fog");
}


class TestView : public BGLView {
public:
	TestView(BRect frame)
		:
		BGLView(frame, "gltest", B_FOLLOW_NONE, 0,
			BGL_RGB | BGL_DEPTH | BGL_DOUBLE)
	{
	}

	void AttachedToWindow()
	{
		BGLView::AttachedToWindow();
		if (sThread) {
			// as most applications draw: the window stays free meanwhile
			resume_thread(spawn_thread(&_RunEntry, "gltest bench",
				B_DISPLAY_PRIORITY, this));
			return;
		}
		Run();
	}

	static status_t _RunEntry(void* data)
	{
		((TestView*)data)->Run();
		return B_OK;
	}

	void Run()
	{
		LockGL();
		printf("renderer: %s, %s, OpenGL %s\n", glGetString(GL_VENDOR),
			glGetString(GL_RENDERER), glGetString(GL_VERSION));
		if (sBenchSeconds > 0) {
			team_usage_info before;
			get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &before);
			bigtime_t start = system_time();
			bigtime_t end = start + sBenchSeconds * 1000000LL;
			int frames = 0;
			while (system_time() < end) {
				if (sCells > 0)
					cells_frame(frames++);
				else
					bench_frame(frames++);
				SwapBuffers();
			}
			glFinish();
			bigtime_t elapsed = system_time() - start;
			team_usage_info after;
			get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &after);
			bigtime_t cpu = after.user_time + after.kernel_time
				- before.user_time - before.kernel_time;
			if (sCells > 0) {
				printf("cells %d: %d frames in %.1f s: %.1f frames/s, %.1f ms"
					" a frame, %.1f ms CPU (%.1f user)\n", sCells, frames,
					elapsed / 1e6, frames * 1e6 / elapsed,
					elapsed / 1e3 / frames, cpu / 1e3 / frames,
					(after.user_time - before.user_time) / 1e3 / frames);
			} else {
				printf("%dx%d: %d frames in %.1f s: %.1f frames/s\n",
					sBenchWidth, sBenchHeight, frames, elapsed / 1e6,
					frames * 1e6 / elapsed);
			}
			UnlockGL();
			be_app->PostMessage(B_QUIT_REQUESTED);
			return;
		}
		lit();
		prims();
		texture();
		lines();
		fog();
		SwapBuffers();
		UnlockGL();
		be_app->PostMessage(B_QUIT_REQUESTED);
	}
};


int
main(int argc, char** argv)
{
	int width = kSize;
	int height = kSize;
	if ((argc == 5 || argc == 6) && strcmp(argv[1], "bench") == 0) {
		width = sBenchWidth = atoi(argv[2]);
		height = sBenchHeight = atoi(argv[3]);
		sBenchSeconds = atoi(argv[4]);
		sThread = argc == 6 && strcmp(argv[5], "thread") == 0;
	} else if ((argc == 4 || argc == 5) && strcmp(argv[1], "cells") == 0) {
		width = height = sBenchWidth = sBenchHeight = 400;
		sCells = atoi(argv[2]);
		sBenchSeconds = atoi(argv[3]);
		sThread = argc == 5 && strcmp(argv[4], "thread") == 0;
	} else if (argc == 2)
		sDirectory = argv[1];
	else {
		fprintf(stderr, "usage: gltest <directory>\n"
			"       gltest bench <width> <height> <seconds> [thread]\n"
			"       gltest cells <n> <seconds> [thread]\n");
		return 1;
	}
	BApplication app("application/x-vnd.c2-gltest");
	BWindow* window = new BWindow(BRect(5, 25, 5 + width - 1,
		25 + height - 1), "gltest", B_TITLED_WINDOW, B_NOT_RESIZABLE);
	window->AddChild(new TestView(window->Bounds()));
	window->Show();
	app.Run();
	return 0;
}
