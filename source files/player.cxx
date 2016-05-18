#ifdef WIN32
#include <FL/gl.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>				// Add string functionality
#include <fstream>
#include <cassert>
#include <cmath>
#include <algorithm>			// For sort() and find()

#include <process.h>

#include <GL/gl.h>				// Header so that you can use GL routines (MESA)
#include <GL/glu.h>				// some OpenGL extensions
#include <FL/glut.H>			// GLUT for use with FLTK
#include <FL/fl_file_chooser.H> // Allow a file chooser for save.
#include <iostream>
using namespace std;

#include <vector>				// For storing Keyframes

#include "player.h"   		      
#include "interface.h"			// UI framework built by FLTK (using fluid)

#ifdef WRITE_JPEGS
#include "pic.h"				// for saving jpeg pictures.  
#endif

#include "transform.h"			// utility functions for vector and matrix transformation  
#include "display.h"   
#include "interpolator.h"
#include "video_texture.h"

/***************  Types *********************/
enum { OFF, ON };

/***************  Static Variables *********/
static Display displayer;

static Skeleton *pActor = NULL;			// Actor info as read from ASF file
static bool bActorExist = false;		// Set to true if actor exists

static Motion *pSampledMotion = NULL;	// Motion information as read from AMC file
static Motion *pInterpMotion = NULL;	// Interpolated Motion 


static int nFrameNum, nFrameInc = 1;		// Current frame and frame increment

static Fl_Window *form = NULL;  			// Global form 
static MouseT mouse;					// Keeping track of mouse input 
static CameraT camera;					// Structure about camera setting 

static int Play = OFF, Rewind = OFF;		// Some Flags for player
static int Repeat = OFF;

#ifdef WRITE_JPEGS
static int Record = OFF;
static char *Record_filename;			// Recording file name 
#endif

static int PlayInterpMotion = ON;			// Flag which desides which motion to play (pSampledMotion or pInterpMotion)	

static int Background = ON, Light = OFF;	// Flags indicating if the object exists    

static int recmode = 0;
static int piccount = 0;
static char * argv2;
static int maxFrames = 0;

static std::vector<int> keyframes;			// Stores the frame numbers of keyframes
static int firstFrame = 0;					// Number of the first frame of animation

/***************  Functions *******************/
static void draw_triad()
{
	glBegin(GL_LINES);

	/* draw x axis in red, y axis in green, z axis in blue */
	glColor3f(1., .2, .2);
	glVertex3f(0., 0., 0.);
	glVertex3f(1., 0., 0.);

	glColor3f(.2, 1., .2);
	glVertex3f(0., 0., 0.);
	glVertex3f(0., 1., 0.);

	glColor3f(.2, .2, 1.);
	glVertex3f(0., 0., 0.);
	glVertex3f(0., 0., 1.);

	glEnd();
}

//Draw checker board ground plane
static void draw_ground()
{
	float i, j;
	int count = 0;

	GLfloat white4[] = { .4, .4, .4, 1. };
	GLfloat white1[] = { .1, .1, .1, 1. };
	GLfloat green5[] = { 0., .5, 0., 1. };
	GLfloat green2[] = { 0., .2, 0., 1. };
	GLfloat black[] = { 0., 0., 0., 1. };
	GLfloat mat_shininess[] = { 7. };		/* Phong exponent */

	glBegin(GL_QUADS);

	for (i = -15.; i <= 15.; i += 1)
	{
		for (j = -15.; j <= 15.; j += 1)
		{
			if ((count % 2) == 0)
			{
				glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
				glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white4);
				//			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white1);
				//			glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
				glColor3f(.6, .6, .6);
			}
			else
			{
				glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
				glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, green5);
				//			glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, green2);
				//			glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);
				glColor3f(.8, .8, .8);
			}

			glNormal3f(0., 0., 1.);

			glVertex3f(j, 0, i);
			glVertex3f(j, 0, i + 1);
			glVertex3f(j + 1, 0, i + 1);
			glVertex3f(j + 1, 0, i);
			count++;
		}
	}

	glEnd();
}

