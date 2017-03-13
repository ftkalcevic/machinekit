#include "rtapi_math.h"
#include "posemath.h"
#include "kinematics.h"
#include "probekins.h"
#include "lineardeltakins-common.h"

#ifdef RTAPI

#include "rtapi.h"      /* RTAPI realtime OS API */
#include "rtapi_app.h"      /* RTAPI realtime module decls */
#include "hal.h"

#define VTVERSION VTKINEMATICS_VERSION1

struct haldata {
    hal_float_t *r, *l, *j0off, *j1off, *j2off;

    hal_bit_t *enable;
    hal_float_t *delta_z; // debugging - show last z correction
    hal_u32_t *flags;
#define PRINT_STATS 1
#define PRINT_MESH 2
} *haldata;

static void pk_update(void *h, long l);
static int shm_id;
static int key = SHMEM_KEY;
static mesh_struct *mp = 0;
static int size = 512;

static int tests, faces, hits;
static double z_dir[3] = {0,0,1};
static long long int total_nsec;
#define MOVE_COUNT 10000
static hal_bit_t old_enable;
static int move_count;
static hal_float_t move_z;

// Moeller/Trumbore ray/triangle intersection
// http://www.lighthouse3d.com/tutorials/maths/ray-triangle-intersection/
// adapted to do line intersection

#define EPSILON 0.00001
#define innerProduct(v,q)			\
    ((v)[0] * (q)[0] +				\
     (v)[1] * (q)[1] +				\
     (v)[2] * (q)[2])

#define crossProduct(a,b,c)				\
    (a)[0] = (b)[1] * (c)[2] - (c)[1] * (b)[2];		\
       (a)[1] = (b)[2] * (c)[0] - (c)[2] * (b)[0];	\
       (a)[2] = (b)[0] * (c)[1] - (c)[0] * (b)[1];

/* a = b - c */
#define vector(a,b,c)				\
    (a)[0] = (b)[0] - (c)[0];			\
       (a)[1] = (b)[1] - (c)[1];		\
       (a)[2] = (b)[2] - (c)[2];

#define PARALLEL -1
#define OUTSIDE   0
#define INSIDE    1

static inline int lineIntersectsTriangle(const double *p, const double *d,
				 const vertex_t v0, const vertex_t v1,
				 const vertex_t v2, double *t)
{
    double e1[3],e2[3],h[3],s[3],q[3];
    double a,f,u,v;

    vector(e1,v1,v0);
    vector(e2,v2,v0);
    crossProduct(h,d,e2);
    a = innerProduct(e1,h);

    if (a > -EPSILON && a < EPSILON)
	// the vector is parallel to the plane (the intersection is at infinity)
	return(PARALLEL);

    f = 1/a;
    vector(s,p,v0);
    u = f * (innerProduct(s,h));

    if ((u < 0.0) || (u > 1.0))
        // the intersection is outside of the triangle
	return(OUTSIDE);

    crossProduct(q,s,e1);
    v = f * innerProduct(d,q);

    if ((v < 0.0) || ((u + v) > 1.0))
        // the intersection is outside of the triangle
	return(OUTSIDE);

    // at this stage we can compute t to find out where
    // the intersection point is on the line
    *t = f * innerProduct(e2,q);
    return(INSIDE);
}

static inline double z_correct(double x, double y)
{
    face_ptr fp = (face_ptr) &mp->vertices[mp->n_vertices];
    int i;
    double t = 0.0;
    long long int start;
    double where[3];

    tests++;
    start = rtapi_get_time();

    where[0] = x;
    where[1] = y;
    where[2] = 0;

    for (i = 0; i < mp->n_faces; i++) 
    {
        faces++;
        if (lineIntersectsTriangle(where, z_dir,
                                  mp->vertices[fp[i][0]],
                                  mp->vertices[fp[i][1]],
                                  mp->vertices[fp[i][2]],
                                  &t) == INSIDE) 
        {
            hits++;
            break;
        }
    }
    if ( old_enable ^ *(haldata->enable) )
    {
        old_enable = *(haldata->enable);
        move_count = MOVE_COUNT;
        move_z = t;// - *(haldata->delta_z);
    }

    if ( move_count )
    {
        move_count--;
        hal_float_t dz = move_z * (hal_float_t)(MOVE_COUNT - move_count) / MOVE_COUNT;
        t -= dz;
    }
    total_nsec += (rtapi_get_time() - start);
    *(haldata->delta_z) = t;
    return t;
}

int kinematicsForward(const double *joints,
		      EmcPose * pos,
		      const KINEMATICS_FORWARD_FLAGS * fflags,
		      KINEMATICS_INVERSE_FLAGS * iflags)
{
    set_geometry(*haldata->r, *haldata->l, *haldata->j0off, *haldata->j1off, *haldata->j2off);
    kinematics_forward(joints,pos);
    if ( *(haldata->enable) )
        pos->tran.z -= z_correct( pos->tran.x, pos->tran.y );
     return 0;
}

int kinematicsInverse(const EmcPose * pos,
		      double *joints,
		      const KINEMATICS_INVERSE_FLAGS * iflags,
		      KINEMATICS_FORWARD_FLAGS * fflags)
{
    set_geometry(*haldata->r, *haldata->l, *haldata->j0off, *haldata->j1off, *haldata->j2off);
    kinematics_inverse(pos, joints);
    if ( *(haldata->enable) )
    {
        double dz = z_correct(pos->tran.x, pos->tran.y);
        joints[0] += dz;
        joints[1] += dz;
        joints[2] += dz;
    }
    return 0;
}

