#include "nmd_common.h"

bool _nmd_reserve(size_t numNewVertices, size_t numNewIndices)
{
    /* Check vertices */
    size_t futureSize = (_nmd_context.drawList.numVertices + numNewVertices) * sizeof(nmd_vertex);
    if (futureSize > _nmd_context.drawList.verticesCapacity)
    {
        const size_t newCapacity = NMD_MAX(_nmd_context.drawList.verticesCapacity * 2, futureSize);
        void* mem = NMD_ALLOC(newCapacity);
        if (!mem)
            return false;
        memcpy(mem, _nmd_context.drawList.vertices, _nmd_context.drawList.verticesCapacity);
        NMD_FREE(_nmd_context.drawList.vertices);

        _nmd_context.drawList.vertices = (nmd_vertex*)mem;
        _nmd_context.drawList.verticesCapacity = newCapacity;
    }

    /* Check indices */
    futureSize = (_nmd_context.drawList.numIndices + numNewIndices) * sizeof(nmd_index);
    if (futureSize > _nmd_context.drawList.indicesCapacity)
    {
        const size_t newCapacity = NMD_MAX(_nmd_context.drawList.indicesCapacity * 2, futureSize);
        void* mem = NMD_ALLOC(newCapacity);
        if (!mem)
            return false;
        memcpy(mem, _nmd_context.drawList.indices, _nmd_context.drawList.indicesCapacity);
        NMD_FREE(_nmd_context.drawList.indices);

        mem, _nmd_context.drawList.indices = (nmd_index*)mem;
        _nmd_context.drawList.indicesCapacity = newCapacity;
    }

    return true;
}

bool _nmd_reserve_points(size_t numNewPoints)
{
    const size_t futureSize = (_nmd_context.drawList.numPoints + numNewPoints) * sizeof(nmd_vec2);
    if (futureSize > _nmd_context.drawList.pathCapacity)
    {
        const size_t newCapacity = NMD_MAX(_nmd_context.drawList.pathCapacity * 2, futureSize);
        void* mem = NMD_ALLOC(newCapacity);
        if (!mem)
            return false;
        memcpy(mem, _nmd_context.drawList.path, _nmd_context.drawList.pathCapacity);
        NMD_FREE(_nmd_context.drawList.path);

        _nmd_context.drawList.path = (nmd_vec2*)mem;
        _nmd_context.drawList.pathCapacity = newCapacity;
    }

    return true;
}

#define NMD_NORMALIZE2F_OVER_ZERO(VX,VY) { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float inv_len = 1.0f / NMD_SQRT(d2); VX *= inv_len; VY *= inv_len; } }
#define NMD_FIXNORMAL2F(VX,VY) { float d2 = VX*VX + VY*VY; if (d2 < 0.5f) d2 = 0.5f; float inv_lensq = 1.0f / d2; VX *= inv_lensq; VY *= inv_lensq; }

