﻿/****************************************************************************
 * ==> Inter-Quake model demo ----------------------------------------------*
 ****************************************************************************
 * Description : Inter-Quake model demo                                     *
 * Developer   : Jean-Milost Reymond                                        *
 * Copyright   : 2017 - 2022, this file is part of the CompactStar Engine.  *
 *               You are free to copy or redistribute this file, modify it, *
 *               or use it for your own projects, commercial or not. This   *
 *               file is provided "as is", WITHOUT ANY WARRANTY OF ANY      *
 *               KIND. THE DEVELOPER IS NOT RESPONSIBLE FOR ANY DAMAGE OF   *
 *               ANY KIND, ANY LOSS OF DATA, OR ANY LOSS OF PRODUCTIVITY    *
 *               TIME THAT MAY RESULT FROM THE USAGE OF THIS SOURCE CODE,   *
 *               DIRECTLY OR NOT.                                           *
 ****************************************************************************/

// supported platforms check. NOTE iOS only, but may works on other platforms
#if !defined(_OS_IOS_) && !defined(_OS_ANDROID_) && !defined(_OS_WINDOWS_)
    #error "Not supported platform!"
#endif

#ifdef CCR_FORCE_LLVM_INTERPRETER
    #error "Clang/LLVM on iOS does not support function pointer yet. Consider using CPP built-in compiler."
#endif

// std
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

// opengl
#include <gles2.h>
#include <gles2ext.h>

// compactStar engine
#include "SDK/CSR_Common.h"
#include "SDK/CSR_Geometry.h"
#include "SDK/CSR_Collision.h"
#include "SDK/CSR_Vertex.h"
#include "SDK/CSR_Model.h"
#include "SDK/CSR_Iqm.h"
#include "SDK/CSR_Renderer.h"
#include "SDK/CSR_Renderer_OpenGL.h"
#include "SDK/CSR_Scene.h"
#include "SDK/CSR_AI.h"
#include "SDK/CSR_Sound.h"
#include "SDK/CSR_DebugHelper.h"

#include <ccr.h>

// uncomment this line to show the skeleton
//#define SHOW_SKELETON

// resource files
#define IQM_FILE "Resources/mrfixit.iqm"

//----------------------------------------------------------------------------
const char g_VSTextured[] =
    "precision mediump float;"
    "attribute vec3 csr_aVertices;"
    "attribute vec4 csr_aColor;"
    "attribute vec2 csr_aTexCoord;"
    "uniform   mat4 csr_uProjection;"
    "uniform   mat4 csr_uView;"
    "uniform   mat4 csr_uModel;"
    "varying   vec4 csr_vColor;"
    "varying   vec2 csr_vTexCoord;"
    "void main(void)"
    "{"
    "    csr_vColor    = csr_aColor;"
    "    csr_vTexCoord = csr_aTexCoord;"
    "    gl_Position   = csr_uProjection * csr_uView * csr_uModel * vec4(csr_aVertices, 1.0);"
    "}";
//----------------------------------------------------------------------------
const char g_FSTextured[] =
    "precision mediump float;"
    "uniform sampler2D csr_sColorMap;"
    "varying lowp vec4 csr_vColor;"
    "varying      vec2 csr_vTexCoord;"
    "void main(void)"
    "{"
    "    gl_FragColor = csr_vColor * texture2D(csr_sColorMap, csr_vTexCoord);"
    "}";
//------------------------------------------------------------------------------
const char g_VSLine[] =
    "precision mediump float;"
    "attribute    vec3 csr_aVertices;"
    "attribute    vec4 csr_aColor;"
    "uniform      mat4 csr_uProjection;"
    "uniform      mat4 csr_uView;"
    "uniform      mat4 csr_uModel;"
    "varying lowp vec4 csr_vColor;"
    "void main(void)"
    "{"
    "    csr_vColor  = csr_aColor;"
    "    gl_Position = csr_uProjection * csr_uView * csr_uModel * vec4(csr_aVertices, 1.0);"
    "}";
//------------------------------------------------------------------------------
const char g_FSLine[] =
    "precision mediump float;"
    "varying lowp vec4 csr_vColor;"
    "void main(void)"
    "{"
    "    gl_FragColor = csr_vColor;"
    "}";
