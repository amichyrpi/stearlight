#pragma once
#include "hand_tracking.h"
#include "hand_math.h"
#include <GLES3/gl3.h>
#include <cstddef>

// Mesh and joints come directly from OpenXR. No Linux process, camera image,
// recorded pose stream, or downloaded hand asset is needed.
class HandRender {
 struct Mesh {GLuint vao=0,vertices=0,indices=0;GLsizei count=0;std::vector<XrPosef> bind;std::array<quest::Matrix,XR_HAND_JOINT_COUNT_EXT> transforms;};
 Mesh meshes[2];GLuint program=0,depth=0;GLint vp=-1,bones=-1,tint=-1;int depthWidth=0,depthHeight=0;
 struct Vertex {XrVector3f position,normal;XrVector4sFB joints;XrVector4f weights;};
 static GLuint shader(GLenum kind,const char* source){
  GLuint result=glCreateShader(kind);glShaderSource(result,1,&source,nullptr);glCompileShader(result);GLint ok=0;glGetShaderiv(result,GL_COMPILE_STATUS,&ok);
  if(!ok){glDeleteShader(result);return 0;}return result;
 }
 void mesh(HandTracking& tracking,int hand){
  auto& m=meshes[hand];auto& h=tracking.hands[hand];if(!h.tracker)return;
  XrHandTrackingMeshFB mesh{XR_TYPE_HAND_TRACKING_MESH_FB};
  if(XR_FAILED(tracking.getMesh(h.tracker,&mesh))||mesh.jointCountOutput!=XR_HAND_JOINT_COUNT_EXT||!mesh.vertexCountOutput||mesh.vertexCountOutput>65535||mesh.indexCountOutput>200000)return;
  m.bind.resize(mesh.jointCountOutput);std::vector<float> radii(m.bind.size());std::vector<XrHandJointEXT> parents(m.bind.size());
  std::vector<XrVector3f> positions(mesh.vertexCountOutput),normals(positions.size());std::vector<XrVector2f> uv(positions.size());
  std::vector<XrVector4sFB> joints(positions.size());std::vector<XrVector4f> weights(positions.size());std::vector<int16_t> indices(mesh.indexCountOutput);
  mesh.jointCapacityInput=m.bind.size();mesh.jointBindPoses=m.bind.data();mesh.jointRadii=radii.data();mesh.jointParents=parents.data();
  mesh.vertexCapacityInput=positions.size();mesh.vertexPositions=positions.data();mesh.vertexNormals=normals.data();mesh.vertexUVs=uv.data();mesh.vertexBlendIndices=joints.data();mesh.vertexBlendWeights=weights.data();
  mesh.indexCapacityInput=indices.size();mesh.indices=indices.data();
  if(XR_FAILED(tracking.getMesh(h.tracker,&mesh)))return;
  std::vector<Vertex> vertices(positions.size());
  for(size_t i=0;i<vertices.size();i++){
   if(weights[i].x==0)joints[i].x=0;if(weights[i].y==0)joints[i].y=0;if(weights[i].z==0)joints[i].z=0;if(weights[i].w==0)joints[i].w=0;
   for(int j:{joints[i].x,joints[i].y,joints[i].z,joints[i].w})if(j<0||j>=int(m.bind.size()))return;
   vertices[i]={positions[i],normals[i],joints[i],weights[i]};
  }
  glGenVertexArrays(1,&m.vao);glBindVertexArray(m.vao);glGenBuffers(1,&m.vertices);glBindBuffer(GL_ARRAY_BUFFER,m.vertices);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,position)));
  glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));
  glEnableVertexAttribArray(2);glVertexAttribIPointer(2,4,GL_SHORT,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,joints)));
  glEnableVertexAttribArray(3);glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,weights)));
  glGenBuffers(1,&m.indices);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m.indices);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(int16_t),indices.data(),GL_STATIC_DRAW);m.count=indices.size();glBindVertexArray(0);
  __android_log_print(ANDROID_LOG_INFO,"StearlightXR","Hand mesh %d: %zu vertices, %d indices",hand,vertices.size(),m.count);
 }