void cameraView(void)
{
	glTranslated(camera.tx, camera.ty, camera.tz);
	glTranslated(camera.atx, camera.aty, camera.atz);

	glRotated(-camera.tw, 0.0, 1.0, 0.0);
	glRotated(-camera.el, 1.0, 0.0, 0.0);
	glRotated(camera.az, 0.0, 1.0, 0.0);

	glTranslated(-camera.atx, -camera.aty, -camera.atz);
	glScaled(camera.zoom, camera.zoom, camera.zoom);
}


/*
* redisplay() is called by Player_Gl_Window::draw().
*
* The display is double buffered, and FLTK swap buffers when
* Player_Gl_Window::draw() returns.  The GL context associated with this
* instance of Player_Gl_Window is set to be the current context by FLTK
* when it calls draw().
*/
static void redisplay()
{
	if (Light) glEnable(GL_LIGHTING);
	else glDisable(GL_LIGHTING);

	/* clear image buffer to black */
	glClearColor(0, 0, 0, 0);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT); /* clear image, zbuf */

	glPushMatrix();			/* save current transform matrix */

	cameraView();

	glLineWidth(2.);		/* we'll draw background with thick lines */

	if (Background)
	{
		draw_triad();		/* draw a triad in the origin of the world coord */
		draw_ground();
	}

	if (bActorExist) displayer.show();

	glPopMatrix();			/* restore current transform matrix */

}

/* Callbacks from form. */
void redisplay_proc(Fl_Light_Button *obj, long val)
{
	Light = light_button->value();
	Background = background_button->value();
	glwindow->redraw();
}

// Perfoms calculations for Catmull-Rom interpolation
::vector Catmull_RomCalc(::vector input1, ::vector input2, ::vector input3, ::vector input4, float u){

	float cube = pow(u, 3); 
	float square = pow(u, 2);

	::vector temp1 = input1*(-0.5 * cube + square - 0.5 * u);  
	::vector temp2 = input2*(1.5 * cube - 2.5 * square + 1);
	::vector temp3 = input3*(-1.5 * cube + 2 * square + 0.5 * u);
	::vector temp4 = input4*(0.5 * cube - 0.5 * square);

	::vector answer = temp1 + temp2 + temp3 + temp4;

	return answer;
}