void nmd_add_polyline(const nmd_vec2* points, size_t numPoints, nmd_color color, bool closed, float thickness)
{
    const size_t numSegments = closed ? numPoints : numPoints - 1;

    const bool thick_line = thickness > 1.0f;
    nmd_color col_trans;
    if (numPoints < 2)
        return;

    col_trans = color;
    col_trans.a = 0;

    if (_nmd_context.drawList.lineAntiAliasing)
    {
        const float AA_SIZE = 1.0f;

        size_t i1 = 0;

        const size_t idx_count = thick_line ? (numSegments * 18) : (numSegments * 12);
        const size_t vtx_count = thick_line ? (numPoints * 4) : (numPoints * 3);
        if (!_nmd_reserve(vtx_count, idx_count))
            return;

        size_t size;
        nmd_vec2* normals, * temp;
        normals = (nmd_vec2*)NMD_ALLOCA(sizeof(nmd_vec2) * ((thick_line) ? 5 : 3) * numPoints);

        temp = normals + numPoints;

        /* Calculate normals */
        for (i1 = 0; i1 < numSegments; ++i1) {
            const int i2 = (i1 + 1) == numPoints ? 0 : i1 + 1;
            float dx = points[i2].x - points[i1].x;
            float dy = points[i2].y - points[i1].y;
            NMD_NORMALIZE2F_OVER_ZERO(dx, dy);
            normals[i1].x = dy;
            normals[i1].y = -dx;
        }

        if (!closed)
            normals[numPoints - 1] = normals[numPoints - 2];

        if (!thick_line) {
            size_t idx1, i;
            if (!closed) {
                nmd_vec2 d;

                temp[0].x = points[0].x + normals[0].x * AA_SIZE;
                temp[0].y = points[0].y + normals[0].y * AA_SIZE;

                temp[1].x = points[0].x - normals[0].x * AA_SIZE;
                temp[1].y = points[0].y - normals[0].y * AA_SIZE;

                d.x = normals[numPoints - 1].x * AA_SIZE;
                d.y = normals[numPoints - 1].y * AA_SIZE;

                temp[(numPoints - 1) * 2 + 0].x = points[numPoints - 1].x + d.x;
                temp[(numPoints - 1) * 2 + 0].y = points[numPoints - 1].y + d.y;

                temp[(numPoints - 1) * 2 + 1].x = points[numPoints - 1].x - d.x;
                temp[(numPoints - 1) * 2 + 1].y = points[numPoints - 1].y - d.y;
            }

            /* Fill elements */
            idx1 = _nmd_context.drawList.numVertices;
            for (i1 = 0; i1 < numSegments; i1++) {
                nmd_vec2 dm;
                float dmr2;
                size_t i2 = ((i1 + 1) == numPoints) ? 0 : (i1 + 1);
                size_t idx2 = ((i1 + 1) == numPoints) ? _nmd_context.drawList.numVertices : (idx1 + 3);

                /* Average normals */
                dm.x = (normals[i1].x + normals[i2].x) * 0.5f;
                dm.y = (normals[i1].y + normals[i2].y) * 0.5f;

                dmr2 = dm.x * dm.x + dm.y * dm.y;
                if (dmr2 > 0.000001f) {
                    float scale = 1.0f / dmr2;

                    scale = NMD_MIN(100.0f, scale);

                    dm.x = dm.x * scale;
                    dm.y = dm.y * scale;
                }

                dm.x = dm.x * AA_SIZE;
                dm.y = dm.y * AA_SIZE;

                temp[i2 * 2 + 0].x = points[i2].x + dm.x;
                temp[i2 * 2 + 0].y = points[i2].y + dm.y;

                temp[i2 * 2 + 1].x = points[i2].x - dm.x;
                temp[i2 * 2 + 1].y = points[i2].y - dm.y;

                nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
                indices[0]  = idx2 + 0; indices[1]  = idx1 + 0;
                indices[2]  = idx1 + 2; indices[3]  = idx1 + 2;
                indices[4]  = idx2 + 2; indices[5]  = idx2 + 0;
                indices[6]  = idx2 + 1; indices[7]  = idx1 + 1;
                indices[8]  = idx1 + 0; indices[9]  = idx1 + 0;
                indices[10] = idx2 + 0; indices[11] = idx2 + 1;
                _nmd_context.drawList.numIndices += 12;

                idx1 = idx2;
            }

            /* Fill vertices */
            for (i = 0; i < numPoints; ++i) {
                nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
                vertices[0].pos = points[i];       vertices[0].color = color;
                vertices[1].pos = temp[i * 2 + 0]; vertices[1].color = col_trans;
                vertices[2].pos = temp[i * 2 + 1]; vertices[2].color = col_trans;
                _nmd_context.drawList.numVertices += 3;
            }
        }
        else {
            size_t idx1, i;
            const float half_inner_thickness = (thickness - AA_SIZE) * 0.5f;
            if (!closed) {
                nmd_vec2 d1;
                d1.x = normals[0].x * (half_inner_thickness + AA_SIZE);
                d1.y = normals[0].y * (half_inner_thickness + AA_SIZE);

                nmd_vec2 d2;
                d2.x = normals[0].x * half_inner_thickness;
                d2.y = normals[0].y * half_inner_thickness;
        
                temp[0].x = points[0].x + d1.x;
                temp[0].y = points[0].y + d1.y;

                temp[1].x = points[0].x + d2.x;
                temp[1].y = points[0].y + d2.y;

                temp[2].x = points[0].x - d2.x;
                temp[2].y = points[0].y - d2.y;

                temp[3].x = points[0].x - d1.x;
                temp[3].y = points[0].y - d1.y;
        
                d1.x = normals[numPoints - 1].x * (half_inner_thickness + AA_SIZE);
                d1.y = normals[numPoints - 1].y * (half_inner_thickness + AA_SIZE);

                d2.x = normals[numPoints - 1].x * half_inner_thickness;
                d2.y = normals[numPoints - 1].y * half_inner_thickness;
        
                temp[(numPoints - 1) * 4 + 0].x = points[numPoints - 1].x + d1.x;
                temp[(numPoints - 1) * 4 + 0].y = points[numPoints - 1].y + d1.y;

                temp[(numPoints - 1) * 4 + 1].x = points[numPoints - 1].x + d2.x;
                temp[(numPoints - 1) * 4 + 1].y = points[numPoints - 1].y + d2.y;

                temp[(numPoints - 1) * 4 + 2].x = points[numPoints - 1].x - d2.x;
                temp[(numPoints - 1) * 4 + 2].y = points[numPoints - 1].y - d2.y;

                temp[(numPoints - 1) * 4 + 3].x = points[numPoints - 1].x - d1.x;
                temp[(numPoints - 1) * 4 + 3].y = points[numPoints - 1].y - d1.y;
            }
        
            /* Add all elements */
            idx1 = _nmd_context.drawList.numVertices;
            for (i1 = 0; i1 < numSegments; ++i1) {
                nmd_vec2 dm_out, dm_in;
                const size_t i2 = ((i1 + 1) == numPoints) ? 0 : (i1 + 1);
                size_t idx2 = ((i1 + 1) == numPoints) ? _nmd_context.drawList.numVertices : (idx1 + 4);
        
                /* Average normals */
                nmd_vec2 dm;
                dm.x = (normals[i1].x + normals[i2].x) * 0.5f;
                dm.y = (normals[i1].y + normals[i2].y) * 0.5f;

                float dmr2 = dm.x * dm.x + dm.y * dm.y;
                if (dmr2 > 0.000001f) {
                    float scale = 1.0f / dmr2;
                    scale = NMD_MIN(100.0f, scale);
                    dm.x = dm.x * scale;
                    dm.y = dm.y * scale;
                }
        
                dm_out.x = dm.x * ((half_inner_thickness)+AA_SIZE);
                dm_out.y = dm.y * ((half_inner_thickness)+AA_SIZE);

                dm_in.x = dm.x * half_inner_thickness;
                dm_in.y = dm.y * half_inner_thickness;


                temp[i2 * 4 + 0].x = points[i2].x + dm_out.x;
                temp[i2 * 4 + 0].y = points[i2].y + dm_out.y;

                temp[i2 * 4 + 1].x = points[i2].x + dm_in.x;
                temp[i2 * 4 + 1].y = points[i2].y + dm_in.y;

                temp[i2 * 4 + 2].x = points[i2].x - dm_in.x;
                temp[i2 * 4 + 2].y = points[i2].y - dm_in.y;

                temp[i2 * 4 + 3].x = points[i2].x - dm_out.x;
                temp[i2 * 4 + 3].y = points[i2].y - dm_out.y;
        
                nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
                indices[0]  = idx2 + 1; indices[1]  = idx1 + 1;
                indices[2]  = idx1 + 2; indices[3]  = idx1 + 2;
                indices[4]  = idx2 + 2; indices[5]  = idx2 + 1;
                indices[6]  = idx2 + 1; indices[7]  = idx1 + 1;
                indices[8]  = idx1 + 0; indices[9]  = idx1 + 0;
                indices[10] = idx2 + 0; indices[11] = idx2 + 1;
                indices[12] = idx2 + 2; indices[13] = idx1 + 2;
                indices[14] = idx1 + 3; indices[15] = idx1 + 3;
                indices[16] = idx2 + 3; indices[17] = idx2 + 2;
                _nmd_context.drawList.numIndices += 18;

                idx1 = idx2;                
            }
        
            /* Add vertices */
            for (i = 0; i < numPoints; i++) {
                nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
                vertices[0].pos = temp[i * 4 + 0]; vertices[0].color = col_trans;
                vertices[1].pos = temp[i * 4 + 1]; vertices[1].color = color;
                vertices[2].pos = temp[i * 4 + 2]; vertices[2].color = color;
                vertices[3].pos = temp[i * 4 + 3]; vertices[3].color = col_trans;
                _nmd_context.drawList.numVertices += 4;
            }
        }
    }
    else /* Non anti-alised */
    {
        const size_t numIndices = numSegments * 6;
        const size_t numVertices = numSegments * 4;
        
        if (!_nmd_reserve(numVertices, numIndices))
            return;

        for (size_t i = 0; i < numSegments; i++)
        {
            const size_t j = (i + 1) == numPoints ? 0 : i + 1;
            const nmd_vec2* p0 = &points[i];
            const nmd_vec2* p1 = &points[j];

            float dx = p1->x - p0->x;
            float dy = p1->y - p0->y;

            NMD_NORMALIZE2F_OVER_ZERO(dx, dy);
            dx *= (thickness * 0.5f);
            dy *= (thickness * 0.5f);

            const size_t offset = _nmd_context.drawList.numVertices;

            nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
            indices[0] = offset + 0; indices[1] = offset + 1; indices[2] = offset + 2;
            indices[3] = offset + 0; indices[4] = offset + 2; indices[5] = offset + 3;
            _nmd_context.drawList.numIndices += 6;

            nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
            vertices[0].pos.x = p0->x + dy; vertices[0].pos.y = p0->y - dx; vertices[0].color = color;
            vertices[1].pos.x = p1->x + dy; vertices[1].pos.y = p1->y - dx; vertices[1].color = color;
            vertices[2].pos.x = p1->x - dy; vertices[2].pos.y = p1->y + dx; vertices[2].color = color;
            vertices[3].pos.x = p0->x - dy; vertices[3].pos.y = p0->y + dx; vertices[3].color = color;
            _nmd_context.drawList.numVertices += 4;
        }
    }
}

