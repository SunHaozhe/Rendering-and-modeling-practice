// --------------------------------------------------------------------------
// Copyright(C) 2009-2016
// Tamy Boubekeur
//
// Permission granted to use this code only for teaching projects and
// private practice.
//
// Do not distribute this code outside the teaching assignements.
// All rights reserved.
// --------------------------------------------------------------------------

#include <GL/glew.h>
#include <GL/glut.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>

#include "Vec3.h"
#include "Camera.h"
#include "Mesh.h"
#include "GLProgram.h"
#include "Exception.h"
#include "LightSource.h"

using namespace std;

static const unsigned int DEFAULT_SCREENWIDTH = 1024;
static const unsigned int DEFAULT_SCREENHEIGHT = 768;
static const string DEFAULT_MESH_FILE ("models/man.off");

static const string appTitle ("Informatique Graphique & Realite Virtuelle - Travaux Pratiques - Algorithmes de Rendu");
static const string myName ("Haozhe Sun");
static GLint window;
static unsigned int FPS = 0;
static bool fullScreen = false;

static Camera camera;
static Mesh mesh;
GLProgram * glProgram;

static std::vector<Vec3f> colorResponses; // Cached per-vertex color response, updated at each frame
static std::vector<LightSource> lightSources;

//speeds of interactions
static float lightMoveSpeed = 0.5f;
static float alphaSpeed = 0.01f;
static float F0Speed = 0.01f;

static float kd = M_PI;           //coefficient diffusion
static float ks = 1;							//coefficient specular
static float fd = kd / M_PI; 			//Lambert BRDF (diffusion)
static float s = 1;               //shininess
static float alpha = 0.5f;         //roughness
static float F0 = 0.5f;						//Fresnel refraction index, dependent on material

static bool microFacet = true;		//Blinn-Phong BRDF / micro facet BRDF
static bool ggx = false;					//Cook-Torrance micro facet BRDF / GGX micro facet BRDF
static bool schlick = false;			//Approximation of Schlick for GGX micro facet BRDF

//coefficients for attenuation, aq the coefficient for d^2, al the coefficient for d, ac the constant coefficient, where d means the distance between the vertex and the light source
static const float ac = 0;
static const float al = 1;
static const float aq = 0;

void printUsage () {
	std::cerr << std::endl<< appTitle << std::endl
         << "Author: " << myName << std::endl << std::endl
         << "Usage: ./main [<file.off>]" << std::endl
         << "Commands:" << std::endl
         << "------------------" << std::endl
         << " ?: Print help" << std::endl
		 << " w: Toggle wireframe mode" << std::endl
         << " <drag>+<left button>: rotate model" << std::endl
         << " <drag>+<right button>: move model" << std::endl
         << " <drag>+<middle button>: zoom" << std::endl
				 << " <f>: full screen mode"<< std::endl
				 << " <w>: skeleton mode"<< std::endl
				 << " <left button> / <right button>: move the red light source"<< std::endl
				 << " <c>: micro facet mode / Blinn-Phong mode for specular reflection"<< std::endl
				 << " <v>: Cook-Torrance micro facet mode / GGX micro facet mode for specular reflection"<< std::endl
				 << " <b>: Smith for GGX micro facet mode / Approximation of Schlick for GGX micro facet mode for specular reflection"<< std::endl
				 << " <r>: increase roughness alpha for micro facet mode"<< std::endl
				 << " <t>: decrease roughness alpha for micro facet mode"<< std::endl
				 << " <y>: increase Fresnel refraction index F0 for micro facet mode"<< std::endl
				 << " <u>: decrease Fresnel refraction index F0 for micro facet mode"<< std::endl
         << " q, <esc>: Quit" << std::endl << std::endl;
}