//------------------------------------------------------------------------------
CSR_Scene*        g_pScene       = 0;
CSR_OpenGLShader* g_pShader      = 0;
CSR_OpenGLShader* g_pLineShader  = 0;
CSR_IQM*          g_pModel       = 0;
CSR_SceneContext  g_SceneContext;
CSR_Matrix4       g_Matrix;
size_t            g_AnimCount    = 0;
size_t            g_AnimMaxFrame = 101;
float             g_Angle        = 0.0f;
CSR_OpenGLID      g_ID[2];
//---------------------------------------------------------------------------
CSR_PixelBuffer* OnLoadTexture(const char* pTextureName)
{
    char* pFileName = (char*)malloc(10 + strlen(pTextureName));
    strcpy(pFileName,      "Resources/\0");
    strcpy(pFileName + 10, pTextureName);

    return csrPixelBufferFromTgaFile(pFileName);
}
//---------------------------------------------------------------------------
void OnApplySkin(size_t index, const CSR_Skin* pSkin, int* pCanRelease)
{
    size_t id;

    if (!pSkin || !pSkin->m_Texture.m_pFileName)
        return;

    if (strcmp(pSkin->m_Texture.m_pFileName, "Body.tga") == 0)
        id = 0;
    else
        id = 1;

    // should not be hardcoded, however there is only 1 model which will use this function in this demo,
    // so it is safe to do that
    g_ID[id].m_pKey     = (void*)(&pSkin->m_Texture);
    g_ID[id].m_ID       = csrOpenGLTextureFromPixelBuffer(pSkin->m_Texture.m_pBuffer);
    g_ID[id].m_UseCount = 1;

    // from now the source texture will no longer be used
    if (pCanRelease)
        *pCanRelease = 1;
}
//---------------------------------------------------------------------------
void* OnGetID(const void* pKey)
{
    size_t       id;
    CSR_Texture* pTexture;

    pTexture = (CSR_Texture*)pKey;

    if (!pTexture || !pTexture->m_pFileName)
        return 0;

    if (strcmp(pTexture->m_pFileName, "Body.tga") == 0)
        id = 0;
    else
        id = 1;

    return &g_ID[id];
}
//---------------------------------------------------------------------------
void OnDeleteTexture(const CSR_Texture* pTexture)
{
    size_t id;

    if (!pTexture || !pTexture->m_pFileName)
        return;

    if (strcmp(pTexture->m_pFileName, "Body.tga") == 0)
        id = 0;
    else
        id = 1;

    // unuse the texture
    if (g_ID[id].m_UseCount)
        --g_ID[id].m_UseCount;

    // is texture no longer used?
    if (g_ID[id].m_UseCount)
        return;

    // delete the texture from the GPU
    if (g_ID[id].m_ID != M_CSR_Error_Code)
    {
        glDeleteTextures(1, (GLuint*)(&g_ID[id].m_ID));
        g_ID[id].m_ID = M_CSR_Error_Code;
    }
}
//---------------------------------------------------------------------------
void* OnGetShader(const void* pModel, CSR_EModelType type)
{
    csrShaderEnable(g_pShader);
    return g_pShader;
}
//---------------------------------------------------------------------------
void OnGetIQMIndex(const CSR_IQM* pIQM, size_t* pAnimSetIndex, size_t* pFrameIndex)
{
    *pAnimSetIndex = 0;
    *pFrameIndex   = g_AnimCount % g_AnimMaxFrame;
}
//---------------------------------------------------------------------------
void BuildCharMatrix(CSR_Matrix4* pMatrix)
{
    CSR_Vector3 axis;
    CSR_Vector3 factor;
    CSR_Matrix4 rotateXMatrix;
    CSR_Matrix4 rotateYMatrix;
    CSR_Matrix4 scaleMatrix;
    CSR_Matrix4 intermediateMatrix;
    float       normalMat[3][3];

    csrMat4Identity(pMatrix);

    // set rotation axis
    axis.m_X = 1.0f;
    axis.m_Y = 0.0f;
    axis.m_Z = 0.0f;

    // create the rotation matrix
    csrMat4Rotate(-((float)M_PI / 2.0), &axis, &rotateXMatrix);

    // set rotation axis
    axis.m_X = 0.0f;
    axis.m_Y = 1.0f;
    axis.m_Z = 0.0f;

    // create the rotation matrix
    csrMat4Rotate((float)(-M_PI / 4.0) + g_Angle, &axis, &rotateYMatrix);

    // set scale factor
    factor.m_X = 0.05f;
    factor.m_Y = 0.05f;
    factor.m_Z = 0.05f;

    // create the scale matrix
    csrMat4Scale(&factor, &scaleMatrix);

    // build the model matrix
    csrMat4Multiply(&scaleMatrix,        &rotateXMatrix, &intermediateMatrix);
    csrMat4Multiply(&intermediateMatrix, &rotateYMatrix, pMatrix);

    // place it in the scene
    pMatrix->m_Table[3][0] =  0.1f;
    pMatrix->m_Table[3][1] = -0.2f;
    pMatrix->m_Table[3][2] = -1.0f;
}
//------------------------------------------------------------------------------
void CreateViewport(float w, float h)
{
    // calculate matrix items
    const float zNear  = 0.01f;
    const float zFar   = 100.0f;
    const float fov    = 45.0f;
    const float aspect = w / h;

    csrMat4Perspective(fov, aspect, zNear, zFar, &g_pScene->m_ProjectionMatrix);

    csrShaderEnable(g_pShader);

    // connect projection matrix to shader
    GLint projectionSlot = glGetUniformLocation(g_pShader->m_ProgramID, "csr_uProjection");
    glUniformMatrix4fv(projectionSlot, 1, 0, &g_pScene->m_ProjectionMatrix.m_Table[0][0]);

    #ifdef SHOW_SKELETON
        csrShaderEnable(g_pLineShader);

        // connect projection matrix to line shader
        projectionSlot = glGetUniformLocation(g_pLineShader->m_ProgramID, "csr_uProjection");
        glUniformMatrix4fv(projectionSlot, 1, 0, &g_pScene->m_ProjectionMatrix.m_Table[0][0]);
    #endif
}
//------------------------------------------------------------------------------
void on_GLES2_Init(int view_w, int view_h)
{
    CSR_VertexFormat  vertexFormat;
    CSR_VertexCulling vertexCulling;
    CSR_Material      material;
    CSR_Vector3       camera;

    // initialize the scene
    g_pScene = csrSceneCreate();

    // configure the scene color background
    g_pScene->m_Color.m_R = 0.08f;
    g_pScene->m_Color.m_G = 0.12f;
    g_pScene->m_Color.m_B = 0.17f;
    g_pScene->m_Color.m_A = 1.0f;

    // configure the scene view matrix
    csrMat4Identity(&g_pScene->m_ViewMatrix);

    // configure the scene context
    csrSceneContextInit(&g_SceneContext);
    g_SceneContext.m_fOnGetShader   = OnGetShader;
    g_SceneContext.m_fOnGetIQMIndex = OnGetIQMIndex;
    g_SceneContext.m_fOnGetID       = OnGetID;

    #ifdef SHOW_SKELETON
        // compile, link and use shader
        g_pLineShader = csrOpenGLShaderLoadFromStr(&g_VSLine[0],
                                                    sizeof(g_VSLine),
                                                   &g_FSLine[0],
                                                    sizeof(g_FSLine),
                                                    0,
                                                    0);

        // succeeded?
        if (!g_pLineShader)
        {
        	printf("FAILED to compile line shader\n");
            return;
        }

        csrShaderEnable(g_pLineShader);

        // get line shader attributes
        g_pLineShader->m_VertexSlot = glGetAttribLocation(g_pLineShader->m_ProgramID, "csr_aVertices");
        g_pLineShader->m_ColorSlot  = glGetAttribLocation(g_pLineShader->m_ProgramID, "csr_aColor");
    #endif

    // compile, link and use shader
    g_pShader = csrOpenGLShaderLoadFromStr(&g_VSTextured[0],
                                            sizeof(g_VSTextured),
                                           &g_FSTextured[0],
                                            sizeof(g_FSTextured),
                                            0,
                                            0);

    // succeeded?
    if (!g_pShader)
    {
    	printf("FAILED to compile shader\n");
        return;
    }

    csrShaderEnable(g_pShader);

    // get shader attributes
    g_pShader->m_VertexSlot   = glGetAttribLocation(g_pShader->m_ProgramID, "csr_aVertices");
    g_pShader->m_ColorSlot    = glGetAttribLocation(g_pShader->m_ProgramID, "csr_aColor");
    g_pShader->m_TexCoordSlot = glGetAttribLocation(g_pShader->m_ProgramID, "csr_aTexCoord");
    g_pShader->m_TextureSlot  = glGetAttribLocation(g_pShader->m_ProgramID, "csr_sTexture");

    // configure OpenGL depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDepthRangef(0.0f, 1.0f);

    BuildCharMatrix(&g_Matrix);

    // configure the vertex format
    vertexFormat.m_HasNormal         = 0;
    vertexFormat.m_HasTexCoords      = 1;
    vertexFormat.m_HasPerVertexColor = 1;

    vertexCulling.m_Face = CSR_CF_CCW;
    vertexCulling.m_Type = CSR_CT_None;

    // configure the material
    material.m_Color       = 0xFFFFFFFF;
    material.m_Transparent = 0;
    material.m_Wireframe   = 0;

    // load the IQM model
    g_pModel = csrIQMOpen(IQM_FILE,
                         &vertexFormat,
                         &vertexCulling,
                         &material,
                          0,
                          0,
                          0,
                          OnLoadTexture,
                          OnApplySkin,
                          OnDeleteTexture);

    // add the model to the scene
    CSR_SceneItem* pSceneItem = csrSceneAddIQM(g_pScene, g_pModel, 0, 0);
    csrSceneAddModelMatrix(g_pScene, g_pModel, &g_Matrix);
}
//------------------------------------------------------------------------------
void on_GLES2_Final()
{
    // delete the scene
    csrSceneRelease(g_pScene, 0);
    g_pScene = 0;

    // delete scene shader
    csrOpenGLShaderRelease(g_pShader);
    g_pShader = 0;

    // delete line shader
    #ifdef SHOW_SKELETON
        csrOpenGLShaderRelease(g_pLineShader);
        g_pLineShader = 0;
    #endif
}
//------------------------------------------------------------------------------
void on_GLES2_Size(int view_w, int view_h)
{
    glViewport(0, 0, view_w, view_h);
    CreateViewport(view_w, view_h);
}
//------------------------------------------------------------------------------
void on_GLES2_Update(float timeStep_sec)
{
    // rebuild the model matrix
    BuildCharMatrix(&g_Matrix);

    // update the animation counter
    g_AnimCount += timeStep_sec * 100.0f;
}
//------------------------------------------------------------------------------
void on_GLES2_Render()
{
    // draw the scene
    csrSceneDraw(g_pScene, &g_SceneContext);

    #ifdef SHOW_SKELETON
        // enable the line shader and connect the view matrix
        csrShaderEnable(g_pLineShader);
        csrOpenGLShaderConnectViewMatrix(g_pLineShader, &g_pScene->m_ViewMatrix);

        // connect model matrix to shader
        GLint slot = glGetUniformLocation(g_pLineShader->m_ProgramID, "csr_uModel");
        glUniformMatrix4fv(slot, 1, 0, &g_Matrix.m_Table[0][0]);

        // draw the skeleton
        csrDebugDrawSkeletonIQM(g_pModel, g_pLineShader, 0, g_AnimCount % g_AnimMaxFrame);
    #endif
}
//------------------------------------------------------------------------------
void on_GLES2_TouchBegin(float x, float y)
{}
//------------------------------------------------------------------------------
void on_GLES2_TouchEnd(float x, float y)
{}
//------------------------------------------------------------------------------
void on_GLES2_TouchMove(float prev_x, float prev_y, float x, float y)
{
	g_Angle += (x - prev_x) * 0.001f;
}
//------------------------------------------------------------------------------
#if __CCR__ > 2 || (__CCR__ == 2 && (__CCR_MINOR__ > 2 || ( __CCR_MINOR__ == 2 && __CCR_PATCHLEVEL__ >= 1)))
    int main()
    {
        ccrSet_GLES2_Init_Callback(on_GLES2_Init);
        ccrSet_GLES2_Final_Callback(on_GLES2_Final);
        ccrSet_GLES2_Size_Callback(on_GLES2_Size);
        ccrSet_GLES2_Update_Callback(on_GLES2_Update);
        ccrSet_GLES2_Render_Callback(on_GLES2_Render);
        ccrSet_GLES2_TouchBegin_Callback(on_GLES2_TouchBegin);
        ccrSet_GLES2_TouchMove_Callback(on_GLES2_TouchMove);
        ccrSet_GLES2_TouchEnd_Callback(on_GLES2_TouchEnd);

        ccrBegin_GLES2_Drawing();

        while (ccrGetEvent(false) != CCR_EVENT_QUIT);

        ccrEnd_GLES2_Drawing();

        return 0;
    }
#endif
//------------------------------------------------------------------------------