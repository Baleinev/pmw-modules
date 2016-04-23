/*
 * Copyright (C) 2002  Terence M. Welsh
 * Ported to Linux by Tugrul Galatali <tugrul@galatali.com>
 *
 * Skyrocket is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as 
 * published by the Free Software Foundation.
 *
 * Skyrocket is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// Skyrocket screen saver

#define VIEWPORT_SIZE_X 1024
#define VIEWPORT_SIZE_Y (768*2)

#include <math.h>
#include <stdio.h>
#include <pthread.h>

#include <GLES/gl.h>
#include <GLES/glext.h>

// #include <GL/glu.h>

#include <list>

#include "driver.h"
#include "rsDefines.h"
#include "rsRand.h"
#include "rsMath/rsVec.h"
#include "skyrocket_flare.h"
#include "skyrocket_particle.h"
#include "skyrocket_smoke.h"
#include "skyrocket_shockwave.h"

// To get rid of the world dependency
float clouds[2];	// 9 = x,y,z,u,v,std bright,r,g,b
unsigned int cloudtex;

extern unsigned int flaretex[5];

static float heading, pitch;

GLuint renderbuffer, framebuffer;

GLuint bufferText;

 int mire = -1;

float p[4][2] = {
  {-1,-1},
  {1,-1},
  {1,1},
  {-1,1}
};

extern unsigned int o[2];

extern float billboardMat[16];

/*  list of particles */
std::list < particle > particles;

/* Lists for Draw Elements */
GLfloat         vertexlist[1024*1024];
GLfloat         colorList[1024*1024/2];
GLushort        indices[1024*1024/2];
GLfloat         texList[1024*1024/2];




  GLfloat orthoMat[16] =
  {
    1.0f/400,   0,              0, 0,
    0,          1.0f/300,       0, 0,
    0,          0,              1, 0,
    0,          0,              0, 1
  };
  

  GLubyte orthoIndices[] = {0,1,2,2,3,0};


// Time from one frame to the next
float elapsedTime = 0.0f;

// Window variables
int xsize, ysize, centerx, centery;
float aspectRatio;

// Camera variables
rsVec lookFrom[3]	// 3 = position, target position, last position
	= { rsVec (2500.0f, 0.0f, 0.0f),
	rsVec (0.0f, 0.0f, 0.0f),
	rsVec (0.0f, 0.0f, 0.0f)
};
rsVec lookAt[3]		// 3 = position, target position, last position
	= { rsVec (0.0f, 1300.0f, 0.0f),
	rsVec (0.0f, 1200.0f, 0.0f),
	rsVec (0.0f, 1200.0f, 0.0f)
};

int numRockets = 0;


// Parameters edited in the dialog box
int dMaxrockets = 5;
int dSmoke;
int dExplosionsmoke;
int dWind;
int dAmbient;
int dStardensity;
int dFlare;
int dMoonglow;
int dMoon;
int dClouds;
int dEarth;
int dIllumination;
int dSound;
int dPriority;

// Commands given from keyboard
int kAction = 1;
int kCamera = 1;		// 0 = paused, 1 = autonomous, 2 = mouse control
int kNewCamera = 0;
int kSlowMotion = 0;
int userDefinedExplosion = -1;

extern 	uint32_t screen_width;
extern 	uint32_t screen_height;

extern pthread_mutex_t         listMutex;


particle *addParticle(unsigned long seed)
{
	static particle *tempPart;

//         pthread_mutex_lock(&listMutex);
        
	particles.push_back(particle(seed));
	tempPart = &(particles.back());
	tempPart->depth = 10000;
        
//         pthread_mutex_unlock(&listMutex);        

	return tempPart;
}

