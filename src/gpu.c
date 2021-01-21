#include "vesper/gpu.h"
#include <vulkan/vulkan.h>

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wavefront.inc"

enum { BATCH=8192, GROUP=64, COUNTERS=12, DEVICE_LIMIT=64 };
enum { CONFIG,NODES,PRIMITIVES,MATERIALS,LIGHTS,WORK,STATES,HITS,SHADOWS,EVENTS,QUEUES,MEDIA,DENSITY,BUFFERS };
typedef struct { float x,y,z,w; } F4;
typedef struct { uint32_t x,y,z,w; } U4;
typedef struct { F4 lo,hi; U4 link; } GNode;
typedef struct { F4 a,b,c,velocity; U4 info; } GPrimitive;
typedef struct { F4 albedo[2],emission[2],eta[2],k[2],optics,pulse; U4 info; } GMaterial;
typedef struct { F4 vector,power[2],pulse; U4 info; } GLight;
typedef struct { F4 eye,right,up,forward,shutter; U4 counts,media; } GConfig;
typedef struct { F4 origin_time,dir_beta,point_delay,previous_pdf,spectral; U4 key,tracking,boundary[4]; } GState;
typedef struct { F4 normal_t; U4 id; } GHit;
typedef struct { F4 origin_dist,dir_value,time_pulse,point; U4 key,boundary[4]; } GShadow;
typedef struct { F4 absorption[2],scattering[2],lo,hi,optics; U4 grid,kind; } GMedium;
typedef struct { uint32_t pixel,sample; } Job;
typedef struct { uint32_t stage,parity,count,seed,width,height,depth,flags,iteration; } Push;

_Static_assert(sizeof(GNode)==48 && sizeof(GPrimitive)==80 && sizeof(GMaterial)==176 &&
               sizeof(GLight)==80 && sizeof(GConfig)==112 && sizeof(GState)==176 &&
               sizeof(GHit)==32 && sizeof(GShadow)==144 && sizeof(GMedium)==144 && sizeof(Job)==8 && sizeof(Push)==36,
               "std430 ABI");

typedef struct { VkBuffer buffer; VkDeviceMemory memory; VkDeviceSize size; void *data; } Buffer;

struct VsGpu {
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkPhysicalDevice physical;
    VkPhysicalDeviceProperties limits;
    VkPhysicalDeviceMemoryProperties memory;
    VkDevice device;
    VkQueue queue;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkShaderModule shader;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool set_pool;
    VkDescriptorSet set;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    Buffer buffers[BUFFERS];
    VsDevice info;
    VsRender cfg;
    uint64_t scene,allocated;
    uint32_t validation_errors;
};

