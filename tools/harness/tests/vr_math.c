/* the headset's poses and fields of view in the game's terms (port/linux/game/vr_math.c), an eye's field checked
through the game's own frustum (render_camera_build_frustum) */
#include "harness.h"
#include <math.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
#define _pi 3.14159265f
#define _real_epsilon 0.0001f
#define match_vassert(file, line, condition, message) ((void)0)
#define csprintf(buffer, ...) ""
#define csmemset memset
#define tangent(x) ((real)tan(x))
typedef union { short v[4]; struct { short y0, x0, y1, x1; }; } rectangle2d;
typedef union { real n[4]; struct { real x0, x1, y0, y1; }; } real_rectangle2d;
typedef union { real n[6]; struct { real x0, x1, y0, y1, z0, z1; }; } real_rectangle3d;
typedef struct { real i, j; } real_vector2d;
typedef struct { real_vector3d n; real d; } real_plane3d;
typedef struct { real scale; real_vector3d forward, left, up; real_point3d position; } real_matrix4x3;
static real_point3d const origin3d;
static real_point3d const *const global_origin3d = &origin3d;
static char temporary[256];

static void cross_product3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *out)
{
    real_vector3d c = {a->j * b->k - a->k * b->j, a->k * b->i - a->i * b->k, a->i * b->j - a->j * b->i};
    *out = c;
}
static real dot(real_vector3d const *a, real_vector3d const *b) { return a->i * b->i + a->j * b->j + a->k * b->k; }
static void normalize3d(real_vector3d *v)
{
    real length = sqrtf(dot(v, v));
    v->i /= length; v->j /= length; v->k /= length;
}
static void negate_vector3d(real_vector3d const *v, real_vector3d *out) { out->i = -v->i; out->j = -v->j; out->k = -v->k; }
static void plane3d_negate(real_plane3d const *p, real_plane3d *out)
{
    negate_vector3d(&p->n, &out->n);
    out->d = -p->d;
}
static void plane3d_from_point_and_normal(real_plane3d *plane, real_point3d const *point, real_vector3d const *normal)
{
    plane->n = *normal;
    plane->d = normal->i * point->x + normal->j * point->y + normal->k * point->z;
}
/* (rows forward, left, up: a rotation, so its inverse is its transpose) */
static void matrix4x3_transform_vector(real_matrix4x3 const *m, real_vector3d const *v, real_vector3d *out)
{
    real_vector3d r = {
        m->forward.i * v->i + m->left.i * v->j + m->up.i * v->k,
        m->forward.j * v->i + m->left.j * v->j + m->up.j * v->k,
        m->forward.k * v->i + m->left.k * v->j + m->up.k * v->k};
    *out = r;
}
static void matrix4x3_transform_point(real_matrix4x3 const *m, real_point3d const *p, real_point3d *out)
{
    real_vector3d v = {p->x, p->y, p->z};
    matrix4x3_transform_vector(m, &v, &v);
    out->x = v.i + m->position.x; out->y = v.j + m->position.y; out->z = v.k + m->position.z;
}
static void matrix4x3_transform_plane(real_matrix4x3 const *m, real_plane3d const *p, real_plane3d *out)
{
    real_vector3d n;
    real_vector3d position = {m->position.x, m->position.y, m->position.z};
    matrix4x3_transform_vector(m, &p->n, &n);
    out->n = n;
    out->d = p->d + dot(&n, &position);
}
static void matrix4x3_inverse(real_matrix4x3 const *m, real_matrix4x3 *out)
{
    real_vector3d p = {-m->position.x, -m->position.y, -m->position.z};
    real_matrix4x3 t = *m;
    t.forward.i = m->forward.i; t.forward.j = m->left.i; t.forward.k = m->up.i;
    t.left.i = m->forward.j; t.left.j = m->left.j; t.left.k = m->up.j;
    t.up.i = m->forward.k; t.up.j = m->left.k; t.up.k = m->up.k;
    t.position.x = t.position.y = t.position.z = 0;
    matrix4x3_transform_vector(&t, &p, &p);
    t.position.x = p.i; t.position.y = p.j; t.position.z = p.k;
    *out = t;
}
static boolean valid_real_matrix4x3(real_matrix4x3 const *m) { return m != NULL; }
static real plane3d_distance_to_point(real_plane3d const *plane, real_point3d const *point)
{
    return plane->n.i * point->x + plane->n.j * point->y + plane->n.k * point->z - plane->d;
}
/* (the frustum's own check that its corners lie on its planes: the worst miss) */
static real worst_miss;
static void render_camera_warn_once(int condition, real distance)
{
    if (distance > worst_miss || distance != distance)
        worst_miss = distance;
}

