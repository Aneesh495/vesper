#include "vesper/geometry.h"
#include <stdlib.h>

enum { BINS=16, LEAF=4 };

typedef struct { VsBounds bounds; uint32_t count; } Bin;
typedef struct { VsBounds bounds; VsVec3 center; } Reference;
typedef struct { VsBvh *tree; Reference *refs; } Builder;
typedef struct { double cost, origin, scale; unsigned axis, bin; } Split;

static double surface(VsBounds b) {
    VsVec3 v=vs_sub(b.hi,b.lo);
    return 2*(v.x*v.y+v.x*v.z+v.y*v.z);
}

static unsigned bin_index(double position, double origin, double scale) {
    return (unsigned)fmin(BINS-1,fmax(0,(position-origin)*scale));
}

static Split split_search(Builder *build, uint32_t first, uint32_t count,
                          VsBounds parent, VsBounds centers) {
    Split best={.cost=count*surface(parent)};
    for (unsigned axis=0;axis<3;++axis) {
        double lo=vs_at(centers.lo,axis), hi=vs_at(centers.hi,axis);
        if (hi-lo<=0 || !isfinite(BINS/(hi-lo))) continue;
        double scale=BINS/(hi-lo);
        Bin bins[BINS];
        for (unsigned i=0;i<BINS;++i) bins[i]=(Bin){vs_bounds_empty(),0};
        for (uint32_t i=first;i<first+count;++i) {
            Reference *ref=build->refs+build->tree->order[i];
            unsigned j=bin_index(vs_at(ref->center,axis),lo,scale);
            bins[j].bounds=vs_bounds_union(bins[j].bounds,ref->bounds);
            ++bins[j].count;
        }
        uint32_t right_count[BINS];
        double right_cost[BINS];
        VsBounds right=vs_bounds_empty(), left=vs_bounds_empty();
        uint32_t nr=0,nl=0;
        for (unsigned j=BINS-1;j>0;--j) {
            right=vs_bounds_union(right,bins[j].bounds);
            nr+=bins[j].count;
            right_count[j]=nr;
            right_cost[j]=nr?nr*surface(right):0;
        }
        for (unsigned j=0;j+1<BINS;++j) {
            left=vs_bounds_union(left,bins[j].bounds);
            nl+=bins[j].count;
            if (!nl || !right_count[j+1]) continue;
            double cost=surface(parent)+nl*surface(left)+right_cost[j+1];
            if (cost<best.cost) best=(Split){cost,lo,scale,axis,j};
        }
    }
    return best;
}

static uint32_t build_node(Builder *build, uint32_t first, uint32_t count, uint32_t depth) {
    VsBvh *tree=build->tree;
    uint32_t index=tree->node_count++;
    if (tree->depth<depth) tree->depth=depth;
    VsBounds bounds=vs_bounds_empty(),centers=vs_bounds_empty();
    for (uint32_t i=first;i<first+count;++i) {
        Reference *ref=build->refs+tree->order[i];
        bounds=vs_bounds_union(bounds,ref->bounds);
        centers=vs_bounds_union(centers,(VsBounds){ref->center,ref->center});
    }
    tree->nodes[index]=(VsNode){bounds,first,count,0};
    if (count<=LEAF || depth>=VS_BVH_STACK-1) return index;
    Split split=split_search(build,first,count,bounds,centers);
    uint32_t mid=first;
    if (split.scale>0) {
        for (uint32_t i=first;i<first+count;++i) {
            uint32_t id=tree->order[i];
            if (bin_index(vs_at(build->refs[id].center,split.axis),split.origin,split.scale)>split.bin)
                continue;
            tree->order[i]=tree->order[mid];
            tree->order[mid++]=id;
        }
    }
    /* Bound leaf work even for coincident centroids. */
    if (mid==first || mid==first+count) {
        if (count<=16) return index;
        mid=first+count/2;
    }
    uint32_t left=build_node(build,first,mid-first,depth+1);
    uint32_t right=build_node(build,mid,first+count-mid,depth+1);
    tree->nodes[index]=(VsNode){bounds,left,0,right};
    return index;
}