void gluMakeIdentityf_(GLfloat m[16])
{
  m[0+4*0] = 1; m[0+4*1] = 0; m[0+4*2] = 0; m[0+4*3] = 0;
  m[1+4*0] = 0; m[1+4*1] = 1; m[1+4*2] = 0; m[1+4*3] = 0;
  m[2+4*0] = 0; m[2+4*1] = 0; m[2+4*2] = 1; m[2+4*3] = 0;
  m[3+4*0] = 0; m[3+4*1] = 0; m[3+4*2] = 0; m[3+4*3] = 1;
}

void gluPerspective_(GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar)
{
  GLfloat m[4][4];
  GLfloat sine, cotangent, deltaZ;
  GLfloat radians = fovy / 2 * 3.14 / 180;

  deltaZ = zFar - zNear;
  sine = sin(radians);
  if ((deltaZ == 0) || (sine == 0) || (aspect == 0))
  {
  return;
  }
  cotangent = cos(radians) / sine;

  gluMakeIdentityf_(&m[0][0]);
  m[0][0] = cotangent / aspect;
  m[1][1] = cotangent;
  m[2][2] = -(zFar + zNear) / deltaZ;
  m[2][3] = -1;
  m[3][2] = -2 * zNear * zFar / deltaZ;
  m[3][3] = 0;
  glMultMatrixf(&m[0][0]);
}

void draw()
{
  /* Draw on dedicated framebuffer */
  glBindFramebufferOES(GL_FRAMEBUFFER_OES, framebuffer);       

  glViewport (0, 0, VIEWPORT_SIZE_X, VIEWPORT_SIZE_Y);
  
  aspectRatio = (float)VIEWPORT_SIZE_X/(float)VIEWPORT_SIZE_Y;     

  int i, j;
  
  glPushMatrix();

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity();
  gluPerspective_ (60.0f, aspectRatio, 1.0f, 40000.0f);

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity();
  
  glRotatef(-pitch, 1, 0, 0);
  glRotatef(-heading, 0, 1, 0);
  glTranslatef(-lookFrom[0][0], -lookFrom[0][1], -lookFrom[0][2]);        

  /* Clear the framebuffer  */
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);


  static float rocketTimer = 0.0f;
  static float rocketTimeConst = 10.0f / float (dMaxrockets);
  static float changeRocketTimeConst = 20.0f;

  changeRocketTimeConst -= elapsedTime;
  if (changeRocketTimeConst <= 0.0f)
  {
          float temp = rsRandf (4.0f);

          rocketTimeConst = (temp * temp) + (10.0f / float (dMaxrockets));
          changeRocketTimeConst = rsRandf (30.0f) + 10.0f;
  }

  // add new rocket to list
  rocketTimer -= elapsedTime;
  
//   if ((rocketTimer <= 0.0f) || (userDefinedExplosion >= 0))
//   {
//           if (numRockets < dMaxrockets)
//           {
// //                   particle *rock = addParticle(42); // To be added to the first sublist
// 
// // 				if (rsRandi (3) || (userDefinedExplosion >= 0)) {	// Usually launch a rocket
//                     printf("%d\n",particles.size());
//                     
// //             981997,0.898039,0.156863,0.482353,0.988235,0.913725,0.309804,5.175040,13.974669
//             
//             particle *rock = addParticle(981997);
//             
//             rock->initRocket(0.898039f,0.156863f,0.482353f,0.988235f,0.913725f,0.309804f,5.175040f,13.974669f);
// //             (float)priG->valuedouble,
// //             (float)priB->valuedouble,
// //             (float)secR->valuedouble,
// //             (float)secG->valuedouble,
// //             (float)secB->valuedouble,
// //             ((float)atof(power->valuestring))/100.0f*7.0f,
// //             ((float)atof(angle->valuestring))
// //             );
// //             
// 
//   
//             rock->explosiontype = 0;
//             
//             
//                           numRockets++;
//           }
// 
//           if (dMaxrockets)
//                   rocketTimer = rsRandf (rocketTimeConst);
//           else
//                   rocketTimer = 60.0f;	// arbitrary number since no rockets ever fire
// 
//           if (userDefinedExplosion >= 0) {
//                   userDefinedExplosion = -1;
//                   rocketTimer = 20.0f;	// Wait 20 seconds after user launches a rocket before launching any more
//           }
//   }