//Interpolate motion
void interpolate_callback(Fl_Button *button, void *)
{

	int size = keyframes.size(); 
	
	if (pInterpMotion == NULL && !(keyframes.size() < 2))
	{
		(*pSampledMotion).offset = 0;
		(*dt_input).value(0);

		sort(keyframes.begin(), keyframes.end());

		if (!(keyframes[1] == keyframes[0] + 1)){
			Skeleton *s = (*pActor).clone();
			(*s).R = 0, (*s).G = 0; (*s).B = 1;
			displayer.loadActor(s);
			keyframes.insert(keyframes.begin() + 1, keyframes.front() + 1);
			(*displayer.m_pActor[size]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes.front() + 1)]);
		}
		if (keyframes[size-2] != (keyframes[size-1] - 1)*1){
			Skeleton *s = (*pActor).clone();
			(*s).R = 0, (*s).G = 0; (*s).B = 1;
			displayer.loadActor(s);
			keyframes.insert(keyframes.end() - 1, keyframes.back() - 1);
			(*displayer.m_pActor[size]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes.back() - 1)]);
		}

		maxFrames = (keyframes.back() - keyframes.front() + 1)*1;
		pInterpMotion = new Motion((*pSampledMotion).m_NumFrames);
		firstFrame = keyframes.front();
		(*frame_slider).maximum((double)maxFrames);

		int p = 0; 
		while(p < size){
			(*pInterpMotion).SetPosture(keyframes[p], *((*pSampledMotion).GetPosture(keyframes[p])));
			p++;
		}

		int i = 1; 
		while(i < keyframes.size()-2){
	
			float j = keyframes[i] + 1;
			while(j < keyframes[i + 1]){

				int k = 0;
				while(k < pActor->NUM_BONES_IN_ASF_FILE){

					::vector input1 = (*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes[i - 1])].bone_translation[k].p;
					::vector input2 = (*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes[i])].bone_translation[k].p;
					::vector input3 = (*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes[i + 1])].bone_translation[k].p;
					::vector input4 = (*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes[i + 2])].bone_translation[k].p;
					
					double temp1 = (j - keyframes[i]);
					double temp2 = (keyframes[i + 1] - keyframes[i]);
					double temp3 = temp1 / temp2; 

					::vector value = Catmull_RomCalc(input1, input2, input3, input4, (temp3));

					(*pInterpMotion).m_pPostures[(*pInterpMotion).GetPostureNum(j)].bone_translation[k].setValue(value.p);

					input1 = (*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes[i - 1])].bone_rotation[k].p;
					input2 = (*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes[i])].bone_rotation[k].p;
					input3 = (*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes[i + 1])].bone_rotation[k].p;
					input4 = (*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(keyframes[i + 2])].bone_rotation[k].p;
					
					value = Catmull_RomCalc(input1, input2, input3, input4, (temp3));

					(*pInterpMotion).m_pPostures[(*pInterpMotion).GetPostureNum(j)].bone_rotation[k].setValue(value.p);

					k++;
				}
				j++;
			}
			i++;
		}

		Skeleton *s = (*pActor).clone();
		(*s).R = 1;
		(*s).G = 0.6;
		(*s).B = 0;
		displayer.loadActor(s);

		nFrameNum = firstFrame;
		(*displayer.m_pActor[keyframes.size() + 1]).setPosture((*pInterpMotion).m_pPostures[(*pInterpMotion).GetPostureNum(nFrameNum)]);
		(*displayer.m_pActor[keyframes.size() + 1]).tx = (*displayer.m_pActor[0]).tx + 60;

		(*displayer.m_pActor[0]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(nFrameNum)]);
		(*displayer.m_pActor[0]).tx = (*displayer.m_pActor[0]).tx + 30;

		Play = OFF;
	}
}

void addKeyframe_callback(Fl_Button *button, void *)
{
	int size = keyframes.size();

	if (pActor != NULL){
		if(pSampledMotion != NULL){
			if (!(size > (MAX_SKELS - 3)*1)){
				if (find(keyframes.begin(), keyframes.end(), nFrameNum + (int)(*dt_input).value()) == keyframes.end()){
					Skeleton *s = new Skeleton("Test Data/Forrest.ASF", MOCAP_SCALE);
					(*s).R = 0;
					(*s).G = 0;
					(*s).B = 1;
					displayer.loadActor(s);
					keyframes.push_back(nFrameNum + (int)(*dt_input).value());
					(*displayer.m_pActor[keyframes.size()]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(nFrameNum)]);
				}
			}
			else cout << "No more keyframes can be added!!!\n";
		}
	}
}

void reset_callback(Fl_Button *button, void *)
{

	int size = keyframes.size(); 

	if (pActor != NULL){
		delete pActor;
		pActor = NULL;
		
		int i = 0;
		while(i < displayer.numActors){
			displayer.m_pActor[i] = NULL;
			i++;
		}
		displayer.numActors = 0;
		bActorExist = false;
	}

	if (pSampledMotion != NULL)
	{
		delete pSampledMotion;
		pSampledMotion = NULL;
	}

	if (!(size == 0))
	{
		keyframes.clear();
	}

	if (!(pInterpMotion == NULL))
	{
		delete pInterpMotion;
		pInterpMotion = NULL;
	}

	firstFrame = 0;
	Play = OFF;
	Rewind = OFF;
	Repeat = OFF;
	(*dt_input).value(0);
	(*tx_input).value(0);
	(*ty_input).value(0);
	(*tz_input).value(0);
	(*rx_input).value(0);
	(*ry_input).value(0);
	(*rz_input).value(0);
}