void nmd_path_to(float x0, float y0)
{
    if (!_nmd_reserve_points(1))
        return;

    _nmd_context.drawList.path[_nmd_context.drawList.numPoints].x = x0;
    _nmd_context.drawList.path[_nmd_context.drawList.numPoints].y = y0;
    _nmd_context.drawList.numPoints++;
}

void nmd_path_fill_convex(nmd_color color)
{
    nmd_add_convex_polygon_filled(_nmd_context.drawList.path, _nmd_context.drawList.numPoints, color);

    /* Clear points in 'path' */
    _nmd_context.drawList.numPoints = 0;
}

void nmd_path_stroke(nmd_color color, bool closed, float thickness)
{
    nmd_add_polyline(_nmd_context.drawList.path, _nmd_context.drawList.numPoints, color, closed, thickness);

    /* Clear points in 'path' */
    _nmd_context.drawList.numPoints = 0;
}

/*
void PathBezierToCasteljau(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, size_t level)
{
    const float dx = x4 - x1;
    const float dy = y4 - y1;
    float d2 = ((x2 - x4) * dy - (y2 - y4) * dx);
    float d3 = ((x3 - x4) * dy - (y3 - y4) * dx);
    d2 = (d2 >= 0) ? d2 : -d2;
    d3 = (d3 >= 0) ? d3 : -d3;
    if ((d2 + d3) * (d2 + d3) < GetContext().drawList.curveTessellationTolerance * (dx * dx + dy * dy))
        GetContext().drawList.path.emplace_back(x4, y4);
    else if (level < 10)
    {
        const float x12 = (x1 + x2) * 0.5f, y12 = (y1 + y2) * 0.5f;
        const float x23 = (x2 + x3) * 0.5f, y23 = (y2 + y3) * 0.5f;
        const float x34 = (x3 + x4) * 0.5f, y34 = (y3 + y4) * 0.5f;
        const float x123 = (x12 + x23) * 0.5f, y123 = (y12 + y23) * 0.5f;
        const float x234 = (x23 + x34) * 0.5f, y234 = (y23 + y34) * 0.5f;
        const float x1234 = (x123 + x234) * 0.5f, y1234 = (y123 + y234) * 0.5f;
        PathBezierToCasteljau(x1, y1, x12, y12, x123, y123, x1234, y1234, level + 1);
        PathBezierToCasteljau(x1234, y1234, x234, y234, x34, y34, x4, y4, level + 1);
    }
}

nmd_vec2 BezierCalc(nmd_vec2 p0, nmd_vec2 p1, nmd_vec2 p2, nmd_vec2 p3, float t)
{
    const float u = 1.0f - t;
    const float w1 = u * u * u;
    const float w2 = 3 * u * u * t;
    const float w3 = 3 * u * t * t;
    const float w4 = t * t * t;
    return { w1 * p0.x + w2 * p1.x + w3 * p2.x + w4 * p3.x, w1 * p0.y + w2 * p1.y + w3 * p2.y + w4 * p3.y };
}
*/

/* Distribute UV over (a, b) rectangle */
void _nmd_shade_verts_linear_uv(int vert_start_idx, int vert_end_idx, float x0, float y0, float x1, float y1, float uv_x0, float uv_y0, float uv_x1, float uv_y1, bool clamp)
{
    const float size_x = x1 - x0;
    const float size_y = y1 - y0;

    const float uv_size_x = uv_x1 - uv_x0;
    const float uv_size_y = uv_y1 - uv_y0;

    const float scale_x = size_x != 0.0f ? (uv_size_x / size_x) : 0.0f;
    const float scale_y = size_y != 0.0f ? (uv_size_y / size_y) : 0.0f;

    nmd_vertex* vert_start = _nmd_context.drawList.vertices + vert_start_idx;
    nmd_vertex* vert_end = _nmd_context.drawList.vertices + vert_end_idx;
    if (clamp)
    {
        const float min_x = NMD_MIN(uv_x0, uv_x1);
        const float min_y = NMD_MIN(uv_y0, uv_y1);

        const float max_x = NMD_MAX(uv_x0, uv_x1);
        const float max_y = NMD_MAX(uv_y1, uv_y1);
        for (nmd_vertex* vertex = vert_start; vertex < vert_end; ++vertex)
        {
            vertex->uv.x = NMD_CLAMP(uv_x0 + (vertex->pos.x - x0) * scale_x, min_x, max_x);
            vertex->uv.y = NMD_CLAMP(uv_y0 + (vertex->pos.y - y0) * scale_y, min_x, max_x);
        }
    }
    else
    {
        for (nmd_vertex* vertex = vert_start; vertex < vert_end; ++vertex)
        {
            vertex->uv.x = uv_x0 + (vertex->pos.x - x0) * scale_x;
            vertex->uv.y = uv_y0 + (vertex->pos.y - y0) * scale_y;
        }
    }
}

void nmd_add_rect(float x0, float y0, float x1, float y1, nmd_color color, float rounding, uint32_t cornerFlags, float thickness)
{
    if (!color.a || thickness == 0.0f)
        return;
    
    nmd_path_rect(x0, y0, x1, y1, rounding, cornerFlags);

    nmd_path_stroke(color, true, thickness);
}

void nmd_add_rect_filled(float x0, float y0, float x1, float y1, nmd_color color, float rounding, uint32_t cornerFlags)
{
    if (!color.a)
        return;

    if (rounding > 0.0f)
    {
        nmd_path_rect(x0, y0, x1, y1, rounding, cornerFlags);
        nmd_path_fill_convex(color);
    }
    else
    {
        if (!_nmd_reserve(4, 6))
            return;

        const size_t offset = _nmd_context.drawList.numVertices;

        nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
        indices[0] = offset + 0; indices[1] = offset + 1; indices[2] = offset + 2;
        indices[3] = offset + 0; indices[4] = offset + 2; indices[5] = offset + 3;
        _nmd_context.drawList.numIndices += 6;

        nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
        vertices[0].pos.x = x0; vertices[0].pos.y = y0; vertices[0].color = color;
        vertices[1].pos.x = x1; vertices[1].pos.y = y0; vertices[1].color = color;
        vertices[2].pos.x = x1; vertices[2].pos.y = y1; vertices[2].color = color;
        vertices[3].pos.x = x0; vertices[3].pos.y = y1; vertices[3].color = color;
        _nmd_context.drawList.numVertices += 4;
    }
}