/* implemented for these kinematics as giving joints preference */
int kinematicsHome(EmcPose * world,
		   double *joint,
		   KINEMATICS_FORWARD_FLAGS * fflags,
		   KINEMATICS_INVERSE_FLAGS * iflags)
{
    *fflags = 0;
    *iflags = 0;

    return kinematicsForward(joint, world, fflags, iflags);
}

KINEMATICS_TYPE kinematicsType()
{
    return KINEMATICS_BOTH;
}

static vtkins_t vtk = {
    .kinematicsForward = kinematicsForward,
    .kinematicsInverse  = kinematicsInverse,
    // .kinematicsHome = kinematicsHome,
    .kinematicsType = kinematicsType
};

static int comp_id, vtable_id;
static const char *name = "lineardeltaprobekins";

RTAPI_MP_INT(size, "mesh buffer size")
MODULE_LICENSE("GPL");

static void pk_update(void *arg, long l)
{
    int i;
    int level = rtapi_get_msg_level();
    face_ptr fp = (face_ptr) &mp->vertices[mp->n_vertices];

    rtapi_set_msg_level(RTAPI_MSG_DBG);

    if (*haldata->flags & PRINT_STATS) {
	rtapi_print_msg(RTAPI_MSG_DBG, "lineardeltaprobekins nf=%d nv=%d tests=%d faces=%d hits=%d fsize=%d dsize=%d\n",
			mp->n_faces, mp->n_vertices,
			tests, faces, hits,sizeof(float),sizeof(double)); //, total_nsec/tests);
	*haldata->flags &= ~PRINT_STATS;
    }
    if (*haldata->flags & PRINT_MESH) {
	for (i = 0; i < mp->n_vertices; i++)
	    rtapi_print_msg(RTAPI_MSG_DBG, "vertex %d %d: %d %d %d\n", i,
			    (void *) &mp->vertices[i] - (void *) mp,
			    (int) mp->vertices[i][0],
			    (int) mp->vertices[i][1],
			    (int) mp->vertices[i][2]);
	for (i = 0; i < mp->n_faces; i++)
	    rtapi_print_msg(RTAPI_MSG_DBG, "face %d %d : %d %d %d\n", i,
			    (void *) &fp[i] - (void *) mp,
			    fp[i][0],fp[i][1],fp[i][2]);
	*haldata->flags &= ~PRINT_MESH;
    }
    rtapi_set_msg_level(level);
}

int rtapi_app_main(void) 
{
    int retval;
    int res;
    int level = rtapi_get_msg_level();
    
    move_count = 0;

    rtapi_set_msg_level(RTAPI_MSG_DBG);

    comp_id = hal_init("lineardeltaprobekins");
    if (comp_id < 0)
	    return comp_id;

    vtable_id = hal_export_vtable(name, VTVERSION, &vtk, comp_id);
    if (vtable_id < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: ERROR: hal_export_vtable(%s,%d,%p) failed: %d\n",
			    name, name, VTVERSION, &vtk, vtable_id );
	    return -ENOENT;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "lineardeltaprobekins shmsize=%d comp_id=%d\n",
		            size, comp_id );

    shm_id = rtapi_shmem_new(key, comp_id, size  );
    if (shm_id < 0) 
    {
	    rtapi_exit(comp_id);
	    return -1;
    }

    retval = rtapi_shmem_getptr(shm_id, (void **) &mp, NULL);
    if (retval < 0) 
    {
	    rtapi_exit(comp_id);
	    return -1;
    }

    do 
    {
	    haldata = hal_malloc(sizeof(struct haldata));
	    if (!haldata)
	        break;

	    res = hal_pin_bit_new("lineardeltaprobekins.enable-z-correct", HAL_IN, &(haldata->enable), comp_id);
	    if (res < 0) break;
        old_enable = *(haldata->enable);

	    res = hal_pin_float_new("lineardeltaprobekins.delta-z", HAL_OUT, &(haldata->delta_z), comp_id);
	    if (res < 0) break;

	    res = hal_pin_u32_new("lineardeltaprobekins.flags", HAL_IO, &(haldata->flags), comp_id);
	    if (res < 0) break;

        res = hal_pin_float_newf(HAL_IN, &haldata->r, comp_id, "lineardeltaprobekins.R");
	    if (res < 0) break;

        res = hal_pin_float_newf(HAL_IN, &haldata->l, comp_id, "lineardeltaprobekins.L");
	    if (res < 0) break;

        res = hal_pin_float_newf(HAL_IN, &haldata->j0off, comp_id, "lineardeltakins.J0off");
	    if (res < 0) break;
        res = hal_pin_float_newf(HAL_IN, &haldata->j1off, comp_id, "lineardeltakins.J1off");
	    if (res < 0) break;
        res = hal_pin_float_newf(HAL_IN, &haldata->j2off, comp_id, "lineardeltakins.J2off");
	    if (res < 0) break;


        *haldata->r = DELTA_RADIUS;
        *haldata->l = DELTA_DIAGONAL_ROD;
	    *haldata->j0off = JOINT_0_OFFSET;
	    *haldata->j1off = JOINT_1_OFFSET;
	    *haldata->j2off = JOINT_2_OFFSET;
    
        res =  hal_export_funct("pkupdate", pk_update,  NULL, 1, 0, comp_id);
	    if (res < 0) break;

	    hal_ready(comp_id);
	    rtapi_set_msg_level(level);
	    return 0;
    } while (0);

    hal_exit(comp_id);
    return comp_id;
}

void rtapi_app_exit(void)
{
    rtapi_set_msg_level(RTAPI_MSG_DBG);

    rtapi_shmem_delete(shm_id, comp_id);
    hal_exit(comp_id);
}

#endif