void load_callback(Fl_Button *button, void *)
{
	nFrameNum = 0;
	Play = OFF;
	char *filename;

	if (button == loadActor_button)
	{
		if(pActor == NULL){
			filename = fl_file_chooser("Select filename", "*.ASF", "");
			if (filename != NULL)
			{
				pActor = new Skeleton(filename, MOCAP_SCALE);
				(*pActor).R = 1;
				(*pActor).G = 1;
				(*pActor).B = 0.1;
				(*pActor).actor_Filename = filename;

				(*pActor).setBasePosture();
				displayer.loadActor(pActor);
				bActorExist = true;
				(*glwindow).redraw();
			}
		}
	}

	if (button == loadMotion_button)
	{
		if (bActorExist == true)
		{
			if(pSampledMotion == NULL){
				filename = fl_file_chooser("Select filename", "*.AMC", "");
				if (filename != NULL)
				{
					pSampledMotion = new Motion(filename, MOCAP_SCALE, pActor);

					maxFrames = (*pSampledMotion).m_NumFrames - 1;
					(*frame_slider).maximum((double)maxFrames + 1);
				
					nFrameNum = (int)(*frame_slider).value() - 1;
				}
			}
		}
	}

	if (pSampledMotion != NULL)
	{
		(*displayer.m_pActor[0]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(nFrameNum)]);
		Fl::flush();
		(*glwindow).redraw();
	}
}

#ifdef WRITE_JPEGS
void save_callback(Fl_Button *button, void *)
{
	//char *filename;
	if (button == save_button)
		glwindow->save(fl_file_chooser("Save to Jpeg File", "*.jpg", ""));

}
#endif

void play_callback(Fl_Button *button, void *)
{
	if (pSampledMotion != NULL)
	{
		if (button == play_button) { Play = ON; Rewind = OFF; }
		if (button == pause_button){ Play = OFF; Repeat = OFF; }
		if (button == repeat_button) { Rewind = OFF; Play = ON; Repeat = ON; }
		if (button == rewind_button) { Rewind = ON; Play = OFF; Repeat = OFF; }
	}
}

#ifdef WRITE_JPEGS
void record_callback(Fl_Light_Button *button, void *)
{
	int current_state = (int)button->value();

	if (Play == OFF)
	{
		if (Record == OFF && current_state == ON)
		{
			Record_filename = fl_file_chooser("Save Animation to Jpeg Files", "", "");
			if (Record_filename != NULL)
				Record = ON;
		}
		if (Record == ON && current_state == OFF)
			Record = OFF;

	}
	button->value(Record);
}
#endif

void idle(void*)
{

	if (pSampledMotion != NULL)
	{
		if (Rewind == ON)
		{
			nFrameNum = firstFrame;
			(*displayer.m_pActor[0]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(nFrameNum)]);
			if (pInterpMotion != NULL){
				(*displayer.m_pActor[keyframes.size() + 1]).setPosture((*pInterpMotion).m_pPostures[(*pInterpMotion).GetPostureNum(nFrameNum)]);
			}
			Rewind = OFF;
		}

		if (Play == ON)
		{
			int lastFrame = (firstFrame*1) + (maxFrames*1) - (2*1);
			if (!(nFrameNum <= lastFrame) && Repeat == ON)
				nFrameNum = firstFrame;

			if (!(nFrameNum > lastFrame))
				nFrameNum = nFrameNum + nFrameInc;
			else Play = OFF;

			(*displayer.m_pActor[0]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(nFrameNum)]);
			if (pInterpMotion != NULL){
				(*displayer.m_pActor[keyframes.size() + 1]).setPosture((*pInterpMotion).m_pPostures[(*pInterpMotion).GetPostureNum(nFrameNum)]);
			}

#ifdef WRITE_JPEGS
			if (Record == ON)
				glwindow->save(Record_filename);
#endif

		}
	}

	(*frame_slider).value((double)nFrameNum - firstFrame + 1);
	(*glwindow).redraw();
}