void nmd_add_rect_filled_multi_color(float x0, float y0, float x1, float y1, nmd_color colorUpperLeft, nmd_color colorUpperRight, nmd_color colorBottomRight, nmd_color colorBottomLeft)
{
    if (!_nmd_reserve(4, 6))
        return;

    const size_t offset = _nmd_context.drawList.numVertices;

    nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
    indices[0] = offset + 0; indices[1] = offset + 1; indices[2] = offset + 2;
    indices[3] = offset + 0; indices[4] = offset + 2; indices[5] = offset + 3;
    _nmd_context.drawList.numIndices += 6;

    nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
    vertices[0].pos.x = x0; vertices[0].pos.y = y0; vertices[0].color = colorUpperLeft;
    vertices[1].pos.x = x1; vertices[1].pos.y = y0; vertices[1].color = colorUpperRight;
    vertices[2].pos.x = x1; vertices[2].pos.y = y1; vertices[2].color = colorBottomRight;
    vertices[3].pos.x = x0; vertices[3].pos.y = y1; vertices[3].color = colorBottomLeft;
    _nmd_context.drawList.numVertices += 4;
}

void nmd_add_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, nmd_color color, float thickness)
{
    if (!color.a)
        return;

    nmd_path_to(x0, y0);
    nmd_path_to(x1, y1);
    nmd_path_to(x2, y2);
    nmd_path_to(x3, y3);

    nmd_path_stroke(color, true, thickness);
}

void nmd_add_quad_filled(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, nmd_color color)
{
    if (!color.a)
        return;

    nmd_path_to(x0, y0);
    nmd_path_to(x1, y1);
    nmd_path_to(x2, y2);
    nmd_path_to(x3, y3);

    nmd_path_fill_convex(color);
}

void nmd_add_triangle(float x0, float y0, float x1, float y1, float x2, float y2, nmd_color color, float thickness)
{
    if (!color.a)
        return;
    
    nmd_path_to(x0, y0);
    nmd_path_to(x1, y1);
    nmd_path_to(x2, y2);

    nmd_path_stroke(color, true, thickness);
}

void nmd_add_triangle_filled(float x0, float y0, float x1, float y1, float x2, float y2, nmd_color color)
{
    if (!color.a || !_nmd_reserve(3, 3))
        return;

    const size_t offset = _nmd_context.drawList.numVertices;

    nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
    indices[0] = offset + 0;
    indices[1] = offset + 1;
    indices[2] = offset + 2;
    _nmd_context.drawList.numIndices += 6;

    nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
    vertices[0].pos.x = x0; vertices[0].pos.y = y0; vertices[0].color = color;
    vertices[1].pos.x = x1; vertices[1].pos.y = y1; vertices[1].color = color;
    vertices[2].pos.x = x2; vertices[2].pos.y = y2; vertices[2].color = color;
    _nmd_context.drawList.numVertices += 4;
}

void nmd_add_circle(float x0, float y0, float radius, nmd_color color, size_t numSegments, float thickness)
{
    if (!color.a || radius <= 0.0f)
        return;

    if (numSegments == 0)
        numSegments = (radius - 1 < 64) ? _nmd_context.drawList.cachedCircleSegmentCounts64[(int)radius - 1] : NMD_CIRCLE_AUTO_SEGMENT_CALC(radius, 1.6f);
    else
        numSegments = NMD_CLAMP(numSegments, 3, NMD_CIRCLE_AUTO_SEGMENT_MAX);

    if (numSegments == 12)
        nmd_path_arc_to_cached(x0, y0, radius - 0.5f, 0, 12, false);
    else
        nmd_path_arc_to(x0, y0, radius - 0.5f, 0.0f, NMD_2PI * ((numSegments - 1) / (float)numSegments), numSegments - 1, false);

    nmd_path_stroke(color, true, thickness);
}

void nmd_add_circle_filled(float x0, float y0, float radius, nmd_color color, size_t numSegments)
{
    if (!color.a || radius <= 0.0f)
        return;

    if (numSegments <= 0)
        numSegments = (radius - 1 < 64) ? _nmd_context.drawList.cachedCircleSegmentCounts64[(int)radius - 1] : NMD_CIRCLE_AUTO_SEGMENT_CALC(radius, 1.6f);
    else
        numSegments = NMD_CLAMP(numSegments, 3, NMD_CIRCLE_AUTO_SEGMENT_MAX);

    if (numSegments == 12)
        nmd_path_arc_to_cached(x0, y0, radius, 0, 12, false);
    else
        nmd_path_arc_to(x0, y0, radius, 0.0f, NMD_2PI * ((numSegments - 1.0f) / (float)numSegments), numSegments - 1, false);
        
    nmd_path_fill_convex(color);
}

void nmd_add_ngon(float x0, float y0, float radius, nmd_color color, size_t numSegments, float thickness)
{
    if (!color.a || numSegments < 3)
        return;

    /* remove one(1) from numSegment because it's a closed shape. */
    nmd_path_arc_to(x0, y0, radius, 0.0f, NMD_2PI * ((numSegments - 1) / (float)numSegments), numSegments - 1, false);
    nmd_path_stroke(color, true, thickness);
}

void nmd_add_ngon_filled(float x0, float y0, float radius, nmd_color color, size_t numSegments)
{
    if (!color.a || numSegments < 3)
        return;

    /* remove one(1) from numSegment because it's a closed shape. */
    nmd_path_arc_to(x0, y0, radius, 0.0f, NMD_2PI * ((numSegments - 1) / (float)numSegments), numSegments - 1, false);
    nmd_path_fill_convex(color);
}

void nmd_path_rect(float x0, float y0, float x1, float y1, float rounding, uint32_t cornerFlags)
{
    if (rounding == 0.0f || cornerFlags == 0)
    {
        nmd_path_to(x0, y0);
        nmd_path_to(x1, y0);
        nmd_path_to(x1, y1);
        nmd_path_to(x0, y1);
    }
    else
    {
        const float roundingTopLeft = (cornerFlags & NMD_CORNER_TOP_LEFT) ? rounding : 0.0f;
        const float roundingTopRight = (cornerFlags & NMD_CORNER_TOP_RIGHT) ? rounding : 0.0f;
        const float roundingBottomRight = (cornerFlags & NMD_CORNER_BOTTOM_RIGHT) ? rounding : 0.0f;
        const float roundingBottomLeft = (cornerFlags & NMD_CORNER_BOTTOM_LEFT) ? rounding : 0.0f;
        nmd_path_arc_to_cached(x0 + roundingTopLeft, y0 + roundingTopLeft, roundingTopLeft, 6, 9, false);
        nmd_path_arc_to_cached(x1 - roundingTopRight, y0 + roundingTopRight, roundingTopRight, 9, 12, false);
        nmd_path_arc_to_cached(x1 - roundingBottomRight, y1 - roundingBottomRight, roundingBottomRight, 0, 3, false);
        nmd_path_arc_to_cached(x0 + roundingBottomLeft, y1 - roundingBottomLeft, roundingBottomLeft, 3, 6, false);
    }
}

