#pragma once
typedef unsigned int GLuint; typedef int GLint; typedef unsigned int GLenum; typedef int GLsizei; typedef float GLfloat; typedef char GLchar; typedef long GLsizeiptr; typedef unsigned char GLboolean;
#define GL_VERTEX_SHADER 1
#define GL_FRAGMENT_SHADER 2
#define GL_COMPILE_STATUS 3
#define GL_ARRAY_BUFFER 4
#define GL_FLOAT 5
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TEXTURE_2D 6
#define GL_RGBA8 7
#define GL_RGBA 8
#define GL_UNSIGNED_BYTE 9
#define GL_TEXTURE_MIN_FILTER 10
#define GL_TEXTURE_MAG_FILTER 11
#define GL_NEAREST 12
#define GL_LINEAR 13
#define GL_TEXTURE_WRAP_S 14
#define GL_TEXTURE_WRAP_T 15
#define GL_CLAMP_TO_EDGE 16
#define GL_STREAM_DRAW 17
#define GL_STATIC_DRAW 25
#define GL_TRIANGLES 18
#define GL_TEXTURE0 19
#define GL_DEPTH_TEST 20
#define GL_LEQUAL 21
#define GL_BLEND 22
#define GL_CULL_FACE 23
#define GL_ONE 24
inline GLuint glCreateShader(GLenum){return 1;} inline void glShaderSource(GLuint,int,const char**,const int*){} inline void glCompileShader(GLuint){}
inline void glGetShaderiv(GLuint,GLenum,GLint*p){*p=1;} inline void glGetShaderInfoLog(GLuint,int,int*,char*){}
inline GLuint glCreateProgram(){return 1;} inline void glAttachShader(GLuint,GLuint){} inline void glLinkProgram(GLuint){}
inline GLint glGetUniformLocation(GLuint,const char*){return 0;} inline void glGenBuffers(int,GLuint*p){*p=1;} inline void glGenVertexArrays(int,GLuint*p){*p=1;}
inline void glBindVertexArray(GLuint){} inline void glBindBuffer(GLenum,GLuint){} inline void glEnableVertexAttribArray(GLuint){}
inline void glVertexAttribPointer(GLuint,int,GLenum,GLboolean,int,const void*){} inline void glGenTextures(int,GLuint*p){*p=1;}
inline void glBindTexture(GLenum,GLuint){} inline void glTexImage2D(GLenum,int,int,int,int,int,GLenum,GLenum,const void*){} inline void glTexParameteri(GLenum,GLenum,int){}
inline void glUseProgram(GLuint){} inline void glUniformMatrix4fv(GLint,int,GLboolean,const float*){} inline void glUniform1f(GLint,float){} inline void glUniform1i(GLint,int){}
inline void glActiveTexture(GLenum){} inline void glBindSampler(GLuint,GLuint){} inline void glEnable(GLenum){} inline void glDisable(GLenum){} inline void glDepthFunc(GLenum){} inline void glDepthMask(GLboolean){}
inline void glBlendFunc(GLenum,GLenum){} 
void hostDraw(const void* v, int n, int bytesPerVert);
inline void glBufferData(GLenum,long size,const void* d,GLenum){ if(d) hostDraw(d, int(size/40), 40); }
inline void glBufferSubData(GLenum,long,long size,const void* d){ hostDraw(d, int(size/40), 40); }
inline void glDrawArrays(GLenum,int,int){}
#define GLAD_API_PTR
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_LINK_STATUS 0x8B82
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_SHORT 0x1403
inline void glGetProgramiv(GLuint, GLenum, GLint* v){ if(v)*v=1; }
inline void glDeleteProgram(GLuint){}
inline void glUniform3f(GLint, float, float, float){}
inline void glDrawElements(GLenum, GLsizei, GLenum, const void*){}
