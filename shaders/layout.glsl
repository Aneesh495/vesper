const float PI=3.14159265358979323846;
const float C_M_PER_NS=.299792458;
const float FAR=3.402823e38;
const uint NONE=0xffffffffu;
const uint BATCH=8192u;

struct Node { vec4 lo,hi; uvec4 link; };
struct Primitive { vec4 a,b,c,velocity; uvec4 info; };
struct Material { vec4 albedo[2],emission[2],eta[2],k[2],optics,pulse; uvec4 info; };
struct Light { vec4 vector,power[2],pulse; uvec4 info; };
struct State {
    vec4 origin_time,dir_beta,point_delay,previous_pdf,spectral;
    uvec4 key,tracking;
    uvec4 boundary[4];
};
struct Hit { vec4 normal_t; uvec4 id; };
struct Shadow {
    vec4 origin_dist,dir_value,time_pulse,point;
    uvec4 key,boundary[4];
};
struct Medium { vec4 absorption[2],scattering[2],lo,hi,optics; uvec4 grid,kind; };
struct Ray { vec3 o,d; float tmin,tmax,time; };

layout(std430,binding=0) readonly buffer SceneConfig {
    vec4 eye,right,up,forward;
    vec4 shutter;
    uvec4 counts,media;
} scene;
layout(std430,binding=1) readonly buffer Nodes { Node nodes[]; };
layout(std430,binding=2) readonly buffer Primitives { Primitive primitives[]; };
layout(std430,binding=3) readonly buffer Materials { Material materials[]; };
layout(std430,binding=4) readonly buffer Lights { Light lights[]; };
layout(std430,binding=5) readonly buffer Work { uvec2 work[]; };
layout(std430,binding=6) buffer States { State paths[]; };
layout(std430,binding=7) buffer Hits { Hit hits[]; };
layout(std430,binding=8) buffer Shadows { Shadow shadows[]; };
layout(std430,binding=9) buffer Events { vec4 events[]; };
layout(std430,binding=10) buffer Queues { uint counter[12]; uint queue[]; };
layout(std430,binding=11) readonly buffer Media { Medium media[]; };
layout(std430,binding=12) readonly buffer Densities { float density[]; };
layout(push_constant) uniform Push {
    uint stage,parity,count,seed,width,height,depth,flags,iteration;
} pc;

void fault(uint code) {
    atomicAdd(counter[5],1u);
    uint previous=atomicCompSwap(counter[6],0u,code);
    if (previous==0u) counter[7]=gl_GlobalInvocationID.x;
}
bool finite1(float x) { return !isnan(x) && !isinf(x); }