//   update particles
  numRockets = 0;
  
        pthread_mutex_lock(&listMutex);  
  
  std::list < particle >::iterator curpart = particles.begin();
                
  while (curpart != particles.end())
  {
    curpart->update();

    if (curpart->type == ROCKET)
      numRockets++;

    if (curpart->life <= 0.0f || curpart->xyz[1] < 0.0f) 
    {
      if (curpart->type == ROCKET) 
      {	// become explosion
        if (curpart->xyz[1] <= 0.0f)
        {	// move above ground for explosion
          curpart->xyz[1] = 0.1f;
          curpart->vel[1] *= -0.7f;
        }
        if (curpart->explosiontype == 18)
          curpart->initSpinner();
        else
          curpart->initExplosion();
      } 
      else if (curpart->type == POPPER)
      {
        switch (curpart->explosiontype)
        {
          case STAR:
            curpart->explosiontype = 100;
            curpart->initExplosion ();
          break;
          case STREAMER:
            curpart->explosiontype = 101;
            curpart->initExplosion ();
          break;
          case METEOR:
            curpart->explosiontype = 102;
            curpart->initExplosion ();
          break;
          case POPPER:
            curpart->type = STAR;
            curpart->rgb.set (1.0f, 0.8f, 0.6f);
            curpart->t = curpart->tr = 0.2f;
        }
      }
      else if (curpart->type == SUCKER)
      {
        curpart->initShockwave ();
      }
      else if (curpart->type == STRETCHER)
      {	// become big explosion
        curpart->initBigmama ();
      }
      else	// remove particles from list
        curpart = particles.erase(curpart)--;
    }
    curpart++;
  }