void nmd_path_arc_to(float x0, float y0, float radius, float startAngle, float endAngle, size_t numSegments, bool startAtCenter)
{
    if (!_nmd_reserve_points((startAtCenter ? 1 : 0) + numSegments))
        return;

    nmd_vec2* path = _nmd_context.drawList.path + _nmd_context.drawList.numPoints;

    if (startAtCenter)
    {
        path->x = x0, path->y = y0;
        path++;
    }

    for (size_t i = 0; i <= numSegments; i++)
    {
        const float angle = startAngle + (i / (float)numSegments) * (endAngle - startAngle);
        path[i].x = x0 + NMD_COS(angle) * radius;
        path[i].y = y0 + NMD_SIN(angle) * radius;
    }

    _nmd_context.drawList.numPoints = (startAtCenter ? 1 : 0) + numSegments + 1;
}

void nmd_path_arc_to_cached(float x0, float y0, float radius, size_t startAngleOf12, size_t endAngleOf12, bool startAtCenter)
{
    if (!_nmd_reserve_points((startAtCenter ? 1 : 0) + (endAngleOf12 - startAngleOf12)))
        return;

    nmd_vec2* path = _nmd_context.drawList.path + _nmd_context.drawList.numPoints;

    if (startAtCenter)
    {
        path->x = x0, path->y = y0;
        path++;
    }

    for (size_t angle = startAngleOf12; angle <= endAngleOf12; angle++)
    {
        const nmd_vec2* point = &_nmd_context.drawList.cachedCircleVertices12[angle % 12];
        path->x = x0 + point->x * radius;
        path->y = y0 + point->y * radius;
        path++;
    }

    _nmd_context.drawList.numPoints = path - _nmd_context.drawList.path;
}

/*
void DrawList::PathBezierCurveTo(const nmd_vec2& p2, const nmd_vec2& p3, const nmd_vec2& p4, size_t numSegments)
{
    const nmd_vec2& p1 = path.back();
    if (numSegments == 0)
        PathBezierToCasteljau(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p4.x, p4.y, 0);
    else
    {
        const float tStep = 1.0f / static_cast<float>(numSegments);
        for (size_t iStep = 1; iStep <= numSegments; iStep++)
            path.push_back(BezierCalc(p1, p2, p3, p4, tStep * iStep));
    }
}
*/

void nmd_prim_rect_uv(float x0, float y0, float x1, float y1, float uv_x0, float uv_y0, float uv_x1, float uv_y1, nmd_color color)
{
    const size_t offset = _nmd_context.drawList.numVertices;

    nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
    indices[0] = offset + 0; indices[1] = offset + 1; indices[2] = offset + 2;
    indices[3] = offset + 0; indices[4] = offset + 2; indices[5] = offset + 3;
    _nmd_context.drawList.numIndices += 6;

    nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
    vertices[0].pos.x = x0; vertices[0].pos.y = y0; vertices[0].uv.x = uv_x0; vertices[0].uv.y = uv_y0; vertices[0].color = color;
    vertices[1].pos.x = x1; vertices[1].pos.y = y0; vertices[1].uv.x = uv_x1; vertices[1].uv.y = uv_y0; vertices[1].color = color;
    vertices[2].pos.x = x1; vertices[2].pos.y = y1; vertices[2].uv.x = uv_x1; vertices[2].uv.y = uv_y1; vertices[2].color = color;
    vertices[3].pos.x = x0; vertices[3].pos.y = y1; vertices[3].uv.x = uv_x0; vertices[3].uv.y = uv_y1; vertices[3].color = color;
    _nmd_context.drawList.numVertices += 4;
}

void nmd_prim_quad_uv(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float uv_x0, float uv_y0, float uv_x1, float uv_y1, float uv_x2, float uv_y2, float uv_x3, float uv_y3, nmd_color color)
{
    const size_t offset = _nmd_context.drawList.numVertices;

    nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
    indices[0] = offset + 0; indices[1] = offset + 1; indices[2] = offset + 2;
    indices[3] = offset + 0; indices[4] = offset + 2; indices[5] = offset + 3;
    _nmd_context.drawList.numIndices += 6;

    nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
    vertices[0].pos.x = x0; vertices[0].pos.y = y0; vertices[0].uv.x = uv_x0; vertices[0].uv.y = uv_y0; vertices[0].color = color;
    vertices[1].pos.x = x1; vertices[1].pos.y = y1; vertices[1].uv.x = uv_x1; vertices[1].uv.y = uv_y1; vertices[1].color = color;
    vertices[2].pos.x = x2; vertices[2].pos.y = y2; vertices[2].uv.x = uv_x2; vertices[2].uv.y = uv_y2; vertices[2].color = color;
    vertices[3].pos.x = x3; vertices[3].pos.y = y3; vertices[3].uv.x = uv_x3; vertices[3].uv.y = uv_y3; vertices[3].color = color;
    _nmd_context.drawList.numVertices += 4;
}

void nmd_add_line(float x0, float y0, float x1, float y1, nmd_color color, float thickness)
{
    if (!color.a)
        return;

    nmd_path_to(x0, y0);
    nmd_path_to(x1, y1);
    nmd_path_stroke(color, false, thickness);
}