void fslider_callback(Fl_Value_Slider *slider, long val)
{
	if (pSampledMotion != NULL)
	{
		if ((*pSampledMotion).m_NumFrames > 0)
		{
			nFrameNum = (int)(*frame_slider).value() + firstFrame - 1;

			(*displayer.m_pActor[0]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(nFrameNum)]);
			if (pInterpMotion != NULL){
				(*displayer.m_pActor[keyframes.size() + 1]).setPosture((*pInterpMotion).m_pPostures[(*pInterpMotion).GetPostureNum(nFrameNum)]);
			}
			Fl::flush();
			Play = OFF;
			(*glwindow).redraw();
		}
	}
}

// locate rotation center at the (root.x, 0, root.z)
void locate_callback(Fl_Button *obj, void *)
{
	if (bActorExist && pSampledMotion != NULL)
	{
		camera.zoom = 1;
		camera.atx = pActor->m_RootPos[0];
		camera.aty = 0;
		camera.atz = pActor->m_RootPos[2];
	}
	glwindow->redraw();
}

void valueIn_callback(Fl_Value_Input *obj, void *)
{
	displayer.m_SpotJoint = (int)joint_idx->value();
	nFrameInc = (int)fsteps->value();
	glwindow->redraw();
}

void sub_callback(Fl_Value_Input *obj, void*)
{
	int subnum;
	subnum = (int)sub_input->value();
	if (subnum < 0) sub_input->value(0);
	else if (subnum > displayer.numActors - 1) sub_input->value(displayer.numActors - 1);
	else
	{
		// Change values of other inputs to match subj num
		dt_input->value(pSampledMotion->offset);
		tx_input->value(displayer.m_pActor[subnum]->tx);
		ty_input->value(displayer.m_pActor[subnum]->ty);
		tz_input->value(displayer.m_pActor[subnum]->tz);
		rx_input->value(displayer.m_pActor[subnum]->rx);
		ry_input->value(displayer.m_pActor[subnum]->ry);
		rz_input->value(displayer.m_pActor[subnum]->rz);
	}
	glwindow->redraw();
}

void dt_callback(Fl_Value_Input *obj, void*)
{
	if (pInterpMotion == NULL)
	{
		int subnum, max = 0;
		subnum = (int)(*sub_input).value();
		if (!(subnum >= displayer.numActors) && subnum >= 0)
		{
			(*pSampledMotion).SetTimeOffset((int)(*dt_input).value());
			printf("Shifting subject %d by %d\n", subnum, (int)(*dt_input).value());
			
			int i = 0;
			while(i < displayer.numActors)
			{
				if (((*pSampledMotion).m_NumFrames - 1 - (*pSampledMotion).offset) > max)
					max = ((*pSampledMotion).m_NumFrames - 1 - (*pSampledMotion).offset);
				i++;
			}
			maxFrames = max;
			(*frame_slider).maximum((double)maxFrames + 1);
			(*displayer.m_pActor[subnum]).setPosture((*pSampledMotion).m_pPostures[(*pSampledMotion).GetPostureNum(nFrameNum)]);
		}
		(*glwindow).redraw();
	}
	else (*dt_input).value(0);
}

void tx_callback(Fl_Value_Input *obj, void*)
{
	int subnum = 0;
	subnum = (int)sub_input->value();
	if (subnum < displayer.numActors && subnum >= 0)
	{
		displayer.m_pActor[subnum]->tx = (int)tx_input->value();
	}
	glwindow->redraw();
}

void ty_callback(Fl_Value_Input *obj, void*)
{
	int subnum = 0;
	subnum = (int)sub_input->value();
	if (subnum < displayer.numActors && subnum >= 0)
	{
		displayer.m_pActor[subnum]->ty = (int)ty_input->value();
	}
	glwindow->redraw();
}