//   printf(".");fflush(0);

  i=0;
  j=0;
  unsigned int k=0;
  unsigned int l=0;
  unsigned int m=0;
  unsigned int n=0;

  curpart = particles.begin();
  
  while (curpart != particles.end() && m < 2000)
  {
//                   printf("%f %f\n",vertexlist[i],vertexlist[i]+1,vertexlist[i]+2);                              
                  
        if (
          curpart->life > 0.0f          && 
          curpart->depth >= 0.0f        &&
          curpart->xyz[1] >= 0.0f       &&
          curpart->type != SHOCKWAVE    &&
          curpart->type != POPPER)
        {                  
                  vertexlist[i]       = curpart->xyz[0]-0.75f*curpart->size;
                  vertexlist[i+1]     = curpart->xyz[1]-0.75f*curpart->size;
                  vertexlist[i+2]     = curpart->xyz[2];
                  
                  vertexlist[i+3]     = curpart->xyz[0]-0.75f*curpart->size;
                  vertexlist[i+4]     = curpart->xyz[1]+0.75f*curpart->size;
                  vertexlist[i+5]     = curpart->xyz[2];
                                    
                  vertexlist[i+6]     = curpart->xyz[0]+0.75f*curpart->size;
                  vertexlist[i+7]     = curpart->xyz[1]+0.75f*curpart->size;
                  vertexlist[i+8]     = curpart->xyz[2];
                  
                  vertexlist[i+9]     = curpart->xyz[0]+0.75f*curpart->size;
                  vertexlist[i+10]    = curpart->xyz[1]-0.75f*curpart->size;
                  vertexlist[i+11]    = curpart->xyz[2];
                  
                  indices[j]    = n;
                  indices[j+1]  = n+1;
                  indices[j+2]  = n+2;
                  
                  indices[j+3]  = n;
                  indices[j+4]  = n+2;
                  indices[j+5]  = n+3;  
                  
                  
                  n+=4;
                  
                  
                  
                  texList[l]          = 0.0f;
                  texList[l+1]        = 0.0f;
                  
                  texList[l+2]        = 0.0f;
                  texList[l+3]        = 1.0f;
                  
                  texList[l+4]        = 1.0f;
                  texList[l+5]        = 1.0f;
                  
                  texList[l+6]        = 1.0f;
                  texList[l+7]        = 0.0f;
                  
                  
                  colorList[k]        = curpart->rgb[0];
                  colorList[k+1]      = curpart->rgb[1];
                  colorList[k+2]      = curpart->rgb[2];
                  colorList[k+3]      = curpart->bright;                  
                  k+=4;
                  colorList[k]        = curpart->rgb[0];
                  colorList[k+1]      = curpart->rgb[1];
                  colorList[k+2]      = curpart->rgb[2];
                  colorList[k+3]      = curpart->bright;
                  k+=4;
                  colorList[k]        = curpart->rgb[0];
                  colorList[k+1]      = curpart->rgb[1];
                  colorList[k+2]      = curpart->rgb[2];
                  colorList[k+3]      = curpart->bright;
                  k+=4;
                  colorList[k]        = curpart->rgb[0];
                  colorList[k+1]      = curpart->rgb[1];
                  colorList[k+2]      = curpart->rgb[2];
                  colorList[k+3]      = curpart->bright;                  
                  k+=4;                 
                  
                  
                  
                  i+=12;
                  j+=6;
                  l+=8;
                  
                  
                  
                  
                  
                  
                  
                  
                  
                  
                  vertexlist[i]       = curpart->xyz[0]-0.25f*curpart->size;
                  vertexlist[i+1]     = curpart->xyz[1]-0.25f*curpart->size;
                  vertexlist[i+2]     = curpart->xyz[2];
                  
                  vertexlist[i+3]     = curpart->xyz[0]-0.25f*curpart->size;
                  vertexlist[i+4]     = curpart->xyz[1]+0.25f*curpart->size;
                  vertexlist[i+5]     = curpart->xyz[2];
                  
                  vertexlist[i+6]     = curpart->xyz[0]+0.25f*curpart->size;
                  vertexlist[i+7]     = curpart->xyz[1]+0.25f*curpart->size;
                  vertexlist[i+8]     = curpart->xyz[2];
                  
                  vertexlist[i+9]     = curpart->xyz[0]+0.25f*curpart->size;
                  vertexlist[i+10]    = curpart->xyz[1]-0.25f*curpart->size;
                  vertexlist[i+11]    = curpart->xyz[2];
                  
                  indices[j]    = n;
                  indices[j+1]  = n+1;
                  indices[j+2]  = n+2;
                  
                  indices[j+3]  = n;
                  indices[j+4]  = n+2;
                  indices[j+5]  = n+3;  
                  
                  
                  n+=4;                  
                  
                  
                  texList[l]          = 0.0f;
                  texList[l+1]        = 0.0f;
                  
                  texList[l+2]        = 0.0f;
                  texList[l+3]        = 1.0f;
                  
                  texList[l+4]        = 1.0f;
                  texList[l+5]        = 1.0f;
                  
                  texList[l+6]        = 1.0f;
                  texList[l+7]        = 0.0f;
                  
                  
                  colorList[k]        = 1.0;
                  colorList[k+1]      = 1.0;
                  colorList[k+2]      = 1.0;
                  colorList[k+3]      = curpart->bright;                  
                  k+=4;
                  colorList[k]        = 1.0;
                  colorList[k+1]      = 1.0;
                  colorList[k+2]      = 1.0;
                  colorList[k+3]      = curpart->bright;  
                  k+=4;
                  colorList[k]        = 1.0;
                  colorList[k+1]      = 1.0;
                  colorList[k+2]      = 1.0;
                  colorList[k+3]      = curpart->bright;  
                  k+=4;
                  colorList[k]        = 1.0;
                  colorList[k+1]      = 1.0;
                  colorList[k+2]      = 1.0;
                  colorList[k+3]      = curpart->bright;                   
                  k+=4;                  
                                   
                  
                  i+=12;
                  j+=6;
                  l+=8;                  
                  
                                    
//                   colorlist[i*3]
                  
                  if(curpart->type == EXPLOSION) 
                  {

                    vertexlist[i]       = curpart->xyz[0]-curpart->bright*1.5f;
                    vertexlist[i+1]     = curpart->xyz[1]-curpart->bright*1.5f;
                    vertexlist[i+2]     = curpart->xyz[2];
                    
                    vertexlist[i+3]     = curpart->xyz[0]-curpart->bright*1.5f;
                    vertexlist[i+4]     = curpart->xyz[1]+curpart->bright*1.5f;
                    vertexlist[i+5]     = curpart->xyz[2];
                                      
                    vertexlist[i+6]     = curpart->xyz[0]+curpart->bright*1.5f;
                    vertexlist[i+7]     = curpart->xyz[1]+curpart->bright*1.5f;
                    vertexlist[i+8]     = curpart->xyz[2];
                    
                    vertexlist[i+9]     = curpart->xyz[0]+curpart->bright*1.5f;
                    vertexlist[i+10]    = curpart->xyz[1]-curpart->bright*1.5f;
                    vertexlist[i+11]    = curpart->xyz[2];
                    
                    indices[j]    = n;
                    indices[j+1]  = n+1;
                    indices[j+2]  = n+2;
                    
                    indices[j+3]  = n;
                    indices[j+4]  = n+2;
                    indices[j+5]  = n+3;  
                    
                    
                    n+=4;
                    
                    
                    texList[l]          = 0.0f;
                    texList[l+1]        = 0.0f;
                    
                    texList[l+2]        = 0.0f;
                    texList[l+3]        = 1.0f;
                    
                    texList[l+4]        = 1.0f;
                    texList[l+5]        = 1.0f;
                    
                    texList[l+6]        = 1.0f;
                    texList[l+7]        = 0.0f;
                    
                    colorList[k]        = 1.0f;
                    colorList[k+1]      = 1.0f;
                    colorList[k+2]      = 1.0f;
                    colorList[k+3]      = curpart->bright;                  
                    k+=4;
                    colorList[k]        = 1.0f;
                    colorList[k+1]      = 1.0f;
                    colorList[k+2]      = 1.0f;
                    colorList[k+3]      = curpart->bright;
                    k+=4;
                    colorList[k]        = 1.0f;
                    colorList[k+1]      = 1.0f;
                    colorList[k+2]      = 1.0f;
                    colorList[k+3]      = curpart->bright;
                    k+=4;
                    colorList[k]        = 1.0f;
                    colorList[k+1]      = 1.0f;
                    colorList[k+2]      = 1.0f;
                    colorList[k+3]      = curpart->bright;                 
                    k+=4;                 
                    
                    
                    i+=12;
                    j+=6;
                    l+=8;  

                    m++;                  
                    
                  }
                  
                  m+=2;
        }
                  curpart++;
                }
            
        pthread_mutex_unlock(&listMutex);

  GLfloat orthoVtx2[] = 
  {
    400.0f*p[0][0], 300.0f*p[0][1], 0.0f,
    400.0f*p[1][0], 300.0f*p[1][1], 0.0f,
    400.0f*p[2][0], 300.0f*p[2][1], 0.0f,
    400.0f*p[3][0], 300.0f*p[3][1], 0.0f
  };
  
  GLfloat orthoVtx[] = 
  {
    400.0f*p[0][0], 300.0f*p[0][1], 0.0f,
    400.0f*p[1][0], 300.0f*p[1][1], 0.0f,
    400.0f*p[2][0], 300.0f*p[2][1], 0.0f,
    400.0f*p[3][0], 300.0f*p[3][1], 0.0f
  };  