void nmd_add_convex_polygon_filled(const nmd_vec2* points, size_t numPoints, nmd_color color)
{
    if (numPoints < 3)
        return;

    if (_nmd_context.drawList.fillAntiAliasing)
    {
        /* Anti-aliased fill */
        const float AA_SIZE = 1.0f;
        nmd_color col_trans = color;
        col_trans.a = 0;

        const int idx_count = (numPoints - 2) * 3 + numPoints * 6;
        const int vtx_count = (numPoints * 2);
        if (!_nmd_reserve(vtx_count, idx_count))
            return;

        /* Add indexes for fill */
        unsigned int vtx_inner_idx = _nmd_context.drawList.numVertices;
        unsigned int vtx_outer_idx = _nmd_context.drawList.numVertices + 1;
        nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
        for (int i = 2; i < numPoints; i++)
        {
            indices[0] = vtx_inner_idx; indices[1] = vtx_inner_idx + ((i - 1) << 1); indices[2] = vtx_inner_idx + (i << 1);
            indices += 3;
        }

        /* Compute normals */
        nmd_vec2* temp_normals = (nmd_vec2*)NMD_ALLOCA(numPoints * sizeof(nmd_vec2));
        for (int i0 = numPoints - 1, i1 = 0; i1 < numPoints; i0 = i1++)
        {
            const nmd_vec2* p0 = &points[i0];
            const nmd_vec2* p1 = &points[i1];
            float dx = p1->x - p0->x;
            float dy = p1->y - p0->y;
            NMD_NORMALIZE2F_OVER_ZERO(dx, dy);
            temp_normals[i0].x = dy;
            temp_normals[i0].y = -dx;
        }

        nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
        for (int i0 = numPoints - 1, i1 = 0; i1 < numPoints; i0 = i1++)
        {
            /* Average normals */
            const nmd_vec2* n0 = &temp_normals[i0];
            const nmd_vec2* n1 = &temp_normals[i1];
            float dm_x = (n0->x + n1->x) * 0.5f;
            float dm_y = (n0->y + n1->y) * 0.5f;
            NMD_FIXNORMAL2F(dm_x, dm_y);
            dm_x *= AA_SIZE * 0.5f;
            dm_y *= AA_SIZE * 0.5f;

            /* Add vertices */
            vertices[0].pos.x = (points[i1].x - dm_x); vertices[0].pos.y = (points[i1].y - dm_y); vertices[0].color = color;
            vertices[1].pos.x = (points[i1].x + dm_x); vertices[1].pos.y = (points[i1].y + dm_y); vertices[1].color = col_trans;
            vertices += 2;

            /* Add indexes for fringes */
            indices[0] = vtx_inner_idx + (i1 << 1); indices[1] = vtx_inner_idx + (i0 << 1); indices[2] = vtx_outer_idx + (i0 << 1);
            indices[3] = vtx_outer_idx + (i0 << 1); indices[4] = vtx_outer_idx + (i1 << 1); indices[5] = vtx_inner_idx + (i1 << 1);
            indices += 6;
        }
        _nmd_context.drawList.numVertices = vertices - _nmd_context.drawList.vertices;
        _nmd_context.drawList.numIndices = indices - _nmd_context.drawList.indices;
    }
    else
    {

        if (!_nmd_reserve(numPoints, (numPoints - 2) * 3))
            return;

        const size_t offset = _nmd_context.drawList.numVertices;
        nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
        for (size_t i = 2; i < numPoints; i++)
            indices[(i - 2) * 3 + 0] = offset, indices[(i - 2) * 3 + 1] = offset + (i - 1), indices[(i - 2) * 3 + 2] = offset + i;
        _nmd_context.drawList.numIndices += (numPoints - 2) * 3;

        nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
        for (size_t i = 0; i < numPoints; i++)
            vertices[i].pos.x = points[i].x, vertices[i].pos.y = points[i].y, vertices[i].color = color;
        _nmd_context.drawList.numVertices += numPoints;
    }
}

#ifdef NMD_GRAPHICS_ENABLE_DUMMY_TEXT_API