void init (const char * modelFilename) {
    glewExperimental = GL_TRUE;
    glewInit (); // init glew, which takes in charges the modern OpenGL calls (v>1.2, shaders, etc)
    glCullFace (GL_BACK);     // Specifies the faces to cull (here the ones pointing away from the camera)
    glEnable (GL_CULL_FACE); // Enables face culling (based on the orientation defined by the CW/CCW enumeration).
    glDepthFunc (GL_LESS); // Specify the depth test for the z-buffer
    glEnable (GL_DEPTH_TEST); // Enable the z-buffer in the rasterization
    glEnableClientState (GL_VERTEX_ARRAY);
    glEnableClientState (GL_NORMAL_ARRAY);
    glEnableClientState (GL_COLOR_ARRAY);
    glEnable (GL_NORMALIZE);
	glLineWidth (2.0); // Set the width of edges in GL_LINE polygon mode
    glClearColor (0.0f, 0.0f, 0.0f, 1.0f); // Background color
	mesh.loadOFF (modelFilename);
    colorResponses.resize (mesh.positions ().size ());
    camera.resize (DEFAULT_SCREENWIDTH, DEFAULT_SCREENHEIGHT);
    try {
        glProgram = GLProgram::genVFProgram ("Simple GL Program", "shader.vert", "shader.frag"); // Load and compile pair of shaders
        glProgram->use (); // Activate the shader program

    } catch (Exception & e) {
        cerr << e.msg () << endl;
    }

		//8 light sources maximum
		lightSources.resize(8);
		lightSources[0] = LightSource(Vec3f(1.0f, 1.0f, 1.0f), Vec3f(1.0f, 0.9f, 0.8f));
		lightSources[0].activeLightSource();
		lightSources[1] = LightSource(Vec3f(-2.0f, -1.0f, -1.0f), Vec3f(1.0f, 0.8f, 1.0f));
		lightSources[1].activeLightSource();
		lightSources[2] = LightSource(Vec3f(0.0f, 1.0f, 1.0f), Vec3f(1.0f, 0.0f, 0.0f));
		lightSources[2].activeLightSource();
}

float G1Schlick(Vec3f w, Vec3f n){
	float k = alpha * sqrt(2.0f / M_PI);
	return dot(n, w) / (dot(n, w) * (1 - k) + k);
}

float G1Smith(Vec3f w, float alpha2, Vec3f n){
	return 2 * dot(n, w) / (dot(n, w) + sqrt(alpha2 + (1 - alpha2) * pow(dot(n, w), 2)));
}

float microFacetFs(Vec3f n, Vec3f wi, Vec3f wo, Vec3f wh){
	float nwh2 = pow(dot(n, wh), 2);
	float F = F0 + (1 - F0) * pow(1 - max(0.0f, dot(wi, wh)), 5);
	if(ggx == false){
		//Cook-Torrance micro facet mode
		float alpha2 = pow(alpha, 2);
		float D = exp((nwh2 - 1.0f) / (alpha2 * nwh2)) / (pow(nwh2, 2) * alpha2 * M_PI);

		float shading = 2 * (dot(n, wh) * dot(n, wi)) / dot(wo, wh);
		float masking = 2 * (dot(n, wh) * dot(n, wo)) / dot(wo, wh);
		float G = min(1.0f, min(shading, masking));

		return (D * F * G) / (4 * dot(n, wi) * dot(n, wo));
	}else{
		//GGX micro facet mode
		float alphap = alpha;
		float alphap2 = pow(alphap, 2);
		float alpha2 = pow(alpha, 2);
		float D = alphap2 / (M_PI * pow(1.0f + (alphap2 - 1) * nwh2, 2));
		float G;
		if(schlick == false) 	G = G1Smith(wi, alpha2, n) * G1Smith(wo, alpha2, n);
		else 								 	G = G1Schlick(wi, n) * G1Schlick(wo, n); 							//approximation of Schlick

		return (D * F * G) / (4 * dot(n, wi) * dot(n, wo));
	}
}

// the following color response shall be replaced with a proper reflectance evaluation/shadow test/etc.
void updatePerVertexColorResponse () {
    for (unsigned int i = 0; i < colorResponses.size (); i++){
        colorResponses[i] = Vec3f (0.f, 0.f, 0.f);
				for(unsigned int lightIndex = 0; lightIndex < lightSources.size(); lightIndex++){
					if(lightSources[lightIndex].isActive()){
						LightSource lighSource = lightSources[lightIndex];
						Vec3f x = mesh.positions()[i]; 		//coordinates of this vertex
						Vec3f n = mesh.normals()[i];  		//normal vector of this vertex
						Vec3f cameraPosition;
						camera.getPos(cameraPosition);
						Vec3f wo = (cameraPosition - x);
						wo.normalize();
						Vec3f wi = (lighSource.getPosition() - x);
						wi.normalize();
						Vec3f Li = lighSource.getColor();
						Vec3f wh = (wi + wo);
						wh.normalize();									 //half vector
						float fs;
						if(microFacet == false) fs = ks * pow(dot(n, wh), s);						//Blinn-Phong BRDF (specular)
						else fs = microFacetFs(n, wi, wo, wh);													//Micro facet Cook-Torrance BRDF (specular)
						float f = fd + fs;
						float d = (lighSource.getPosition() - x).length();					//distance between the vertex x and the light source
						float attenuation = 1 / (ac + al * d + aq * d * d);
						colorResponses[i] += Li * f * max(dot(n, wi), 0.0f) * attenuation;
					}
				}
		}
}