/*  GLfloat orthoTex[] =
  {
    1-aX, 1-aY, 0, aX aY,
    1-bX, bY, 0, bX bY,
    cX, cY, 0, cX cY,
    dX, 1-dY, 0, dX dY
  }; */ 
  
  
float ax = p[2][0] - p[0][0];
float ay = p[2][1] - p[0][1];
float bx = p[3][0] - p[1][0];
float by = p[3][1] - p[1][1];

float cross = ax * by - ay * bx;


  float cy = p[0][1] - p[1][1];
  float cx = p[0][0] - p[1][0];

  float s = (ax * cy - ay * cx) / cross;

    float t = (bx * cy - by * cx) / cross;

      float q0 = 1 / (1 - t);
      float q1 = 1 / (1 - s);
      float q2 = 1 / t;
      float q3 = 1 / s;
                        
      // you can now pass (u * q, v * q, q) to OpenGL
   
  
  GLfloat orthoTex[] =
  {
    0.0f*q0, 1.0f*q0, 0.0f*q0,1.0f*q0,
    1.0f*q1, 1.0f*q1, 0.0f*q1,1.0f*q1,    
    1.0f*q2, 0.0f*q2, 0.0f*q2,1.0f*q2,
    0.0f*q3, 0.0f*q3, 0.0f*q3,1.0f*q3
  };  
  
  
  GLfloat orthoTex2[] =
  {
    1.0f*q0, (o[1] == 0 ? 0.0f : 0.5f)*q0, 0.0f*q0,1.0f*q0,
    0.0f*q1, (o[1] == 0 ? 0.0f : 0.5f)*q1, 0.0f*q1,1.0f*q1,    
    0.0f*q2, (o[1] == 0 ? 0.5f : 1.0f)*q2, 0.0f*q2,1.0f*q2,
    1.0f*q3, (o[1] == 0 ? 0.5f : 1.0f)*q3, 0.0f*q3,1.0f*q3
  };    
  