void tz_callback(Fl_Value_Input *obj, void*)
{
	int subnum = 0;
	subnum = (int)sub_input->value();
	if (subnum < displayer.numActors && subnum >= 0)
	{
		displayer.m_pActor[subnum]->tz = (int)tz_input->value();
	}
	glwindow->redraw();
}

void rx_callback(Fl_Value_Input *obj, void*)
{
	int subnum = 0;
	subnum = (int)sub_input->value();
	if (subnum < displayer.numActors && subnum >= 0)
	{
		displayer.m_pActor[subnum]->rx = (int)rx_input->value();
	}
	glwindow->redraw();
}

void ry_callback(Fl_Value_Input *obj, void*)
{
	int subnum = 0;
	subnum = (int)sub_input->value();
	if (subnum < displayer.numActors && subnum >= 0)
	{
		displayer.m_pActor[subnum]->ry = (int)ry_input->value();
	}
	glwindow->redraw();
}

void rz_callback(Fl_Value_Input *obj, void*)
{
	int subnum = 0;
	subnum = (int)sub_input->value();
	if (subnum < displayer.numActors && subnum >= 0)
	{
		displayer.m_pActor[subnum]->rz = (int)rz_input->value();
	}
	glwindow->redraw();
}

void exit_callback(Fl_Button *obj, long val)
{
	//DEBUG: uncomment
	exit(1);
}

void light_init()
{
	/* set up OpenGL to do lighting
	* we've set up three lights */

	/* set material properties */
	GLfloat white8[] = { .8, .8, .8, 1. };
	GLfloat white2[] = { .2, .2, .2, 1. };
	GLfloat black[] = { 0., 0., 0., 1. };
	GLfloat mat_shininess[] = { 50. };		/* Phong exponent */

	GLfloat light0_position[] = { -25., 25., 25., 0. }; /* directional light (w=0) */
	GLfloat white[] = { 11., 11., 11., 5. };

	GLfloat light1_position[] = { -25., 25., -25., 0. };
	GLfloat red[] = { 1., .3, .3, 5. };

	GLfloat light2_position[] = { 25., 25., -5., 0. };
	GLfloat blue[] = { .3, .4, 1., 25. };

	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, white2);	/* no ambient */
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, white8);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white2);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, mat_shininess);

	/* set up several lights */
	/* one white light for the front, red and blue lights for the left & top */

	glLightfv(GL_LIGHT0, GL_POSITION, light0_position);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
	glLightfv(GL_LIGHT0, GL_SPECULAR, white);
	glEnable(GL_LIGHT0);

	glLightfv(GL_LIGHT1, GL_POSITION, light1_position);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, red);
	glLightfv(GL_LIGHT1, GL_SPECULAR, red);
	glEnable(GL_LIGHT1);

	glLightfv(GL_LIGHT2, GL_POSITION, light2_position);
	glLightfv(GL_LIGHT2, GL_DIFFUSE, blue);
	glLightfv(GL_LIGHT2, GL_SPECULAR, blue);
	glEnable(GL_LIGHT2);

	//mstevens
	GLfloat light3_position[] = { 0., -25., 0., 0.6 };
	glLightfv(GL_LIGHT3, GL_POSITION, light3_position);
	glLightfv(GL_LIGHT3, GL_DIFFUSE, white);
	glLightfv(GL_LIGHT3, GL_SPECULAR, white);
	glEnable(GL_LIGHT3);

	glEnable(GL_NORMALIZE);	/* normalize normal vectors */
	glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);	/* two-sided lighting*/

	/* do the following when you want to turn on lighting */
	if (Light) glEnable(GL_LIGHTING);
	else glDisable(GL_LIGHTING);
}


static void error_check(int loc)
{
	/* this routine checks to see if OpenGL errors have occurred recently */
	GLenum e;

	while ((e = glGetError()) != GL_NO_ERROR)
		fprintf(stderr, "Error: %s before location %d\n",
		gluErrorString(e), loc);
}


