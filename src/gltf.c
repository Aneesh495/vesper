#include "assets.h"
#include "wire.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <stdio.h>

#define GLTF_LIMIT (64u*1024u*1024u)
#define GLTF_NODE_LIMIT 65536u
#define GLTF_DEPTH_LIMIT 64u

typedef struct {
    VsScene *scene;
    VsPrimitive primitive;
    VsVec3 scale,translation;
    VsError *error;
    cgltf_data *data;
    unsigned char *seen;
} Import;

typedef struct { double m[16]; } Matrix;

static void *bounded_alloc(void *user,cgltf_size size) {
    size_t *allocated=user;
    if (size>GLTF_LIMIT-*allocated) return NULL;
    void *result=malloc(size?size:1);
    if (result) *allocated+=size;
    return result;
}

static void bounded_free(void *user,void *ptr) {
    (void)user;
    free(ptr);
}

static unsigned char *read_asset(const char *path,size_t *size,size_t *allocated) {
    FILE *file=fopen(path,"rb");
    if (!file) return NULL;
    unsigned char *bytes=NULL;
    if (!fseek(file,0,SEEK_END)) {
        long length=ftell(file);
        if (length>0 && (unsigned long)length<=GLTF_LIMIT &&
            (!*size || *size==(size_t)length) && !fseek(file,0,SEEK_SET)) {
            *size=(size_t)length;
            bytes=bounded_alloc(allocated,*size);
            if (bytes && fread(bytes,1,*size,file)!=*size) {
                free(bytes);
                bytes=NULL;
            }
        }
    }
    fclose(file);
    return bytes;
}

static bool load_buffers(cgltf_data *d,const char *path,size_t *allocated,VsError *error) {
    for (size_t i=0;i<d->buffers_count;++i) {
        cgltf_buffer *b=d->buffers+i;
        if (!b->size || b->size>GLTF_LIMIT)
            return vs_fail(error,"invalid glTF buffer size");
        if (!b->uri && i==0 && d->bin && d->bin_size>=b->size && d->bin_size-b->size<=3) {
            b->data=(void *)d->bin;
            continue;
        }
        char relative[4096];
        if (!b->uri || strchr(b->uri,'%') || strchr(b->uri,'?') || strchr(b->uri,'#') ||
            !vs_asset_path(relative,path,b->uri,error))
            return vs_fail(error,"glTF buffers must use GLB storage or plain relative files");
        size_t length=b->size;
        b->data=read_asset(relative,&length,allocated);
        if (!b->data) return vs_fail(error,"could not read glTF buffer with declared byteLength: %s",relative);
        b->data_free_method=cgltf_data_free_method_memory_free;
    }
    return true;
}

static bool validate_storage(cgltf_data *d,VsError *error) {
    for (size_t i=0;i<d->buffer_views_count;++i) {
        const cgltf_buffer_view *v=d->buffer_views+i;
        if (!v->buffer || v->has_meshopt_compression || !v->size ||
            v->offset>v->buffer->size || v->size>v->buffer->size-v->offset ||
            (v->stride && (v->stride<4 || v->stride>252 || v->stride%4)))
            return vs_fail(error,"invalid or unsupported glTF bufferView %zu",i);
    }
    for (size_t i=0;i<d->accessors_count;++i) {
        const cgltf_accessor *a=d->accessors+i;
        const cgltf_buffer_view *v=a->buffer_view;
        size_t component=cgltf_component_size(a->component_type);
        size_t element=cgltf_calc_size(a->type,a->component_type);
        if (!v || a->is_sparse || !component || !element || !a->count ||
            a->count>3u*VS_MAX_PRIMITIVES || a->stride<element || a->stride>252 ||
            a->stride%component || a->offset%component || v->offset%component ||
            a->offset>v->size || element>v->size-a->offset ||
            a->count-1>(v->size-a->offset-element)/a->stride)
            return vs_fail(error,"invalid or unsupported glTF accessor %zu",i);
    }
    /* Buffer data is still unloaded. Index bounds are checked below using
       explicit little-endian loads after the storage checks above. */
    return cgltf_validate(d)==cgltf_result_success || vs_fail(error,"glTF structure failed validation");
}

static const unsigned char *element(const cgltf_accessor *a,size_t index) {
    const cgltf_buffer_view *v=a->buffer_view;
    return (const unsigned char *)v->buffer->data+v->offset+a->offset+index*a->stride;
}

static VsVec3 vector_at(const cgltf_accessor *a,size_t i) {
    const unsigned char *b=element(a,i);
    return vs_v(vs_load_f32(b),vs_load_f32(b+4),vs_load_f32(b+8));
}