#include "under_test.inc"

static int close_to(real a, real b, real tolerance) { return fabsf(a - b) <= tolerance; }

/* a point in the frustum's view space (x right, y up, -z ahead) through its projection matrix (row vectors), as
normalized device coordinates */
static void project(struct render_frustum const *frustum, real x, real y, real z, real *ndc_x, real *ndc_y)
{
    real const (*m)[4] = (real const (*)[4])frustum->projection_matrix;
    real clip_x = x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0];
    real clip_y = x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1];
    real clip_w = x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3];
    *ndc_x = clip_x / clip_w;
    *ndc_y = clip_y / clip_w;
}

static void camera_of(struct render_camera *camera, short width, short height)
{
    memset(camera, 0, sizeof(*camera));
    camera->forward.i = 1;
    camera->up.k = 1;
    camera->vertical_field_of_view = _pi * 0.5f;
    camera->viewport_bounds.x1 = camera->window_bounds.x1 = width;
    camera->viewport_bounds.y1 = camera->window_bounds.y1 = height;
    camera->z_near = 0.0625f;
    camera->z_far = 1024;
}

static void check_edges(struct vr_fov const *fov, short width, short height)
{
    struct render_camera camera;
    struct render_frustum frustum;
    real_rectangle2d bounds;
    real x, y;

    camera_of(&camera, width, height);
    vr_fov_bounds(fov, (float)width / (float)height, bounds.n);
    worst_miss = 0;
    render_camera_build_frustum(&camera, &bounds, &frustum, TRUE);
    CHECK(frustum.projection_valid, "a projection");
    CHECK(worst_miss < 1e-3f, "the frustum's corners on its planes (off by %f)", (double)worst_miss);
    project(&frustum, tanf(fov->left) * 10, 0, -10, &x, &y);
    CHECK(close_to(x, -1, 1e-4f), "the left edge at -1, not %f", (double)x);
    project(&frustum, tanf(fov->right) * 10, 0, -10, &x, &y);
    CHECK(close_to(x, 1, 1e-4f), "the right edge at 1, not %f", (double)x);
    project(&frustum, 0, tanf(fov->up) * 10, -10, &x, &y);
    CHECK(close_to(y, 1, 1e-4f), "the top edge at 1, not %f", (double)y);
    project(&frustum, 0, tanf(fov->down) * 10, -10, &x, &y);
    CHECK(close_to(y, -1, 1e-4f), "the bottom edge at -1, not %f", (double)y);
    project(&frustum, 0, 0, -10, &x, &y);
    CHECK(close_to(x, (tanf(-fov->left) - tanf(fov->right)) / (tanf(fov->right) - tanf(fov->left)), 1e-4f),
        "straight ahead sits off-centre as the eye's field does, at %f", (double)x);
    /* the frustum's left plane passes through the left edge's direction */
    {
        real_vector3d edge = {1, -tanf(fov->left), 0};
        real_vector3d const *n = &frustum.world_planes[_render_frustum_plane_left].n;
        CHECK(close_to(n->i * edge.i + n->j * edge.j + n->k * edge.k, 0, 1e-4f), "the left plane holds the left edge");
    }
}

static int same3(float const *a, float x, float y, float z)
{
    return close_to(a[0], x, 1e-5f) && close_to(a[1], y, 1e-5f) && close_to(a[2], z, 1e-5f);
}