public:
 void initialize(HandTracking& tracking){
  if(!tracking.getMesh)return;
  const char* vertex=R"(#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in ivec4 joints;
layout(location=3) in vec4 weights;
uniform mat4 viewProjection;
uniform mat4 bones[26];
out vec3 n;
void main(){mat4 skin=bones[joints.x]*weights.x+bones[joints.y]*weights.y+bones[joints.z]*weights.z+bones[joints.w]*weights.w;gl_Position=viewProjection*skin*vec4(position,1.0);n=mat3(skin)*normal;}
)";
  const char* fragment=R"(#version 300 es
precision mediump float;
in vec3 n;uniform vec3 tint;out vec4 color;
void main(){float light=0.45+0.55*abs(dot(normalize(n),normalize(vec3(0.3,0.8,0.5))));color=vec4(tint*light,1.0);}
)";
  GLuint vs=shader(GL_VERTEX_SHADER,vertex),fs=shader(GL_FRAGMENT_SHADER,fragment);if(!vs||!fs){if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);return;}
  program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(program);program=0;return;}
  vp=glGetUniformLocation(program,"viewProjection");bones=glGetUniformLocation(program,"bones[0]");tint=glGetUniformLocation(program,"tint");
  for(int i=0;i<2;i++)mesh(tracking,i);
 }
 // Joint poses are shared by both eyes. Skin once for each new stereo frame.
 void update(const HandTracking& tracking){
  for(int i=0;i<2;i++){
   const auto& h=tracking.hands[i];auto& m=meshes[i];
   if(!h.visible||!m.count)continue;
   for(size_t j=0;j<m.bind.size();j++)m.transforms[j]=quest::handSkinMatrix(h.joints[j].pose,m.bind[j],h.scale);
  }
 }
 void draw(const HandTracking& tracking,const XrView& view,int width,int height){
  if(!program)return;
  if(!depth)glGenRenderbuffers(1,&depth);
  glBindRenderbuffer(GL_RENDERBUFFER,depth);
  if(width!=depthWidth||height!=depthHeight){glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,width,height);depthWidth=width;depthHeight=height;}
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
  glDepthMask(GL_TRUE);glClearDepthf(1);glClear(GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);
  glUseProgram(program);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
  auto matrix=quest::viewProjection(view);glUniformMatrix4fv(vp,1,GL_FALSE,matrix.data());
  for(int i=0;i<2;i++){
   const auto& h=tracking.hands[i];const auto& m=meshes[i];if(!h.visible||!m.count)continue;
   glUniformMatrix4fv(bones,m.transforms.size(),GL_FALSE,m.transforms[0].data());
   bool pinch=h.aim.status&XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB;
   glUniform3f(tint,pinch?.3f:.75f,pinch?.75f:.8f,pinch?1.f:.85f);
   glBindVertexArray(m.vao);glDrawElements(GL_TRIANGLES,m.count,GL_UNSIGNED_SHORT,nullptr);
  }glBindVertexArray(0);glUseProgram(0);glDisable(GL_DEPTH_TEST);
  // This attachment is cleared before the next eye and never read elsewhere.
  // Let tile-based GPUs discard it instead of storing it to system memory.
  const GLenum transient=GL_DEPTH_ATTACHMENT;
  glInvalidateFramebuffer(GL_FRAMEBUFFER,1,&transient);
 }
 void shutdown(){for(auto& m:meshes){if(m.vao)glDeleteVertexArrays(1,&m.vao);if(m.vertices)glDeleteBuffers(1,&m.vertices);if(m.indices)glDeleteBuffers(1,&m.indices);m={};}if(program)glDeleteProgram(program);if(depth)glDeleteRenderbuffers(1,&depth);program=depth=0;}
};