void nmd_add_dummy_text(float x, float y, const char* text, float height, nmd_color color, float spacing)
{
    const float initial_x = x;
    const float width = height * 0.55f;
    for (; *text; text++, x += width + spacing)
    {
        switch (*text)
        {
        case ' ': break;
        case '\n':
            x = initial_x;
            y += height;
            break;
        case '!':
            nmd_add_rect_filled(x + width * 0.4f, y + height * 0.052f, x + width * 0.6f, y + height * 0.557f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.4f, y + height * 0.622f, x + width * 0.6f, y + height * 0.751f, color, 0, 0);
            break;
        case '"':
            nmd_add_rect_filled(x + width * 0.230f, y + height * 0.05f, x + width * 0.425f, y + height * 0.28f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.575f, y + height * 0.05f, x + width * 0.769f, y + height * 0.28f, color, 0, 0);
            break;
        case '#':
            nmd_add_quad_filled(x + width * 0.313f, y + height * 0.104f, x + width * 0.442f, y + height * 0.104f, x + width * 0.326, y + height * 0.741f, x + width * 0.195f, y + height * 0.741f, color);
            nmd_add_quad_filled(x + width * 0.670f, y + height * 0.104f, x + width * 0.802f, y + height * 0.104f, x + width * 0.684, y + height * 0.741f, x + width * 0.552f, y + height * 0.741f, color);
            nmd_add_rect_filled(x + width * 0.083f, y + height * 0.270f, x + width * 0.962f, y + height * 0.328f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.038f, y + height * 0.498f, x + width * 0.915f, y + height * 0.562f, color, 0, 0);
            break;
        /*case 0x24: /* $ 
            width = height * 0.75f;
            nmd_add_quad_filled(x + width * 0.7f, y, x + width * 0.9f, y, x + width * 0.7f, y + height, x + width * 0.5f, y + height, color);
            break;*/
        case '%':
            nmd_add_rect_filled(x, y, x + width * 0.5f, y + height * 0.3f, color, 0, 0);
            nmd_add_quad_filled(x + width * 0.7f, y, x + width, y, x + width * 0.3f, y + height, x, y + height, color);
            nmd_add_rect_filled(x + width * 0.5f, y + height * 0.7f, x + width, y + height, color, 0, 0);
            break;
        case '\'':
            nmd_add_rect_filled(x + width * 0.399f, y + height * 0.054f, x + width * 0.602f, y + height * 0.283f, color, 0, 0);
            break;
        case '*':
            nmd_add_rect_filled(x + width * 0.437f, y + height * 0.054f, x + width * 0.565f, y + height * 0.465f, color, 0, 0);
            nmd_add_quad_filled(x + width * 0.146f, y + height * 0.184f, x + width * 0.207f, y + height * 0.127f, x + width * 0.852f, y + height * 0.332f, x + width * 0.793f, y + height * 0.390f, color);
            nmd_add_quad_filled(x + width * 0.793f, y + height * 0.127f, x + width * 0.853f, y + height * 0.184f, x + width * 0.201f, y + height * 0.390f, x + width * 0.146f, y + height * 0.331f, color);
            break;
        case '+':
            nmd_add_rect_filled(x + width * 0.075f, y + height * 0.440f, x + width * 0.925f, y + height * 0.512f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.397f, y + height * 0.244f, x + width * 0.576f, y + height * 0.713f, color, 0, 0);
            break;
        case '-':
            nmd_add_rect_filled(x + width * 0.234f, y + height * 0.436f, x + width * 0.765f, y + height * 0.516f, color, 0, 0);
            break;
        case '.':
            nmd_add_rect_filled(x + width * 0.345f, y + height * 0.587f, x + width * 0.640f, y + height * 0.750f, color, 0, 0);
            break;
        case '/':
            nmd_add_quad_filled(x + width * 0.711f, y + height * 0.052f, x + width * 0.859f, y + height * 0.052f, x + width * 0.249f, y + height * 0.845f, x + width * 0.103f, y + height * 0.845f, color);
            break;
        case '0':
            nmd_add_rect(x + width * 0.05f, y + height * 0.05f, x + width * 0.95f, y + height * 0.95f, color, 0, 0, width * 0.2f);
            break;
        case '1':
            nmd_add_rect_filled(x, y + height * 0.85f, x + width, y + height, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.4f, y, x + width * 0.6f, y + height, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.1f, y, x + width * 0.4f, y + height * 0.2f, color, 0, 0);
            break;
        case '4':
            nmd_add_line(x + width * 0.80f, y, x + width * 0.80f, y + height, color, height * 0.15f);
            nmd_add_line(x, y + height * 0.7f, x + width * 0.80f, y, color, height * 0.15f);
            nmd_add_line(x, y + height * 0.7f, x + width * 0.95f, y + height * 0.7f, color, height * 0.15f);
            break;
        case '7':
            nmd_add_rect_filled(x, y, x + width, y + height * 0.15f, color, 0, 0);
            nmd_add_line(x + width, y, x, y + height, color, height * 0.15f);
            break;
        case '8':
            nmd_add_circle(x + width * 0.5f, y + height * 0.25f, height * 0.3f, color, 12, height * 0.15f);
            nmd_add_circle(x + width * 0.5f, y + height * 0.75f, height * 0.3f, color, 12, height * 0.15f);
            break;
        case '9':
            nmd_add_circle(x + width * 0.5f, y + height * 0.25f, height * 0.3f, color, 12, height * 0.15f);
            nmd_add_line(x + width * 0.95f, y, x + width * 0.95f, y + height, color, height * 0.15f);
            break;
        case ':':
            nmd_add_rect_filled(x, y + height * 0.1f, x + width, y + height * 0.3f, color, 0, 0);
            nmd_add_rect_filled(x, y + height * 0.7f, x + width, y + height * 0.9f, color, 0, 0);
            break;
        case '=':
            nmd_add_rect_filled(x, y + height * 0.2f, x + width, y + height * 0.35f, color, 0, 0);
            nmd_add_rect_filled(x, y + height * 0.65f, x + width, y + height * 0.8f, color, 0, 0);
            break;
        case 'A':
            nmd_add_line(x, y + height, x + width * 0.5f, y, color, height * 0.15f);
            nmd_add_line(x + width * 0.1f, y + height * 0.7f, x + width * 0.9f, y + height * 0.7f, color, height * 0.15f);
            nmd_add_line(x + width * 0.5f, y, x + width, y + height, color, height * 0.15f);
            break;
        case 'E':
            nmd_add_rect_filled(x + width * 0.178f, y + height * 0.102f, x + width * 0.336f, y + height * 0.742f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.336f, y + height * 0.105f, x + width * 0.837f, y + height * 0.175f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.336f, y + height * 0.373f, x + width * 0.837f, y + height * 0.446f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.336f, y + height * 0.670f, x + width * 0.837f, y + height * 0.742f, color, 0, 0);
            break;
        case 'F':
            nmd_add_rect_filled(x + width * 0.178f, y + height * 0.102f, x + width * 0.336f, y + height * 0.742f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.336f, y + height * 0.105f, x + width * 0.837f, y + height * 0.175f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.336f, y + height * 0.373f, x + width * 0.837f, y + height * 0.446f, color, 0, 0);
            break;
        case 'H':
            nmd_add_rect_filled(x + width * 0.098f, y + height * 0.102f, x + width * 0.256f, y + height * 0.742f, color, 0, 0);
            //nmd_add_rect_filled(x + width * 0.207f, y + height * 0.102f, x + width * 0.368f, y + height * 0.669f, color, 0, 0);
            //nmd_add_rect_filled(x + width * 0.207f, y + height * 0.102f, x + width * 0.368f, y + height * 0.669f, color, 0, 0);
            break;
        case 'I':
            nmd_add_rect_filled(x, y, x + width, y + height, color, 0, 0);
            break;
        case 'L':
            nmd_add_rect_filled(x + width * 0.207f, y + height * 0.102f, x + width * 0.368f, y + height * 0.669f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.207f, y + height * 0.670f, x + width * 0.875f, y + height * 0.740f, color, 0, 0);
            break;
        case 'M':
            nmd_add_rect_filled(x, y, x + width * 0.2f, y + height, color, 0, 0);
            nmd_add_quad_filled(x, y, x + width * 0.2f, y, x + width * 0.4f, y + height, x + width * 0.6f, y + height, color);
            nmd_add_quad_filled(x + width * 0.8f, y, x + width, y, x + width * 0.4f, y + height, x + width * 0.6f, y + height, color);
            nmd_add_rect_filled(x + width * 0.8f, y, x + width, y + height, color, 0, 0);
            break;
        case 'N':
            nmd_add_rect_filled(x, y, x + width * 0.2f, y + height, color, 0, 0);
            nmd_add_quad_filled(x, y, x + width * 0.2f, y, x + width * 0.8f, y + height, x + width, y + height, color);
            nmd_add_rect_filled(x + width * 0.8f, y, x + width, y + height, color, 0, 0);
            break;
        case 'T':
            nmd_add_rect_filled(x + width * 0.075f, y + height * 0.102f, x + width * 0.930f, y + height * 0.175f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.425f, y + height * 0.175f, x + width * 0.575f, y + height * 0.740f, color, 0, 0);
            break;
        case 'V':
            nmd_add_quad_filled(x, y, x + width * 0.2f, y, x + width * 0.4f, y + height, x + width * 0.6f, y + height, color);
            nmd_add_quad_filled(x + width * 0.8f, y, x + width, y, x + width * 0.4f, y + height, x + width * 0.6f, y + height, color);
            break;
        case 'X':
            nmd_add_quad_filled(x, y, x + width * 0.2f, y, x + width * 0.8f, y + height, x + width, y + height, color);
            nmd_add_quad_filled(x + width * 0.8f, y, x + width, y, x, y + height, x + width * 0.2f, y + height, color);
            break;
        case 'Y':
            nmd_add_quad_filled(x, y, x + width * 0.2f, y, x + width * 0.4f, y + height * 0.5f, x + width * 0.6f, y + height * 0.5f, color);
            nmd_add_quad_filled(x + width * 0.8f, y, x + width, y, x + width * 0.4f, y + height * 0.5f, x + width * 0.6f, y + height * 0.5f, color);
            nmd_add_rect_filled(x + width * 0.4f, y + height * 0.5f, x + width * 0.6f, y + height, color, 0, 0);
            break;
        case 'Z':
            nmd_add_rect_filled(x, y, x + width, y + height * 0.15f, color, 0, 0);
            nmd_add_quad_filled(x + width * 0.8f, y, x + width, y, x + width * 0.2f, y + height, x, y + height, color);
            nmd_add_rect_filled(x, y + height * 0.85f, x + width, y + height, color, 0, 0);
            break;

        case '[':
            nmd_add_rect_filled(x + width * 0.306f, y + height * 0.034f, x + width * 0.744f, y + height * 0.104f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.306f, y + height * 0.104f, x + width * 0.454f, y + height * 0.872f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.306f, y + height * 0.872f, x + width * 0.744f, y + height * 0.945f, color, 0, 0);
            break;
        case '\\':
            nmd_add_quad_filled(x + width * 0.142f, y + height * 0.052f, x + width * 0.289f, y + height * 0.052f, x + width * 0.899f, y + height * 0.846f, x + width * 0.750f, y + height * 0.846f, color);
            break;
        case ']':
            nmd_add_rect_filled(x + width * 0.256f, y + height * 0.034f, x + width * 0.694f, y + height * 0.105f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.533f, y + height * 0.105f, x + width * 0.694f, y + height * 0.872f, color, 0, 0);
            nmd_add_rect_filled(x + width * 0.256f, y + height * 0.872f, x + width * 0.694f, y + height * 0.945f, color, 0, 0);
            break;
        case '^':
            nmd_add_quad_filled(x + width * 0.432f, y + height * 0.102f, x + width * 0.559f, y + height * 0.102f, x + width * 0.248f, y + height * 0.414f, x + width * 0.107f, y + height * 0.414f, color);
            nmd_add_quad_filled(x + width * 0.432f, y + height * 0.102f, x + width * 0.559f, y + height * 0.102f, x + width * 0.900f, y + height * 0.414f, x + width * 0.746f, y + height * 0.414f, color);
            break;
        case '_':
            nmd_add_rect_filled(x, y + height * 0.870f, x + width, y + height * 0.940f, color, 0, 0);
            break;
        case '`':
            nmd_add_quad_filled(x + width * 0.154f, y + height * 0.052f, x + width * 0.377f, y + height * 0.052f, x + width * 0.592f, y + height * 0.167f, x + width * 0.439f, y + height * 0.167f, color);
            break;

        case 'a':
            break;
        }
    }
}
#endif /* NMD_GRAPHICS_ENABLE_DUMMY_TEXT_API */