static bool check(VkResult result,const char *op,VsError *error) {
    return result==VK_SUCCESS || vs_fail(error,"%s failed with VkResult %d",op,result);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_log(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT kind,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user) {
    (void)kind;
    VsGpu *g=user;
    if (severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++g->validation_errors;
    fprintf(stderr,"Vulkan validation: %s\n",data->pMessage);
    return VK_FALSE;
}

static bool instance_ext(const char *layer,const char *name) {
    VkExtensionProperties p[128];
    uint32_t n=128;
    VkResult r=vkEnumerateInstanceExtensionProperties(layer,&n,p);
    if (r!=VK_SUCCESS && r!=VK_INCOMPLETE) return false;
    for (uint32_t i=0;i<n;++i) if (!strcmp(p[i].extensionName,name)) return true;
    return false;
}

static bool device_ext(VkPhysicalDevice device,const char *name) {
    VkExtensionProperties p[256];
    uint32_t n=256;
    VkResult r=vkEnumerateDeviceExtensionProperties(device,NULL,&n,p);
    if (r!=VK_SUCCESS && r!=VK_INCOMPLETE) return false;
    for (uint32_t i=0;i<n;++i) if (!strcmp(p[i].extensionName,name)) return true;
    return false;
}

static bool validation_available(void) {
    VkLayerProperties p[64];
    uint32_t n=64;
    VkResult r=vkEnumerateInstanceLayerProperties(&n,p);
    if (r!=VK_SUCCESS && r!=VK_INCOMPLETE) return false;
    for (uint32_t i=0;i<n;++i)
        if (!strcmp(p[i].layerName,"VK_LAYER_KHRONOS_validation")) return true;
    return false;
}

static bool make_instance(VsGpu *g,VsError *error) {
    const char *layer="VK_LAYER_KHRONOS_validation";
    const char *ext[3];
    uint32_t n=0;
    VkInstanceCreateFlags flags=0;
    if (instance_ext(NULL,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        ext[n++]=VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        flags|=VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    bool requested=getenv("VESPER_VALIDATION")!=NULL;
    bool debug=requested && validation_available() && instance_ext(NULL,VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (requested && !debug) return vs_fail(error,"Vulkan validation layer or debug utils unavailable");
    bool sync=debug && instance_ext(layer,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    if (debug) ext[n++]=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    if (sync) ext[n++]=VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
    VkDebugUtilsMessengerCreateInfoEXT message={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback=debug_log,.pUserData=g};
    VkValidationFeatureEnableEXT enable=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT features={.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,.pNext=&message,
        .enabledValidationFeatureCount=1,.pEnabledValidationFeatures=&enable};
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="Vesper",
        .applicationVersion=VK_MAKE_VERSION(0,1,0),.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.flags=flags,
        .pApplicationInfo=&app,.enabledExtensionCount=n,.ppEnabledExtensionNames=ext,
        .enabledLayerCount=debug?1u:0u,.ppEnabledLayerNames=&layer,
        .pNext=sync?(void *)&features:debug?(void *)&message:NULL};
    if (!check(vkCreateInstance(&info,NULL,&g->instance),"vkCreateInstance",error)) return false;
    if (debug) {
        PFN_vkCreateDebugUtilsMessengerEXT create=(PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(g->instance,"vkCreateDebugUtilsMessengerEXT");
        if (!create || !check(create(g->instance,&message,NULL,&g->messenger),"validation messenger",error))
            return false;
    }
    g->info.validation=debug;
    return true;
}

static uint64_t fingerprint(uint64_t h,const void *data,size_t n) {
    const unsigned char *p=data;
    for (size_t i=0;i<n;++i) h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}

static bool select_device(VsGpu *g,VsError *error) {
    VkPhysicalDevice devices[DEVICE_LIMIT];
    uint32_t n=DEVICE_LIMIT;
    VkResult r=vkEnumeratePhysicalDevices(g->instance,&n,devices);
    if (r!=VK_SUCCESS && r!=VK_INCOMPLETE) return check(r,"vkEnumeratePhysicalDevices",error);
    uint32_t family=0;
    for (uint32_t i=0;i<n && !g->physical;++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devices[i],&p);
        if ((p.deviceType!=VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
             p.deviceType!=VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) || p.apiVersion<VK_API_VERSION_1_1 ||
            p.limits.maxPerStageDescriptorStorageBuffers<BUFFERS ||
            p.limits.maxDescriptorSetStorageBuffers<BUFFERS ||
            p.limits.maxComputeWorkGroupInvocations<GROUP || p.limits.maxComputeWorkGroupSize[0]<GROUP ||
            p.limits.maxComputeWorkGroupCount[0]<BATCH/GROUP || p.limits.maxPushConstantsSize<sizeof(Push))
            continue;
        VkQueueFamilyProperties q[64];
        uint32_t m=64;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i],&m,q);
        for (uint32_t j=0;j<m;++j) if (q[j].queueCount && (q[j].queueFlags&VK_QUEUE_COMPUTE_BIT)) {
            family=j; g->physical=devices[i]; g->limits=p;
            break;
        }
    }
    if (!g->physical) return vs_fail(error,"no hardware Vulkan 1.1 device supports Vesper compute limits");
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(g->physical,&features);
    vkGetPhysicalDeviceMemoryProperties(g->physical,&g->memory);
    VkPhysicalDeviceIDProperties id={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&id};
    vkGetPhysicalDeviceProperties2(g->physical,&props);
    g->info.float64=features.shaderFloat64;
    g->info.api_version=g->limits.apiVersion;
    g->info.driver_version=g->limits.driverVersion;
    g->info.vendor_id=g->limits.vendorID;
    g->info.device_id=g->limits.deviceID;
    snprintf(g->info.name,sizeof g->info.name,"%s",g->limits.deviceName);
    snprintf(g->info.driver,sizeof g->info.driver,"%08x",g->limits.driverVersion);
    uint64_t h=fingerprint(UINT64_C(14695981039346656037),id.deviceUUID,sizeof id.deviceUUID);
    h=fingerprint(h,id.driverUUID,sizeof id.driverUUID);
    h=fingerprint(h,&g->info.driver_version,sizeof g->info.driver_version);
    g->info.identity=fingerprint(h,vs_wavefront,sizeof vs_wavefront);
    if (!g->info.identity) g->info.identity=1;
    const char *portability="VK_KHR_portability_subset";
    float priority=1;
    VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo info={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,
        .pQueueCreateInfos=&queue,.enabledExtensionCount=device_ext(g->physical,portability)?1u:0u,
        .ppEnabledExtensionNames=&portability};
    if (!check(vkCreateDevice(g->physical,&info,NULL,&g->device),"vkCreateDevice",error)) return false;
    vkGetDeviceQueue(g->device,family,0,&g->queue);
    VkCommandPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    if (!check(vkCreateCommandPool(g->device,&pool,NULL,&g->pool),"vkCreateCommandPool",error)) return false;
    VkCommandBufferAllocateInfo cmd={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=g->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkFenceCreateInfo fence={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    return check(vkAllocateCommandBuffers(g->device,&cmd,&g->cmd),"vkAllocateCommandBuffers",error) &&
        check(vkCreateFence(g->device,&fence,NULL,&g->fence),"vkCreateFence",error);
}

static bool make_buffer(VsGpu *g,Buffer *b,VkDeviceSize size,VsError *error) {
    if (!size) size=16;
    if (size>g->limits.limits.maxStorageBufferRange)
        return vs_fail(error,"Vulkan storage buffer exceeds device range");
    b->size=size;
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,
        .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    if (!check(vkCreateBuffer(g->device,&info,NULL,&b->buffer),"vkCreateBuffer",error)) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g->device,b->buffer,&req);
    if (req.size>VS_FILM_BUDGET-g->allocated) return vs_fail(error,"Vulkan allocation budget exceeded");
    uint32_t index=UINT32_MAX;
    for (uint32_t i=0;i<g->memory.memoryTypeCount;++i) {
        const VkMemoryPropertyFlags need=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VkMemoryPropertyFlags have=g->memory.memoryTypes[i].propertyFlags;
        if ((req.memoryTypeBits&(1u<<i)) && (have&need)==need) { index=i; break; }
    }
    if (index==UINT32_MAX) return vs_fail(error,"no host-coherent Vulkan storage memory");
    VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=req.size,.memoryTypeIndex=index};
    if (!check(vkAllocateMemory(g->device,&allocation,NULL,&b->memory),"vkAllocateMemory",error) ||
        !check(vkBindBufferMemory(g->device,b->buffer,b->memory,0),"vkBindBufferMemory",error) ||
        !check(vkMapMemory(g->device,b->memory,0,size,0,&b->data),"vkMapMemory",error)) return false;
    g->allocated+=req.size;
    return true;
}

static F4 vector(VsVec3 v,double w) { return (F4){(float)v.x,(float)v.y,(float)v.z,(float)w}; }

static void spectrum(F4 dst[2],VsSpectrum s) {
    for (unsigned i=0;i<2;++i) dst[i]=(F4){(float)s.values[4*i],(float)s.values[4*i+1],
                                         (float)s.values[4*i+2],(float)s.values[4*i+3]};
}

static F4 pulse(VsPulse p) { return (F4){(float)(p.start*1e9),(float)(p.width*1e9),0,0}; }

static bool pack(VsGpu *g,const VsScene *s,VsError *error) {
    uint64_t grids=0;
    for (uint32_t i=0;i<s->medium_count;++i) {
        const VsMedium *m=s->media+i;
        grids+=(uint64_t)m->nx*m->ny*m->nz;
    }
    VkDeviceSize sizes[]={sizeof(GConfig),s->bvh.node_count*sizeof(GNode),s->primitive_count*sizeof(GPrimitive),
        s->material_count*sizeof(GMaterial),s->light_count*sizeof(GLight),BATCH*sizeof(Job),
        BATCH*sizeof(GState),BATCH*sizeof(GHit),BATCH*sizeof(GShadow),
        BATCH*(2u*g->cfg.max_depth+1u)*sizeof(F4),(COUNTERS+3u*BATCH)*sizeof(uint32_t),s->medium_count*sizeof(GMedium),grids*sizeof(float)};
    for (unsigned i=0;i<BUFFERS;++i) if (!make_buffer(g,g->buffers+i,sizes[i],error)) return false;
    const VsCamera *c=s->cameras+s->camera;
    VsVec3 forward=vs_unit(vs_sub(c->target,c->position));
    VsVec3 right=vs_unit(vs_cross(forward,c->up)),up=vs_cross(right,forward);
    GConfig *conf=g->buffers[CONFIG].data;
    *conf=(GConfig){vector(c->position,c->aperture),vector(right,c->focus),vector(up,0),
        vector(forward,tan(c->vfov*VS_PI/360)),{(float)c->shutter_open,(float)c->shutter_close,0,0},
        {s->bvh.node_count,s->primitive_count,s->light_count,s->environment},{s->world_medium,0,0,0}};
    GNode *nodes=g->buffers[NODES].data;
    for (uint32_t i=0;i<s->bvh.node_count;++i) {
        VsNode n=s->bvh.nodes[i];
        F4 lo=vector(n.bounds.lo,0),hi=vector(n.bounds.hi,0);
        for (unsigned j=0;j<3;++j) {
            float *low=j==0?&lo.x:j==1?&lo.y:&lo.z;
            float *high=j==0?&hi.x:j==1?&hi.y:&hi.z;
            float margin=8*FLT_EPSILON*fmaxf(1e-5f,fmaxf(fabsf(*low),fabsf(*high)));
            *low-=margin;
            *high+=margin;
        }
        nodes[i]=(GNode){lo,hi,{n.first,n.count,n.right,0}};
    }
    uint32_t *inverse=s->primitive_count?malloc(s->primitive_count*sizeof *inverse):NULL;
    if (s->primitive_count && !inverse) return vs_fail(error,"Vulkan primitive reorder allocation failed");
    GPrimitive *primitive=g->buffers[PRIMITIVES].data;
    for (uint32_t i=0;i<s->primitive_count;++i) {
        uint32_t j=s->bvh.order[i];
        inverse[j]=i;
        VsPrimitive p=s->primitives[j];
        primitive[i]=(GPrimitive){vector(p.a,p.radius),vector(p.b,0),vector(p.c,0),vector(p.velocity,0),
            {p.shape,p.material,p.boundary,j}};
    }
    GMaterial *m=g->buffers[MATERIALS].data;
    for (uint32_t i=0;i<s->material_count;++i) {
        const VsMaterial *p=s->materials+i;
        spectrum(m[i].albedo,p->albedo); spectrum(m[i].emission,p->emission);
        spectrum(m[i].eta,p->eta); spectrum(m[i].k,p->k);
        m[i].optics=(F4){(float)p->roughness,(float)p->ior_a,(float)p->ior_b,0};
        m[i].pulse=pulse(p->pulse);
        m[i].info=(U4){p->kind,p->albedo.kind,p->emission.kind,p->medium};
    }
    GLight *l=g->buffers[LIGHTS].data;
    for (uint32_t i=0;i<s->light_count;++i) {
        const VsLight *p=s->lights+i;
        l[i].vector=vector(p->vector,0);
        spectrum(l[i].power,p->power);
        l[i].pulse=pulse(p->pulse);
        l[i].info=(U4){p->kind,p->power.kind,p->kind==VS_AREA?inverse[p->primitive]:0,0};
    }
    free(inverse);
    GMedium *media=g->buffers[MEDIA].data;
    float *density=g->buffers[DENSITY].data;
    uint32_t offset=0;
    for (uint32_t i=0;i<s->medium_count;++i) {
        const VsMedium *p=s->media+i;
        GMedium *item=media+i;
        spectrum(item->absorption,p->absorption);
        spectrum(item->scattering,p->scattering);
        item->lo=vector(p->bounds.lo,0);
        item->hi=vector(p->bounds.hi,0);
        item->optics=(F4){(float)p->anisotropy,(float)(p->majorant*(1+16*FLT_EPSILON)),0,0};
        item->grid=(U4){p->nx,p->ny,p->nz,offset};
        item->kind=(U4){p->absorption.kind,p->scattering.kind,0,0};
        uint32_t n=p->nx*p->ny*p->nz;
        if (n) memcpy(density+offset,p->density,n*sizeof *density);
        offset+=n;
    }
    return true;
}

static bool make_pipeline(VsGpu *g,VsError *error) {
    VkDescriptorSetLayoutBinding bindings[BUFFERS];
    for (uint32_t i=0;i<BUFFERS;++i)
        bindings[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=BUFFERS,.pBindings=bindings};
    if (!check(vkCreateDescriptorSetLayout(g->device,&layout,NULL,&g->set_layout),"descriptor layout",error))
        return false;
    VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,BUFFERS};
    VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1,.poolSizeCount=1,.pPoolSizes=&size};
    if (!check(vkCreateDescriptorPool(g->device,&pool,NULL,&g->set_pool),"descriptor pool",error))
        return false;
    VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=g->set_pool,.descriptorSetCount=1,.pSetLayouts=&g->set_layout};
    if (!check(vkAllocateDescriptorSets(g->device,&allocate,&g->set),"descriptor set",error))
        return false;
    VkDescriptorBufferInfo buffer[BUFFERS];
    VkWriteDescriptorSet writes[BUFFERS];
    for (uint32_t i=0;i<BUFFERS;++i) {
        buffer[i]=(VkDescriptorBufferInfo){g->buffers[i].buffer,0,g->buffers[i].size};
        writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g->set,
            .dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=buffer+i};
    }
    vkUpdateDescriptorSets(g->device,BUFFERS,writes,0,NULL);
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&g->set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    if (!check(vkCreatePipelineLayout(g->device,&pl,NULL,&g->pipeline_layout),"pipeline layout",error))
        return false;
    VkShaderModuleCreateInfo shader={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=sizeof vs_wavefront,.pCode=(const uint32_t *)(const void *)vs_wavefront};
    if (!check(vkCreateShaderModule(g->device,&shader,NULL,&g->shader),"shader module",error))
        return false;
    VkComputePipelineCreateInfo pipe={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=g->pipeline_layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
            .module=g->shader,.pName="main"}};
    return check(vkCreateComputePipelines(g->device,VK_NULL_HANDLE,1,&pipe,NULL,&g->pipeline),"compute pipeline",error);
}

