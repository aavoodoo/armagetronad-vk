/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
  
***************************************************************************

*/

#include "rModel.h"
#include "rModelMesh.h"
#include <string>
#include <fstream>
#include <stdlib.h>
#include "rScreen.h"
#include "tString.h"
#include "tDirectories.h"
#include "tConfiguration.h"
#include "tLocale.h"
#include <string.h>

#define DONTDOIT
#include "rRender.h"

void rModel::Load(std::istream &in,const char *fileName){

#ifndef DEDICATED
    modelTexFacesCoherent = false;
    bool calc_normals=true;

    if (strstr(fileName,".mod")){ // old loader
        REAL xmax=-1E+32f, xmin=1E+32f;
        REAL zmax=-1E+32f, zmin=1E+32f;

        while (in && !in.eof()){
            char c;

            REAL x,y,z;
            int A,B,C;
            int num;

            in >> c;
            switch (c){
            case('v'):
                            in >> num >> x >> y >> z;

                if (x > xmax)
                    xmax = x;
                if (x < xmin)
                    xmin = x;
                if (z > zmax)
                    zmax = z;
                if (z < zmin)
                    zmin = z;

                vertices[num]=Vec3(x,y,z);
                normals[num]=Vec3(0,0,0);
                break;

            case('f'):
                            in >> A >> B >> C;
                modelFaces[modelFaces.Len()]=rModelFace(A,B,C);
                break;

            default:
                break;
            }
        }

        int i;
        for (i = vertices.Len()-1; i>=0; i--)
{
            Vec3 &v = vertices[i];
            texVert[i] = Vec3((v.x[0]-xmin)/(xmax-xmin), (zmax-v.x[2])/(zmax-zmin), 0);
        }
        modelTexFacesCoherent = true;
        for (i = modelFaces.Len()-1; i>=0; i--)
        {
            modelTexFaces[i] = modelFaces[i];
        }

    }
    else{ // new 3DSMax loader
        int offset=0;       //
        int current_vertex=0; //

        tArray< int > translate(10000);

        enum {VERTICES,FACES} status=VERTICES;

        int my_vert=1;

        while(in && !in.eof()){
            char c;
            char word[100];
            in >> c;
            if (c=='*'){
                in >> word;
                if (!strcmp(word,"MESH_TVERT")){
                    int n;
                    REAL x,y,z;
                    in >> n >> x >> y >> z;
                    texVert[n]=Vec3(x,1-y,z);
                }

                if (!strcmp(word,"MESH_TFACE")){
                    int n;
                    int a,b,c;
                    in >> n >> a >> b >> c;
                    modelTexFaces[n]=rModelFace(a,b,c);
                }

                if (!strcmp(word,"MESH_VERTEX")){
                    if (status==FACES){
                        status=VERTICES;
                        offset+=current_vertex+1;
                    }

                    float vec[3];
                    in >> current_vertex >> vec[0] >> vec[1] >> vec[2];
                    int realvert=current_vertex+offset;

                    translate[realvert]=-1;
                    /*
                      for(int i=realvert-1;i>0;i--){
                      float dist=0;
                      for(int j=2;j>=0;j--)
                      dist+= (vec[j]-vertices[i].x[j])*(vec[j]-vertices[i].x[j]);
                      if (dist<.1)
                      translate[realvert]=i;
                      }
                    */
                    if (translate[realvert]<0){
                        translate[realvert]=my_vert;
                        //std::cerr << "v " << my_vert;
                        for(int i=0;i<3;i++){
                            //std::cerr << '\t' << vec[i]*.025; // change inch to meters
                            vertices[my_vert].x[i]=vec[i]*.025;
                        }
                        //std::cerr << '\n';
                        my_vert++;
                    }
                }
                if (!strcmp(word,"MESH_FACE")){
                    status=FACES;
                    in >> word;
                    in >> word;
                    //std::cerr << "f ";

                    int face=modelFaces.Len();

                    if (strcmp(word,"A:")){
                        std::cerr << "wrong face format: expected A:, got " << word << '\n';
                        exit(-1);
                    }
                    int n;
                    in >> n;
                    modelFaces[face].A[0]=translate[n+offset];
                    //std::cerr << '\t' << translate[n+offset];

                    in >> word;
                    if (strcmp(word,"B:")){
                        std::cerr << "wrong face format: expected B:, got " << word << '\n';
                        exit(-1);
                    }
                    in >> n;
                    modelFaces[face].A[1]=translate[n+offset];
                    // std::cerr << '\t' << translate[n+offset];

                    in >> word;
                    if (strcmp(word,"C:")){
                        std::cerr << "wrong face format: expected C:, got " << word << '\n';
                        exit(-1);
                    }
                    in >> n;
                    modelFaces[face].A[2]=translate[n+offset];
                    // std::cerr << '\t' << translate[n+offset];

                    // std::cerr << '\n';

                    word[0]='\0';
                }

            }
        }
    }

    if (calc_normals)
        for(int i=modelFaces.Len()-1;i>=0;i--){
            Vec3 &A=vertices(modelFaces[i].A[0]);
            Vec3 &B=vertices(modelFaces[i].A[1]);
            Vec3 &C=vertices(modelFaces[i].A[2]);
            Vec3 X(B.x[0]-A.x[0],B.x[1]-A.x[1],B.x[2]-A.x[2]);
            Vec3 Y(C.x[0]-A.x[0],C.x[1]-A.x[1],C.x[2]-A.x[2]);
            Vec3 normal(X.x[1]*Y.x[2]-X.x[2]*Y.x[1],
                        X.x[2]*Y.x[0]-X.x[0]*Y.x[2],
                        X.x[0]*Y.x[1]-X.x[1]*Y.x[0]);
            normal=normal*(1/normal.Norm());

            for(int j=2;j>=0;j--)
                normals[modelFaces[i].A[j]]+=normal;
        }
    for(int i=normals.Len()-1;i>=0;i--){
        REAL len = normals[i].Norm();
        if (len > 1e-6f)
            normals[i]=normals[i]*(1/len);
        else
            normals[i]=Vec3(0,1,0);
    }

#endif
}