bool vs_bvh_build(VsBvh *bvh, const VsPrimitive *p, uint32_t count) {
    if (!bvh || count>VS_MAX_PRIMITIVES || (count && !p)) return false;
    VsBvh tree={.count=count};
    if (count) {
        Reference *refs=malloc(count*sizeof *refs);
        tree.order=malloc(count*sizeof *tree.order);
        tree.nodes=calloc(2u*count-1u,sizeof *tree.nodes);
        if (!refs || !tree.order || !tree.nodes) {
            free(refs);
            vs_bvh_destroy(&tree);
            return false;
        }
        for (uint32_t i=0;i<count;++i) {
            if (!vs_primitive_valid(p+i)) {
                free(refs);
                vs_bvh_destroy(&tree);
                return false;
            }
            VsBounds b=vs_primitive_bounds(p+i);
            refs[i]=(Reference){b,vs_add(vs_mul(b.lo,.5),vs_mul(b.hi,.5))};
            tree.order[i]=i;
        }
        Builder builder={&tree,refs};
        build_node(&builder,0,count,1);
        free(refs);
    }
    vs_bvh_destroy(bvh);
    *bvh=tree;
    return true;
}

void vs_bvh_destroy(VsBvh *bvh) {
    if (!bvh) return;
    free(bvh->nodes);
    free(bvh->order);
    *bvh=(VsBvh){0};
}

static bool encloses(VsBounds a, VsBounds b) {
    return a.lo.x<=b.lo.x && a.lo.y<=b.lo.y && a.lo.z<=b.lo.z &&
           a.hi.x>=b.hi.x && a.hi.y>=b.hi.y && a.hi.z>=b.hi.z;
}

typedef struct { unsigned char *nodes,*primitives; uint32_t leaves,visited,depth; } Visits;

static bool validate_node(const VsBvh *tree, const VsPrimitive *p, uint32_t i,
                          uint32_t depth, Visits *visits) {
    if (i>=tree->node_count || visits->nodes[i] || depth>=VS_BVH_STACK) return false;
    visits->nodes[i]=1;
    ++visits->visited;
    if (visits->depth<depth) visits->depth=depth;
    VsNode n=tree->nodes[i];
    if (n.count) {
        if (n.first>tree->count || n.count>tree->count-n.first) return false;
        for (uint32_t j=0;j<n.count;++j) {
            uint32_t id=tree->order[n.first+j];
            if (id>=tree->count || visits->primitives[id] ||
                !encloses(n.bounds,vs_primitive_bounds(p+id))) return false;
            visits->primitives[id]=1;
            ++visits->leaves;
        }
        return true;
    }
    return n.first<tree->node_count && n.right<tree->node_count &&
           encloses(n.bounds,tree->nodes[n.first].bounds) && encloses(n.bounds,tree->nodes[n.right].bounds) &&
           validate_node(tree,p,n.first,depth+1,visits) && validate_node(tree,p,n.right,depth+1,visits);
}

bool vs_bvh_validate(const VsBvh *tree, const VsPrimitive *p) {
    if (!tree || tree->count>VS_MAX_PRIMITIVES) return false;
    if (!tree->count) return tree->node_count==0;
    if (!tree->nodes || !tree->order || !p || tree->node_count>2u*tree->count-1u) return false;
    Visits v={.nodes=calloc(tree->node_count,1),.primitives=calloc(tree->count,1)};
    bool ok=v.nodes && v.primitives && validate_node(tree,p,0,1,&v) &&
            v.leaves==tree->count && v.visited==tree->node_count && v.depth==tree->depth;
    free(v.nodes);
    free(v.primitives);
    return ok;
}

bool vs_bvh_hit(const VsBvh *tree, const VsPrimitive *p, VsRay ray, VsHit *hit) {
    if (!tree->node_count || !vs_bounds_hit(tree->nodes[0].bounds,ray,NULL)) return false;
    struct { uint32_t node; double t; } stack[VS_BVH_STACK];
    unsigned count=0;
    stack[count++].node=0;
    stack[0].t=ray.min;
    bool found=false;
    while (count) {
        --count;
        if (stack[count].t>ray.max) continue;
        VsNode node=tree->nodes[stack[count].node];
        if (node.count) {
            for (uint32_t i=0;i<node.count;++i) {
                uint32_t id=tree->order[node.first+i];
                VsHit candidate;
                if (!vs_intersect(p+id,ray,&candidate)) continue;
                if (!found || candidate.t<ray.max || id<hit->primitive) {
                    candidate.primitive=id;
                    *hit=candidate;
                    ray.max=hit->t;
                    found=true;
                }
            }
            continue;
        }
        double a,b;
        bool left=vs_bounds_hit(tree->nodes[node.first].bounds,ray,&a);
        bool right=vs_bounds_hit(tree->nodes[node.right].bounds,ray,&b);
        if (left && right && a<b) {
            stack[count].node=node.right; stack[count++].t=b;
            stack[count].node=node.first; stack[count++].t=a;
        } else {
            if (left) { stack[count].node=node.first; stack[count++].t=a; }
            if (right) { stack[count].node=node.right; stack[count++].t=b; }
        }
    }
    return found;
}