int main(int argc, char **argv)
{
    char const *case_name = argc > 1 ? argv[1] : "";
    CASE("bounds") {
        /* a Rift CV1's left eye (roughly), at its recommended size, and a symmetric field */
        struct vr_fov rift = {-0.96f, 0.82f, 0.92f, -0.96f};
        struct vr_fov square = {-0.7853982f, 0.7853982f, 0.7853982f, -0.7853982f};
        real_rectangle2d bounds;
        check_edges(&rift, 1332, 1586);
        check_edges(&rift, 640, 480);
        check_edges(&square, 1000, 1000);
        vr_fov_bounds(&square, 1, bounds.n);
        CHECK(close_to(bounds.x0, -1, 1e-6f) && close_to(bounds.x1, 1, 1e-6f) && close_to(bounds.y0, -1, 1e-6f) &&
            close_to(bounds.y1, 1, 1e-6f), "a square 90-degree field is the game's own bounds");
    }
    CASE("pose") {
        static float const zero[3];
        struct vr_pose pose = {{0, 0, 0}, {0, 0, 0, 1}};
        float position[3], forward[3], up[3], anchor[3] = {10, 20, 30};
        float const half = 0.70710678f;
        vr_pose_view(&pose, zero, anchor, 0, position, forward, up);
        CHECK(same3(position, 10, 20, 30), "at the anchor");
        CHECK(same3(forward, 1, 0, 0) && same3(up, 0, 0, 1), "looking along the world's +x, upright");
        vr_pose_view(&pose, zero, anchor, _pi * 0.5f, position, forward, up);
        CHECK(same3(forward, 0, 1, 0), "a heading a quarter turn left looks along +y");
        /* the head turned a quarter turn left (about LOCAL +y) */
        pose.orientation[1] = half; pose.orientation[3] = half;
        vr_pose_view(&pose, zero, anchor, 0, position, forward, up);
        CHECK(same3(forward, 0, 1, 0) && same3(up, 0, 0, 1), "the head turned left looks along +y");
        /* pitched up a quarter turn (about LOCAL +x) */
        pose.orientation[0] = half; pose.orientation[1] = 0;
        vr_pose_view(&pose, zero, anchor, 0, position, forward, up);
        CHECK(same3(forward, 0, 0, 1) && same3(up, -1, 0, 0), "the head pitched up looks along +z");
        /* rolled 30 degrees to the left (about LOCAL +z, which points back) */
        pose.orientation[0] = 0; pose.orientation[2] = 0.25881905f; pose.orientation[3] = 0.96592583f;
        vr_pose_view(&pose, zero, anchor, 0, position, forward, up);
        CHECK(same3(forward, 1, 0, 0) && same3(up, 0, 0.5f, 0.8660254f), "the roll tilts up toward the left");
        /* offsets: right, up and back in metres; the left eye of a 64 mm IPD */
        pose.orientation[2] = 0; pose.orientation[3] = 1;
        pose.position[0] = 0.1f; pose.position[1] = 0.2f; pose.position[2] = 0.3f;
        vr_pose_view(&pose, zero, anchor, 0, position, forward, up);
        CHECK(same3(position, 10 - 0.3f * VR_UNITS_PER_METRE, 20 - 0.1f * VR_UNITS_PER_METRE,
            30 + 0.2f * VR_UNITS_PER_METRE), "right is -y, up +z, back -x, in world units");
        {
            float origin[3] = {0.1f, 0.2f, 0.3f};
            pose.position[0] = 0.1f - 0.032f;
            vr_pose_view(&pose, origin, anchor, _pi, position, forward, up);
            CHECK(same3(position, 10, 20 - 0.032f * VR_UNITS_PER_METRE, 30),
                "from the origin, the left eye facing -x is on the world's -y side");
            CHECK(same3(forward, -1, 0, 0), "facing -x");
        }
        CHECK(close_to(VR_UNITS_PER_METRE * 3.048f, 1, 1e-6f), "a unit is 10 feet");
    }
    CASE("helpers") {
        float a[4] = {0.1f, 0.3f, -0.2f, 0.9273618f}, b[4] = {-0.3f, 0.2f, 0.4f, 0.8426150f}, ab[4];
        float local[3] = {0.3f, -0.2f, -1.5f}, world[3], back[3];
        vr_local_to_world(local, 0.7f, world);
        vr_world_to_local(world, 0.7f, back);
        CHECK(same3(back, local[0], local[1], local[2]), "the world's axes back to LOCAL's (%f %f %f)", (double)back[0],
            (double)back[1], (double)back[2]);
        vr_world_to_local((float[3]){0, 1, 0}, 1.5707963f, back);
        CHECK(same3(back, 0, 0, -1), "a quarter turn left, the world's +y is ahead");
        float v[3] = {0.3f, -0.5f, 0.8f}, step[3], once[3], twice[3];
        float offset[3] = {3, 0, 4}, small[3] = {0.1f, 0, 0};
        float wide[4], left[4] = {-1, 0.5f, -0.8f, 0.9f}, right[4] = {-0.6f, 1.1f, -0.9f, 0.7f};
        float direction[3] = {0, 0, 1}, bad[3] = {0, 1, 0};
        vr_quaternion_multiply(a, b, ab);
        vr_rotate(b, v, step); vr_rotate(a, step, once);
        vr_rotate(ab, v, twice);
        CHECK(same3(twice, once[0], once[1], once[2]), "a*b turns by b, then a");
        CHECK(close_to(vr_wrap_angle(3 * _pi), -_pi, 1e-5f) || close_to(vr_wrap_angle(3 * _pi), _pi, 1e-5f), "3 pi wraps");
        CHECK(close_to(vr_wrap_angle(-0.5f), -0.5f, 1e-6f), "small angles keep");
        CHECK(close_to(vr_wrap_angle(7.0f), 7.0f - 2 * _pi, 1e-5f), "7 wraps");
        CHECK(vr_wrap_angle(NAN) == 0 && vr_wrap_angle(INFINITY) == 0, "not a number is 0");
        CHECK(close_to(vr_yaw_of(direction, 0.25f), 0.25f, 0), "straight up keeps the yaw given");
        CHECK(close_to(vr_yaw_of(bad, 0), _pi * 0.5f, 1e-6f), "+y is a quarter turn");
        CHECK(vr_limit_reach(offset, 0.35f) && close_to(offset[0], 0.21f, 1e-6f) && close_to(offset[2], 0.28f, 1e-6f),
            "held to the reach, its direction kept");
        CHECK(!vr_limit_reach(small, 0.35f) && small[0] == 0.1f, "within the reach is kept");
        vr_bounds_union(left, right, 0.02f, wide);
        CHECK(close_to(wide[0], -1.02f, 1e-6f) && close_to(wide[1], 1.12f, 1e-6f) && close_to(wide[2], -0.92f, 1e-6f) &&
            close_to(wide[3], 0.92f, 1e-6f), "both eyes' fields and a margin");
        bad[2] = INFINITY;
        CHECK(vr_finite(v, 3) && !vr_finite(bad, 3), "infinity is not finite");
        bad[2] = NAN;
        CHECK(!vr_finite(bad, 3), "nor is not a number");
    }
    CASE("between") {
        float from[3] = {0.36f, 0.48f, -0.8f}, to[3] = {0, 0, -1}, turn[4], turned[3];
        float pairs[][2][3] = {
            {{0.36f, 0.48f, -0.8f}, {0, 0, -1}},
            {{0, 0, -1}, {0.6f, 0, -0.8f}},
            {{1, 0, 0}, {0, 1, 0}},
            {{0, 0, -1}, {0, 0, 1}},
            {{1, 0, 0}, {-1, 0, 0}},
            {{0.6f, 0.8f, 0}, {-0.6f, -0.8f, 0}},
        };
        size_t index;
        for (index = 0; index < sizeof(pairs) / sizeof(pairs[0]); index++) {
            vr_quaternion_between(pairs[index][0], pairs[index][1], turn);
            vr_rotate(turn, pairs[index][0], turned);
            CHECK(same3(turned, pairs[index][1][0], pairs[index][1][1], pairs[index][1][2]),
                "pair %d: turned onto the other (%f %f %f)", (int)index, (double)turned[0], (double)turned[1],
                (double)turned[2]);
            CHECK(close_to(turn[0] * turn[0] + turn[1] * turn[1] + turn[2] * turn[2] + turn[3] * turn[3], 1, 1e-5f),
                "pair %d: of length 1", (int)index);
        }
        /* the shortest turn: about the axis across both, so a direction
        across them both is kept */
        vr_quaternion_between(from, to, turn);
        {
            float across[3] = {from[1] * to[2] - from[2] * to[1], from[2] * to[0] - from[0] * to[2],
                from[0] * to[1] - from[1] * to[0]}, kept[3];
            vr_rotate(turn, across, kept);
            CHECK(same3(kept, across[0], across[1], across[2]), "the axis across both is kept");
        }
        vr_quaternion_between(to, to, turn);
        CHECK(close_to(turn[3], 1, 1e-6f), "no turn from a direction to itself");
    }
    return 0;
}