rModel::rModel(const char *fileName)
    : meshBuilt_(false)
{
#ifndef DEDICATED
    //	tString s;
    //s << "models/";
    //	s << fileName;

    std::ifstream in;

    if ( !tDirectories::Data().Open( in, fileName ) )
    {
        tERR_ERROR("\n\nModel file " << fileName << " could not be found.\n" <<
                   "are you sure you are running " << tOutput("$program_name") << " in it's own directory?\n\n");
    }
    else
    {
        Load(in,fileName);
    }
#endif
}

#ifndef DEDICATED

// Build VBO mesh from loaded model data
void rModel::BuildMesh()
{
    if (meshBuilt_ || modelFaces.Len() == 0)
    {
        return;
    }

    mesh_ = std::make_unique<rModelMesh>();

    // Check if we have texture coordinates
    bool hasTexCoords = (texVert.Len() > 0) &&
                        (modelTexFaces.Len() == modelFaces.Len() || modelTexFacesCoherent);

    // Build interleaved vertex data
    // For non-coherent tex faces, we need to expand vertices
    std::vector<rModelVertex> verts;
    std::vector<unsigned int> indices;

    if (modelTexFacesCoherent)
    {
        // Vertex and tex coords share indices - use indexed rendering
        verts.reserve(vertices.Len());
        indices.reserve(modelFaces.Len() * 3);

        for (int i = 1; i < vertices.Len(); i++)
        {
            rModelVertex v;
            v.position[0] = vertices[i].x[0];
            v.position[1] = vertices[i].x[1];
            v.position[2] = vertices[i].x[2];

            if (normals.Len() > i)
            {
                v.normal[0] = normals[i].x[0];
                v.normal[1] = normals[i].x[1];
                v.normal[2] = normals[i].x[2];
            }

            if (hasTexCoords && texVert.Len() > i)
            {
                v.texcoord[0] = texVert[i].x[0];
                v.texcoord[1] = texVert[i].x[1];
                v.texcoord[2] = texVert[i].x[2];
            }

            verts.push_back(v);
        }

        // Build index array (adjust for 0-based indexing in mesh)
        for (int i = 0; i < modelFaces.Len(); i++)
        {
            indices.push_back(modelFaces[i].A[0] - 1);
            indices.push_back(modelFaces[i].A[1] - 1);
            indices.push_back(modelFaces[i].A[2] - 1);
        }

        mesh_->Build(verts, indices);
    }
    else
    {
        // Non-coherent - expand all triangles
        verts.reserve(modelFaces.Len() * 3);

        for (int i = 0; i < modelFaces.Len(); i++)
        {
            for (int j = 0; j < 3; j++)
            {
                rModelVertex v;
                int vi = modelFaces[i].A[j];

                v.position[0] = vertices(vi).x[0];
                v.position[1] = vertices(vi).x[1];
                v.position[2] = vertices(vi).x[2];

                if (normals.Len() > vi)
                {
                    v.normal[0] = normals(vi).x[0];
                    v.normal[1] = normals(vi).x[1];
                    v.normal[2] = normals(vi).x[2];
                }

                if (modelTexFaces.Len() > i)
                {
                    int ti = modelTexFaces[i].A[j];
                    if (texVert.Len() > ti)
                    {
                        v.texcoord[0] = texVert(ti).x[0];
                        v.texcoord[1] = texVert(ti).x[1];
                        v.texcoord[2] = texVert(ti).x[2];
                    }
                }

                verts.push_back(v);
            }
        }

        mesh_->Build(verts);
    }

    meshBuilt_ = true;
}

// Render using VBO mesh
void rModel::RenderVBO()
{
    if (!meshBuilt_)
    {
        BuildMesh();
    }

    if (mesh_ && mesh_->IsValid())
    {
        // Models use lighting and textures
        sr_SetLightingEnabled(true);
        sr_SetTextureEnabled(texVert.Len() > 0);

        // No face culling: .mod files predate culling and may have inconsistent winding.
        // The original immediate-mode renderer never enabled GL_CULL_FACE for models.
        mesh_->Render();

        // Reset state
        sr_SetLightingEnabled(false);
    }
}

void rModel::Render(){
    if (!sr_glOut)
        return;

    RenderVBO();
}
#endif

rModelMesh& rModel::GetMesh(){
    if (!meshBuilt_) BuildMesh();
    return *mesh_;
}

rModel::~rModel(){
    tCHECK_DEST;
}

static std::map<std::string, rModel *> sr_modelCache;

//! returns a model from the cache
rModel * rModel::GetModel(const char * filename)
{
    std::string key(filename);
    if(sr_modelCache.find(key) == sr_modelCache.end())
    {
        std::ifstream in;
        rModel * model = 0;
        if ( tDirectories::Data().Open( in, filename ) )
        {
            model = tNEW(rModel( filename ));
        }
        sr_modelCache[key] = model;
    }

    return sr_modelCache[key];
}

//! clears the model cache
void rModel::ClearCache()
{
    for(std::map<std::string, rModel *>::iterator iter = sr_modelCache.begin(); iter != sr_modelCache.end(); ++iter)
    {
        delete (*iter).second;
    }

    sr_modelCache.clear();
}