//   printf("%f,%f\n",1.0f*q2, 1.0f*q3);
  
  glEnable(GL_BLEND);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  glMultMatrixf (billboardMat);

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);

  glBindTexture(GL_TEXTURE_2D, flaretex[0]);

  glColorPointer(4, GL_FLOAT, 0, colorList);
  glVertexPointer(3, GL_FLOAT, 0, vertexlist);
  glTexCoordPointer(2, GL_FLOAT, 0, texList);

  glDrawElements(GL_TRIANGLES, m*6, GL_UNSIGNED_SHORT, indices);

  glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);    

  glDisable(GL_BLEND);

  glPopMatrix();

  /* Unbind framebuffer object */
  glBindFramebufferOES(GL_FRAMEBUFFER_OES, 0);

  /* And draw on screen this time */
  glViewport(0, 0, VIEWPORT_SIZE_X, VIEWPORT_SIZE_Y/2);

  glPushMatrix();

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glLoadMatrixf(orthoMat);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glDisable(GL_BLEND);

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glBindTexture(GL_TEXTURE_2D, bufferText);

  glVertexPointer(3, GL_FLOAT, 0, orthoVtx2);
  glTexCoordPointer(4, GL_FLOAT, 0, orthoTex2);

  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, orthoIndices);

  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);

  glBindTexture(GL_TEXTURE_2D, 0);
  
  if(mire>0)
  {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glVertexPointer(3, GL_FLOAT, 0, orthoVtx);
    glTexCoordPointer(4, GL_FLOAT, 0, orthoTex);
    
    glBindTexture(GL_TEXTURE_2D, flaretex[4]);  

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, orthoIndices);
    
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);  
  }

  glPopMatrix();
 
// cleanup when you don't need objects any longer
// glDeleteRenderbuffersOES(1, &renderbuffer);
// glDeleteFramebuffersOES(1, &framebuffer);


}