void renderScene () {
    //updatePerVertexColorResponse ();
    glVertexPointer (3, GL_FLOAT, sizeof (Vec3f), (GLvoid*)(&(mesh.positions()[0])));
    glNormalPointer (GL_FLOAT, 3*sizeof (float), (GLvoid*)&(mesh.normals()[0]));
    glColorPointer (3, GL_FLOAT, sizeof (Vec3f), (GLvoid*)(&(colorResponses[0])));
    glDrawElements (GL_TRIANGLES, 3*mesh.triangles().size(), GL_UNSIGNED_INT, (GLvoid*)((&mesh.triangles()[0])));
}

void reshape(int w, int h) {
    camera.resize (w, h);
}

void display () {
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    camera.apply ();
    renderScene ();
    glFlush ();
    glutSwapBuffers ();
}

void key (unsigned char keyPressed, int x, int y) {
    switch (keyPressed) {
    case 'f':
        if (fullScreen) {
            glutReshapeWindow (camera.getScreenWidth (), camera.getScreenHeight ());
            fullScreen = false;
        } else {
            glutFullScreen ();
            fullScreen = true;
        }
        break;
    case 'q':
    case 27:
        exit (0);
        break;
    case 'w':
        GLint mode[2];
		glGetIntegerv (GL_POLYGON_MODE, mode);
		glPolygonMode (GL_FRONT_AND_BACK, mode[1] ==  GL_FILL ? GL_LINE : GL_FILL);
        break;
        break;
		case 'c':
				microFacet = ! microFacet;
				break;
		case 'v':
				ggx = ! ggx;
				break;
		case 'b':
				schlick = ! schlick;
				break;
		//roughness
		case 'r':
				alpha = min((alpha + alphaSpeed), 1.0f);
				break;
		case 't':
				alpha = max((alpha - alphaSpeed), alphaSpeed);
				break;
		//Fresnel refraction index, dependent on material
		case 'y':
				F0 = min((F0 + F0Speed), 1.0f);
				break;
		case 'u':
				F0 = max((F0 - F0Speed), 0.0f);
				break;
    default:
        printUsage ();
        break;
    }
}

void specialKey(GLint key, GLint x, GLint y){
    switch (key) {
            break;
   	    case GLUT_KEY_UP:
            break;
        case GLUT_KEY_DOWN:
            break;
        case GLUT_KEY_LEFT:
						lightSources[2].moveXBy(-lightMoveSpeed);
            break;
        case GLUT_KEY_RIGHT:
						lightSources[2].moveXBy(lightMoveSpeed);
            break;
        default:
            break;
    }
}

void mouse (int button, int state, int x, int y) {
    camera.handleMouseClickEvent (button, state, x, y);
}

void motion (int x, int y) {
    camera.handleMouseMoveEvent (x, y);
}

void idle () {
    static float lastTime = glutGet ((GLenum)GLUT_ELAPSED_TIME);
    static unsigned int counter = 0;
    counter++;
    float currentTime = glutGet ((GLenum)GLUT_ELAPSED_TIME);
    if (currentTime - lastTime >= 1000.0f) {
        FPS = counter;
        counter = 0;
        static char winTitle [128];
        unsigned int numOfTriangles = mesh.triangles ().size ();
        sprintf (winTitle, "Number Of Triangles: %d - FPS: %d", numOfTriangles, FPS);
        string title = appTitle + " - By " + myName  + " - " + winTitle;
        glutSetWindowTitle (title.c_str ());
        lastTime = currentTime;
    }
    glutPostRedisplay ();
}

int main (int argc, char ** argv) {
    if (argc > 2) {
        printUsage ();
        exit (1);
    }
    glutInit (&argc, argv);
    glutInitDisplayMode (GLUT_RGBA | GLUT_DEPTH | GLUT_DOUBLE);
    glutInitWindowSize (DEFAULT_SCREENWIDTH, DEFAULT_SCREENHEIGHT);
    window = glutCreateWindow (appTitle.c_str ());
    init (argc == 2 ? argv[1] : DEFAULT_MESH_FILE.c_str ());
    glutIdleFunc (idle);
    glutReshapeFunc (reshape);
    glutDisplayFunc (display);
    glutKeyboardFunc (key);
		glutSpecialFunc(specialKey);
    glutMotionFunc (motion);
    glutMouseFunc (mouse);
    printUsage ();
    glutMainLoop ();
    return 0;
}