static VsVec2 uv_at(const cgltf_accessor *a,size_t i) {
    const unsigned char *b=element(a,i);
    if (a->component_type==cgltf_component_type_r_32f)
        return (VsVec2){vs_load_f32(b),vs_load_f32(b+4)};
    unsigned size=(unsigned)cgltf_component_size(a->component_type);
    double max=size==1?255:65535;
    return (VsVec2){vs_load_u(b,size)/max,vs_load_u(b+size,size)/max};
}

static Matrix multiply(Matrix a,Matrix b) {
    Matrix c={0};
    for (unsigned col=0;col<4;++col) for (unsigned row=0;row<4;++row)
        for (unsigned k=0;k<4;++k) c.m[4*col+row]+=a.m[4*k+row]*b.m[4*col+k];
    return c;
}

static VsVec3 apply(Matrix matrix,VsVec3 p) {
    const double *m=matrix.m;
    return vs_v(m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
                m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]);
}

static bool checked_matrix(const cgltf_node *node,Matrix parent,Matrix *result) {
    float transform[16];
    cgltf_node_transform_local(node,transform);
    Matrix local;
    for (unsigned i=0;i<16;++i) {
        if (!isfinite(transform[i])) return false;
        local.m[i]=transform[i];
    }
    if (local.m[3] || local.m[7] || local.m[11] || local.m[15]!=1) return false;
    *result=multiply(parent,local);
    for (unsigned i=0;i<16;++i) if (!isfinite(result->m[i])) return false;
    return true;
}

static bool uv_format(const cgltf_accessor *a) {
    return a->type==cgltf_type_vec2 &&
        ((a->component_type==cgltf_component_type_r_32f && !a->normalized) ||
         ((a->component_type==cgltf_component_type_r_8u ||
           a->component_type==cgltf_component_type_r_16u) && a->normalized));
}

static bool mesh_primitive(Import *import,const cgltf_primitive *source,Matrix world) {
    if (source->type!=cgltf_primitive_type_triangles || source->targets_count ||
        source->has_draco_mesh_compression || source->extensions_count)
        return vs_fail(import->error,"glTF primitive must be uncompressed static triangles");
    const cgltf_accessor *position=NULL,*uv=NULL,*normal=NULL,*index=source->indices;
    for (size_t i=0;i<source->attributes_count;++i) {
        const cgltf_attribute *attr=source->attributes+i;
        if (attr->type==cgltf_attribute_type_position && attr->index==0 && !position) position=attr->data;
        else if (attr->type==cgltf_attribute_type_texcoord && attr->index==0 && !uv) uv=attr->data;
        else if (attr->type==cgltf_attribute_type_normal && attr->index==0 && !normal) normal=attr->data;
        else return vs_fail(import->error,"unsupported or duplicate glTF vertex attribute");
    }
    if (!position || position->type!=cgltf_type_vec3 || position->component_type!=cgltf_component_type_r_32f ||
        position->normalized || (uv && (!uv_format(uv) || uv->count!=position->count)) ||
        (normal && (normal->type!=cgltf_type_vec3 || normal->component_type!=cgltf_component_type_r_32f ||
                   normal->normalized || normal->count!=position->count)))
        return vs_fail(import->error,"unsupported glTF position, UV or normal accessor");
    if (index && (index->type!=cgltf_type_scalar || index->normalized ||
        (index->component_type!=cgltf_component_type_r_8u && index->component_type!=cgltf_component_type_r_16u &&
         index->component_type!=cgltf_component_type_r_32u)))
        return vs_fail(import->error,"unsupported glTF index accessor");
    size_t count=index?index->count:position->count;
    if (count%3 || count/3>VS_MAX_PRIMITIVES-import->scene->primitive_count)
        return vs_fail(import->error,"invalid glTF triangle count or scene primitive limit");

    const double *m=world.m;
    VsVec3 a=vs_v(m[0],m[1],m[2]),b=vs_v(m[4],m[5],m[6]),c=vs_v(m[8],m[9],m[10]);
    VsVec3 cofactors[]={vs_cross(b,c),vs_cross(c,a),vs_cross(a,b)};
    double det=vs_dot(a,cofactors[0]);
    if (!isfinite(det) || det==0) return vs_fail(import->error,"singular glTF instance transform");
    for (size_t i=0;i<count;i+=3) {
        VsPrimitive p=import->primitive;
        p.has_uv=uv!=NULL;
        p.has_normals=normal!=NULL;
        VsVec3 v[3];
        for (unsigned j=0;j<3;++j) {
            size_t ordinal=i+(det<0 && j>0?3-j:j);
            size_t id=index?(size_t)vs_load_u(element(index,ordinal),
                (unsigned)cgltf_component_size(index->component_type)):ordinal;
            if (id>=position->count) return vs_fail(import->error,"glTF triangle index outside POSITION accessor");
            VsVec3 point=apply(world,vector_at(position,id));
            v[j]=vs_add(vs_v(point.x*import->scale.x,point.y*import->scale.y,point.z*import->scale.z),
                        import->translation);
            if (uv) p.uv[j]=uv_at(uv,id);
            if (normal) {
                VsVec3 source_normal=vector_at(normal,id);
                if (!(fabs(vs_length(source_normal)-1)<=.005))
                    return vs_fail(import->error,"invalid glTF vertex normal");
                VsVec3 t=vs_add(vs_add(vs_mul(cofactors[0],source_normal.x),vs_mul(cofactors[1],source_normal.y)),
                               vs_mul(cofactors[2],source_normal.z));
                t=vs_v(t.x/import->scale.x,t.y/import->scale.y,t.z/import->scale.z);
                double length=vs_length(t);
                if (!(length>0 && isfinite(length))) return vs_fail(import->error,"invalid glTF normal transform");
                p.normals[j]=vs_mul(t,(det<0?-1:1)/length);
            }
        }
        p.a=v[0]; p.b=v[1]; p.c=v[2];
        if (!vs_scene_add_primitive(import->scene,p,import->error)) return false;
    }
    return true;
}