int hack_draw(float currentTime, float frameTime)
{
	static float times[5] = { 0.03f, 0.03f, 0.03f, 0.03f, 0.03f };
	static int timeindex = 0;

	if (frameTime > 0) {
		times[timeindex] = frameTime;
	} else {		// else use elapsedTime from last frame
		times[timeindex] = elapsedTime;
	}

	// average last 5 frame times together
	elapsedTime = 0.2f * (times[0] + times[1] + times[2] + times[3] + times[4]);
// 	printf("*************************************");
	timeindex++;
	if (timeindex >= 5)
		timeindex = 0;

	if (elapsedTime > 0.0f)
	{
		draw();
		return 1;
	}
	
	return 0;

}

void hack_init()
{
        glGenTextures (1,&bufferText);
        
        glBindTexture(GL_TEXTURE_2D, bufferText);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VIEWPORT_SIZE_X, VIEWPORT_SIZE_Y, 0, GL_RGBA,GL_UNSIGNED_BYTE, NULL);   
        
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,GL_LINEAR);
//         glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,GL_REPEAT);
//         glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,GL_REPEAT);      

        // Create framebuffer object
        glGenFramebuffersOES(1, &framebuffer);
        glBindFramebufferOES(GL_FRAMEBUFFER_OES, framebuffer);

        glGenRenderbuffersOES(1, &renderbuffer);
        glBindRenderbufferOES(GL_RENDERBUFFER_OES, renderbuffer);

        glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT16_OES, VIEWPORT_SIZE_X, VIEWPORT_SIZE_Y);

        glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, renderbuffer);

        glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, bufferText, 0);
        
        if (glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) != GL_FRAMEBUFFER_COMPLETE_OES)
        {
          printf("[hack_init] ERROR creating framebuffer\n");
          exit(-1);
        }
        
        glBindFramebufferOES(GL_FRAMEBUFFER_OES,0);

	dSmoke = 0;

	xsize = screen_width;
	ysize = screen_height;

	centerx = xsize / 2;
	centery = ysize / 2;

	// Set OpenGL state
	glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
        
        glDisable(GL_LIGHTING);
        glDisable (GL_DEPTH_TEST);
 	glDisable(GL_CULL_FACE);
        
 	glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	
	glMatrixMode(GL_MODELVIEW);	

	// Initialize data structures
	initFlares();

        float radius = sqrt(
          (lookAt[0][0] - lookFrom[0][0]) * (lookAt[0][0] - lookFrom[0][0])
        + (lookAt[0][2] - lookFrom[0][2]) * (lookAt[0][2] - lookFrom[0][2])
        );

        pitch   = RAD2DEG * atan2 (lookAt[0][1] - lookFrom[0][1], radius);
        heading = RAD2DEG * atan2 (lookFrom[0][0] - lookAt[0][0], lookFrom[0][2] - lookAt[0][2]);
        
        // get billboard rotation matrix for particles

        glPushMatrix();        
        
        glRotatef(-pitch, 1, 0, 0);
        glRotatef(-heading, 0, 1, 0);
        glTranslatef(-lookFrom[0][0], -lookFrom[0][1], -lookFrom[0][2]);

        glLoadIdentity();
        glRotatef(heading, 0, 1, 0);
        glRotatef(pitch, 1, 0, 0);
        glGetFloatv(GL_MODELVIEW_MATRIX, billboardMat);
        
        glPopMatrix();        
        
// 	if (dSmoke)
// 		initSmoke ();
// 	initWorld ();
// 	initShockwave ();

// 	lookFrom[1] = rsVec (rsRandf (3000.0f) - 1500.0f, 400.0f, rsRandf (3000.0f) - 1500.0f);
// 	lookFrom[2] = rsVec (rsRandf (1000.0f) + 5000.0f, 5.0f, rsRandf (4000.0f) - 2000.0f);

}