/*
void DrawList::AddBezierCurve(const nmd_vec2& p1, const nmd_vec2& p2, const nmd_vec2& p3, const nmd_vec2& p4, Color color, float thickness, size_t numSegments)
{
    if (!color.a)
        return;

    PathLineTo(p1);
    PathBezierCurveTo(p2, p3, p4, numSegments);
    PathStroke(color, false, thickness);
}
void nmd_add_text(float x, float y, const char* text, nmd_color color)
{
    nmd_add_text(_nmd_context.drawList.defaultFont, x, y, text, color);
}
*/

void nmd_add_text(const nmd_atlas* font, float x, float y, const char* text, const char* textEnd, nmd_color color)
{
    if (!color.a)
        return;

    if (!textEnd)
        textEnd = text + strlen(text);

    if (!_nmd_reserve((textEnd - text) * 4, (textEnd - text) * 6))
        return;

    nmd_push_draw_command(0);

    stbtt_aligned_quad q;
    for(; text < textEnd; text++)
    {
        stbtt_GetBakedQuad((stbtt_bakedchar*)font->bakedChars, 512, 512, *text - 32, &x, &y, &q, 1);

        const size_t offset = _nmd_context.drawList.numVertices;

        nmd_index* indices = _nmd_context.drawList.indices + _nmd_context.drawList.numIndices;
        indices[0] = offset + 0; indices[1] = offset + 1; indices[2] = offset + 2;
        indices[3] = offset + 0; indices[4] = offset + 2; indices[5] = offset + 3;
        _nmd_context.drawList.numIndices += 6;
        
        nmd_vertex* vertices = _nmd_context.drawList.vertices + _nmd_context.drawList.numVertices;
        vertices[0].pos.x = q.x0; vertices[0].pos.y = q.y0; vertices[0].uv.x = q.s0; vertices[0].uv.y = q.t0; vertices[0].color = color;
        vertices[1].pos.x = q.x1; vertices[1].pos.y = q.y0; vertices[1].uv.x = q.s1; vertices[1].uv.y = q.t0; vertices[1].color = color;
        vertices[2].pos.x = q.x1; vertices[2].pos.y = q.y1; vertices[2].uv.x = q.s1; vertices[2].uv.y = q.t1; vertices[2].color = color;
        vertices[3].pos.x = q.x0; vertices[3].pos.y = q.y1; vertices[3].uv.x = q.s0; vertices[3].uv.y = q.t1; vertices[3].color = color;
        _nmd_context.drawList.numVertices += 4;
    }

    nmd_push_texture_draw_command(font->font, 0);
}

void nmd_add_image(nmd_tex_id userTextureId, float x0, float y0, float x1, float y1, nmd_color color)
{
    nmd_add_image_uv(userTextureId, x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void nmd_add_image_uv(nmd_tex_id userTextureId, float x0, float y0, float x1, float y1, float uv_x0, float uv_y0, float uv_x1, float uv_y1, nmd_color color)
{
    if (!color.a)
        return;

    nmd_push_draw_command(0);

    nmd_prim_rect_uv(x0, y0, x1, y1, uv_x0, uv_y0, uv_x1, uv_y1, color);
    
    nmd_push_texture_draw_command(userTextureId, 0);
}

void nmd_add_image_quad(nmd_tex_id userTextureId, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, nmd_color color)
{
    nmd_add_image_quad_uv(userTextureId, x0, y0, x1, y1, x2, y2, x3, y3, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, color);
}

void nmd_add_image_quad_uv(nmd_tex_id userTextureId, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float uv_x0, float uv_y0, float uv_x1, float uv_y1, float uv_x2, float uv_y2, float uv_x3, float uv_y3, nmd_color color)
{
    if (!color.a)
        return;

    nmd_push_draw_command(0);

    nmd_prim_quad_uv(x0, y0, x1, y1, x2, y2, x3, y3, uv_x0, uv_y0, uv_x1, uv_y1, uv_x2, uv_y2, uv_x3, uv_y3, color);

    nmd_push_texture_draw_command(userTextureId, 0);
}

void nmd_add_image_rounded(nmd_tex_id userTextureId, float x0, float y0, float x1, float y1, float rounding, uint32_t cornerFlags, nmd_color color)
{
    nmd_add_image_rounded_uv(userTextureId, x0, y0, x1, y1, rounding, cornerFlags, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void nmd_add_image_rounded_uv(nmd_tex_id userTextureId, float x0, float y0, float x1, float y1, float rounding, uint32_t cornerFlags, float uv_x0, float uv_y0, float uv_x1, float uv_y1, nmd_color color)
{
    if (!color.a)
        return;

    if (rounding <= 0.0f || !cornerFlags)
        nmd_add_image_uv(userTextureId, x0, y0, x1, y1, uv_x0, uv_y0, uv_x1, uv_y1, color);
    else
    {
        nmd_push_draw_command(0);

        const int vert_start_idx = _nmd_context.drawList.numVertices;
        nmd_path_rect(x0, y0, x1, y1, rounding, cornerFlags);
        nmd_path_fill_convex(color);
        const int vert_end_idx = _nmd_context.drawList.numVertices;

        _nmd_shade_verts_linear_uv(vert_start_idx, vert_end_idx, x0, y0, x1, y1, uv_x0, uv_y0, uv_x1, uv_y1, true);

        nmd_push_texture_draw_command(userTextureId, 0);
    }
}