static bool node_geometry(Import *import,const cgltf_node *node,Matrix parent,unsigned depth) {
    size_t i=(size_t)(node-import->data->nodes);
    if (depth>=GLTF_DEPTH_LIMIT || i>=import->data->nodes_count || import->seen[i] ||
        node->skin || node->camera || node->light || node->has_mesh_gpu_instancing ||
        node->weights_count || node->extensions_count)
        return vs_fail(import->error,"unsupported glTF node or repeated/deep scene graph");
    import->seen[i]=1;
    Matrix world;
    if (!checked_matrix(node,parent,&world)) return vs_fail(import->error,"invalid glTF node transform");
    if (node->mesh) {
        if (node->mesh->weights_count) return vs_fail(import->error,"glTF morphing is outside the static subset");
        for (size_t j=0;j<node->mesh->primitives_count;++j)
            if (!mesh_primitive(import,node->mesh->primitives+j,world)) return false;
    }
    for (size_t j=0;j<node->children_count;++j)
        if (!node_geometry(import,node->children[j],world,depth+1)) return false;
    return true;
}

bool vs_load_gltf(VsScene *s,VsPrimitive p,VsVec3 scale,VsVec3 translation,const char *path,VsError *error) {
    size_t length=0,allocated=0;
    unsigned char *bytes=read_asset(path,&length,&allocated);
    if (!bytes) return vs_fail(error,"glTF file read failed or exceeds budget: %s",path);
    cgltf_options options={.memory={bounded_alloc,bounded_free,&allocated}};
    cgltf_data *data=NULL;
    uint32_t first=s->primitive_count;
    uint64_t old_id=s->identity;
    bool ok=cgltf_parse(&options,bytes,length,&data)==cgltf_result_success;
    if (!ok) vs_fail(error,"invalid glTF container: %s",path);
    if (ok && (!data->asset.version || strcmp(data->asset.version,"2.0") ||
        !data->scenes_count || !data->nodes_count || data->nodes_count>GLTF_NODE_LIMIT ||
        data->extensions_used_count || data->extensions_required_count ||
        data->skins_count || data->animations_count))
        ok=vs_fail(error,"glTF requires a static 2.0 scene without extensions");
    if (ok) ok=validate_storage(data,error) && load_buffers(data,path,&allocated,error);
    if (ok) {
        const cgltf_scene *chosen=data->scene?data->scene:data->scenes;
        unsigned char *seen=bounded_alloc(&allocated,data->nodes_count);
        if (!seen) ok=vs_fail(error,"glTF scene traversal allocation limit");
        else {
            memset(seen,0,data->nodes_count);
            Import import={s,p,scale,translation,error,data,seen};
            const Matrix identity={{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
            for (size_t i=0;ok && i<chosen->nodes_count;++i)
                ok=node_geometry(&import,chosen->nodes[i],identity,0);
            free(seen);
        }
    }
    if (ok && s->primitive_count==first) ok=vs_fail(error,"glTF scene contains no usable triangles");
    if (data) cgltf_free(data);
    free(bytes);
    if (!ok) {
        s->primitive_count=first;
        s->identity=old_id;
    }
    return ok;
}