void vs_gpu_close(VsGpu *g) {
    if (!g) return;
    if (g->device) {
        vkDeviceWaitIdle(g->device);
        if (g->pipeline) vkDestroyPipeline(g->device,g->pipeline,NULL);
        if (g->pipeline_layout) vkDestroyPipelineLayout(g->device,g->pipeline_layout,NULL);
        if (g->shader) vkDestroyShaderModule(g->device,g->shader,NULL);
        if (g->set_pool) vkDestroyDescriptorPool(g->device,g->set_pool,NULL);
        if (g->set_layout) vkDestroyDescriptorSetLayout(g->device,g->set_layout,NULL);
        if (g->fence) vkDestroyFence(g->device,g->fence,NULL);
        if (g->pool) vkDestroyCommandPool(g->device,g->pool,NULL);
        for (unsigned i=0;i<BUFFERS;++i) {
            Buffer *b=g->buffers+i;
            if (b->data) vkUnmapMemory(g->device,b->memory);
            if (b->buffer) vkDestroyBuffer(g->device,b->buffer,NULL);
            if (b->memory) vkFreeMemory(g->device,b->memory,NULL);
        }
        vkDestroyDevice(g->device,NULL);
    }
    if (g->messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy=(PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(g->instance,"vkDestroyDebugUtilsMessengerEXT");
        if (destroy) destroy(g->instance,g->messenger,NULL);
    }
    if (g->instance) vkDestroyInstance(g->instance,NULL);
    free(g);
}

bool vs_gpu_open(VsGpu **out,const VsScene *s,VsRender c,VsError *error) {
    if (!out || *out || !s->identity || c.backend!=VS_BACKEND_VULKAN || !vs_render_valid(c,error))
        return vs_fail(error,"invalid Vulkan scene, configuration or output");
    VsGpu *g=calloc(1,sizeof *g);
    if (!g) return vs_fail(error,"Vulkan context allocation failed");
    g->scene=s->identity;
    g->cfg=c;
    if (!make_instance(g,error) || !select_device(g,error) || !pack(g,s,error) || !make_pipeline(g,error) ||
        (g->validation_errors && !vs_fail(error,"Vulkan setup emitted validation errors"))) {
        vs_gpu_close(g);
        return false;
    }
    *out=g;
    return true;
}

const VsDevice *vs_gpu_device(const VsGpu *g) { return g?&g->info:NULL; }

static void barrier(VsGpu *g,VkPipelineStageFlags from,VkPipelineStageFlags to,
                    VkAccessFlags src,VkAccessFlags dst) {
    VkMemoryBarrier m={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=src,.dstAccessMask=dst};
    vkCmdPipelineBarrier(g->cmd,from,to,0,1,&m,0,NULL,0,NULL);
}

static void dispatch(VsGpu *g,Push *p,uint32_t stage) {
    p->stage=stage;
    vkCmdPushConstants(g->cmd,g->pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof *p,p);
    vkCmdDispatch(g->cmd,(p->count+GROUP-1)/GROUP,1,1);
}

static void compute_barrier(VsGpu *g) {
    barrier(g,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
}

static bool submit(VsGpu *g,uint32_t count,VsError *error) {
    const VkPipelineStageFlags compute=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,transfer=VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkAccessFlags shader=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (!check(vkResetCommandBuffer(g->cmd,0),"reset command buffer",error) ||
        !check(vkBeginCommandBuffer(g->cmd,&begin),"begin command buffer",error))
        return false;
    VkBuffer queues=g->buffers[QUEUES].buffer;
    vkCmdFillBuffer(g->cmd,g->buffers[EVENTS].buffer,0,VK_WHOLE_SIZE,0);
    vkCmdFillBuffer(g->cmd,queues,0,4,count);
    vkCmdFillBuffer(g->cmd,queues,4,(COUNTERS-1)*4,0);
    barrier(g,transfer,compute,VK_ACCESS_TRANSFER_WRITE_BIT,shader);
    vkCmdBindPipeline(g->cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g->pipeline);
    vkCmdBindDescriptorSets(g->cmd,VK_PIPELINE_BIND_POINT_COMPUTE,g->pipeline_layout,0,1,&g->set,0,NULL);
    Push p={.count=count,.seed=g->cfg.seed,.width=g->cfg.width,.height=g->cfg.height,.depth=g->cfg.max_depth,
        .flags=(g->cfg.nee?1u:0u)|(g->cfg.roulette?2u:0u)|(g->cfg.sampler<<2)|(g->cfg.wavelength<<4)};
    dispatch(g,&p,0);
    compute_barrier(g);
    for (;p.iteration<=p.depth;++p.iteration,p.parity^=1u) {
        if (p.iteration) {
            barrier(g,compute,transfer,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
            vkCmdFillBuffer(g->cmd,queues,4*(1-p.parity),4,0);
            vkCmdFillBuffer(g->cmd,queues,8,4,0);
            barrier(g,transfer,compute,VK_ACCESS_TRANSFER_WRITE_BIT,shader);
        }
        dispatch(g,&p,1);
        compute_barrier(g);
        dispatch(g,&p,2);
        compute_barrier(g);
        if (g->cfg.nee && p.iteration<p.depth) {
            dispatch(g,&p,3);
            compute_barrier(g);
        }
    }
    barrier(g,compute|transfer,VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
    if (!check(vkEndCommandBuffer(g->cmd),"end command buffer",error) ||
        !check(vkResetFences(g->device,1,&g->fence),"reset GPU fence",error)) return false;
    VkSubmitInfo submit_info={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&g->cmd};
    if (!check(vkQueueSubmit(g->queue,1,&submit_info,g->fence),"queue submit",error) ||
        !check(vkWaitForFences(g->device,1,&g->fence,VK_TRUE,UINT64_C(60000000000)),"GPU fence",error))
        return false;
    const uint32_t *c=g->buffers[QUEUES].data;
    const Job *jobs=g->buffers[WORK].data;
    Job failed=c[7]<count?jobs[c[7]]:(Job){0};
    return (c[3]>=count && !c[5] && !g->validation_errors) ||
        vs_fail(error,"Vulkan dispatch failed: %u rays, %u shader errors (code %u, pixel %u sample %u depth %u), %u validation errors",
                c[3],c[5],c[6],failed.pixel,failed.sample,c[9],g->validation_errors);
}

static bool collect(VsGpu *g,const VsScene *s,VsFilm *f,uint32_t count,VsStats *stats,VsError *error) {
    const Job *jobs=g->buffers[WORK].data;
    const GState *states=g->buffers[STATES].data;
    const F4 *events=g->buffers[EVENTS].data;
    uint32_t stride=2*g->cfg.max_depth+1;
    for (uint32_t i=0;i<count;++i) {
        if (f->pixels[jobs[i].pixel].samples!=jobs[i].sample || states[i].key.x!=jobs[i].pixel ||
            states[i].key.y!=jobs[i].sample)
            return vs_fail(error,"Vulkan sample identity or sequence mismatch");
        VsPath p={.wavelength={states[i].spectral.x,states[i].spectral.y}};
        for (uint32_t j=0;j<stride;++j) {
            F4 e=events[i*stride+j];
            if (e.x>0) p.events[p.count++]=(VsContribution){e.x,fmax(0,e.y)*1e-9,{e.z*1e-9,e.w*1e-9},e.y>=0};
        }
        if (!vs_film_add(f,jobs[i].pixel,&p,s->response,error)) return false;
    }
    if (stats) {
        const uint32_t *c=g->buffers[QUEUES].data;
        stats->samples+=count;
        stats->rays+=c[3]; stats->shadows+=c[4]; stats->nulls+=c[8];
    }
    return true;
}

bool vs_gpu_render(VsGpu *g,const VsScene *s,VsFilm *f,VsRegion r,uint32_t target,
                   VsStats *stats,VsError *error) {
    const VsRender a=f->render,b=g->cfg;
    if (!f->pixels || a.backend!=VS_BACKEND_VULKAN || f->scene!=g->scene || f->scene!=s->identity ||
        f->device!=g->info.identity || a.width!=b.width || a.height!=b.height || a.seed!=b.seed ||
        a.max_depth!=b.max_depth || a.sampler!=b.sampler || a.wavelength!=b.wavelength ||
        a.nee!=b.nee || a.roulette!=b.roulette || !vs_region_valid(f,r) || !target || target>VS_MAX_SPP)
        return vs_fail(error,"Vulkan render config, device, region or scene mismatch");
    Job *jobs=g->buffers[WORK].data;
    uint32_t count=0;
    for (uint32_t y=r.y;y<r.y+r.height;++y) for (uint32_t x=r.x;x<r.x+r.width;++x) {
        uint32_t pixel=y*a.width+x;
        for (uint32_t sample=f->pixels[pixel].samples;sample<target;++sample) {
            jobs[count++]=(Job){pixel,sample};
            if (count==BATCH) {
                if (!submit(g,count,error) || !collect(g,s,f,count,stats,error)) return false;
                count=0;
            }
        }
    }
    return !count || (submit(g,count,error) && collect(g,s,f,count,stats,error));
}