void gl_init()
{
	int red_bits, green_bits, blue_bits;
	struct { GLint x, y, width, height; } viewport;
	glEnable(GL_DEPTH_TEST);	/* turn on z-buffer */

	glGetIntegerv(GL_RED_BITS, &red_bits);
	glGetIntegerv(GL_GREEN_BITS, &green_bits);
	glGetIntegerv(GL_BLUE_BITS, &blue_bits);
	glGetIntegerv(GL_VIEWPORT, &viewport.x);
	printf("OpenGL window has %d bits red, %d green, %d blue; viewport is %dx%d\n",
		red_bits, green_bits, blue_bits, viewport.width, viewport.height);

	/* setup perspective camera with OpenGL */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(/*vertical field of view*/ 45.,
		/*aspect ratio*/ (double)viewport.width / viewport.height,
		/*znear*/ .1, /*zfar*/ 50.);

	/* from here on we're setting modeling transformations */
	glMatrixMode(GL_MODELVIEW);

	//Move away from center
	glTranslatef(0., 0., -5.);

	camera.zoom = .5;

	camera.tw = 0;
	camera.el = -15;
	camera.az = -25;

	camera.atx = 0;
	camera.aty = 0;
	camera.atz = 0;
}


/*******************
* Define the methods for glwindow, a subset of Fl_Gl_Window.
*******************/

/*
* Handle keyboard and mouse events.  Don't make any OpenGL calls here;
* the GL Context is not set!  Make the calls in redisplay() and call
* the redraw() method to cause FLTK to set up the context and call draw().
* See the FLTK documentation under "Using OpenGL in FLTK" for additional
* tricks and tips.
*/
int Player_Gl_Window::handle(int event)
{
	int handled = 1;
	static int prev_x, prev_y;
	int delta_x = 0, delta_y = 0;
	float ev_x, ev_y;

	switch (event) {
	case FL_RELEASE:
		mouse.x = (Fl::event_x());
		mouse.y = (Fl::event_y());
		mouse.button = 0;
		break;
	case FL_PUSH:
		mouse.x = (Fl::event_x());
		mouse.y = (Fl::event_y());
		mouse.button = (Fl::event_button());
		break;
	case FL_DRAG:
		mouse.x = (Fl::event_x());
		mouse.y = (Fl::event_y());
		delta_x = mouse.x - prev_x;
		delta_y = mouse.y - prev_y;

		if (mouse.button == 1)
		{
			if (abs(delta_x) > abs(delta_y))
				camera.az += (GLdouble)(delta_x);
			else
				camera.el -= (GLdouble)(delta_y);
		}
		else if (mouse.button == 2)
		{
			if (abs(delta_y) > abs(delta_x))
			{
				glScalef(1 + delta_y / 100., 1 + delta_y / 100., 1 + delta_y / 100.);
				//     camera.zoom -= (GLdouble) delta_y/100.0;
				//     if(camera.zoom < 0.) camera.zoom = 0;
			}
		}
		else if (mouse.button == 3){
			//camera.tx += (GLdouble) delta_x/10.0;
			//camera.tz -= (GLdouble) delta_y/10.0; //FLTK's origin is at the left_top corner

			camera.tx += (GLdouble)cos(camera.az / 180.0*3.141)*delta_x / 10.0;
			camera.tz += (GLdouble)sin(camera.az / 180.0*3.141)*delta_x / 10.0;
			camera.ty -= (GLdouble)delta_y / 10.0; //FLTK's origin is at the left_top corner

			camera.atx = -camera.tx;
			camera.aty = -camera.ty;
			camera.atz = -camera.tz;
		}
		break;
	case FL_KEYBOARD:
		switch (Fl::event_key()) {
		case 'q':
		case 'Q':
		case 65307:
			exit(0);
		}
		break;
	default:
		// pass other events to the base class...
		handled = Fl_Gl_Window::handle(event);
	}

	prev_x = mouse.x;
	prev_y = mouse.y;
	glwindow->redraw();

	return (handled);  // Returning one acknowledges that we handled this event
}


/*
Prewritten Save Function
*/
#ifdef WRITE_JPEGS
void Player_Gl_Window::save(char *filename)
{
	int i;
	int j;
	static char anim_filename[512];
	static Pic *in = NULL;

	sprintf(anim_filename, "%05d.jpg", piccount++);
	if (filename == NULL) return;

	//Allocate a picture buffer.
	if (in == NULL) in = pic_alloc(640, 480, 3, NULL);

	printf("File to save to: %s\n", anim_filename);

	for (i = 479; i >= 0; i--)
	{
		glReadPixels(0, 479 - i, 640, 1, GL_RGB, GL_UNSIGNED_BYTE,
			&in->pix[i*in->nx*in->bpp]);
	}

	if (jpeg_write(anim_filename, in))
		printf("%s saved Successfully\n", anim_filename);
	else
		printf("Error in Saving\n");
}
#endif

/*
Prewritten Draw Function.
*/
void Player_Gl_Window::draw()
{
	//Upon setup of the window (or when Fl_Gl_Window->invalidate is called), 
	//the set of functions inside the if block are executed.
	if (!valid())
	{
		/*		if (fopen("Skeleton.ASF", "r") == NULL)
		{
		printf("Program can't run without 'Skeleton.ASF'!.\n"
		"Please make sure you place a 'Skeleton.ASF' file to working directory.\n");
		exit(1);
		}*/
		gl_init();
		light_init();
	}

	//Redisplay the screen then put the proper buffer on the screen.
	redisplay();
}


int main(int argc, char **argv)
{

	/* initialize form, sliders and buttons*/
	form = make_window();

	light_button->value(Light);
	background_button->value(Background);
#ifdef WRITE_JPEGS
	record_button->value(Record);
#endif

	frame_slider->value(1);

	/*show form, and do initial draw of model */
	form->show();
	glwindow->show(); /* glwindow is initialized when the form is built */
	if (argc > 2)
	{
		char *filename;

		if (1 == 1)
		{
			filename = argv[1];
			if (filename != NULL)
			{
				//Remove old actor
				if (pActor != NULL)
					delete pActor;
				//Read skeleton from asf file
				pActor = new Skeleton(filename, MOCAP_SCALE);

				//Set the rotations for all bones in their local coordinate system to 0
				//Set root position to (0, 0, 0)
				pActor->setBasePosture();
				displayer.loadActor(pActor);
				bActorExist = true;
			}
		}

		if (1 == 1)
		{
			if (bActorExist == true)
			{
				argv2 = filename = argv[2];
				if (filename != NULL)
				{
					//delete old motion if any
					if (pSampledMotion != NULL)
					{
						delete pSampledMotion;
						pSampledMotion = NULL;
					}
					if (pInterpMotion != NULL)
					{
						delete pInterpMotion;
						pInterpMotion = NULL;
					}


					//Read motion (.amc) file and create a motion
					pSampledMotion = new Motion(filename, MOCAP_SCALE, pActor);

					//set sampled motion for display
					displayer.loadMotion(pSampledMotion);

					//Tell actor to perform the first pose ( first posture )
					pActor->setPosture(pSampledMotion->m_pPostures[0]);

					(*frame_slider).maximum((double)(*pSampledMotion).m_NumFrames);

					nFrameNum = 0;
				}
			}
			else
				printf("Load Actor first.\n");
			nFrameInc = 4;            // Current frame and frame increment
			Play = ON;               // Some Flags for player
			Repeat = OFF;
#ifdef WRITE_JPEGS
			Record = ON;
			Record_filename = "";                    // Recording file name
#endif
			Background = OFF;
			Light = OFF; // Flags indicating if the object exists
			recmode = 1;
		}
		glwindow->redraw();
	}
	Fl::add_idle(idle);
	return Fl::run();
}

