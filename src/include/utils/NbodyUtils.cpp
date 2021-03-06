/*
 * Copyright (c) 2016
 *
 * Cristián Maureira-Fredes <cmaureirafredes@gmail.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "NbodyUtils.hpp"

/** Constructor */
NbodyUtils::NbodyUtils(NbodySystem *ns)
{
    double3 tmp = {0.0,0.0,0.0};
    this->ns = ns;
    this->cod = tmp;

    this->radii.resize(this->ns->n);
    this->layers_radii.resize(sizeof(LAGRANGE_RADII)/4.0);
}

/** Destructor */
NbodyUtils::~NbodyUtils()
{
    /* Empty */
}

/** General method to get many N-body attributes.
 * Virial radius, Crossing time, Close encounter radus, close encounter timestep,
 * core radius and half mass relaxation time.
 */
void NbodyUtils::nbody_attributes()
{
    // Virial radius
    ns->r_virial = get_virial_radius(ns->en.ini);

    // Crossing time
    ns->t_cr  = get_crossing_time(ns->r_virial);

    // Close encounter radius
    ns->r_cl  = get_close_encounter_radius(ns->r_virial);

    // Close encounter timestep
    ns->dt_cl = get_close_encounter_timestep(ns->r_cl);

    // Core radius
    ns->r_core = get_core_radius();

    // Half mass relaxation time
    ns->t_rlx = get_half_mass_relaxation_time();
}

/** Virial radius calculation */
double NbodyUtils::get_virial_radius(double energy)
{
    //std::cout << "Rv new: " << (G * ns->n * ns->total_mass) / std::abs(2 * ns->en.potential) << std::endl;
    return (-G * ns->total_mass * ns->total_mass) / (4 * energy);
}

/** Crossing time calculation */
double NbodyUtils::get_crossing_time(double r_virial)
{
    float M = ns->total_mass;
    float Rv3 = r_virial * r_virial * r_virial;
    float Ut = sqrt( Rv3 / (G * M));
    float t_cr = 2 * sqrt(2) * Ut;

    return t_cr;
}

/** Close encounter radius calculation */
double NbodyUtils::get_close_encounter_radius(double r_virial)
{
    return (4.0 * r_virial)/ns->n;
}

/** Close encounter timestep calculation */
double NbodyUtils::get_close_encounter_timestep(double r_cl)
{
    float r_cl3 = r_cl * r_cl * r_cl;
    return 0.04 * sqrt(r_cl3 * ns->n);
}

/** Core radius calculation using CORE_MASS as limit point, and
 * respect the center of mass of the system*/
double NbodyUtils::get_core_radius()
{
    std::vector<Distance> d(ns->n);
    double core_mass = 0.0;
    double radius = 0.0;

    #pragma omp parallel for
    for (unsigned int i = 0; i < ns->n; i++)
    {
        double rx  = ns->h_r[i].x - cod.x;
        double ry  = ns->h_r[i].y - cod.y;
        double rz  = ns->h_r[i].z - cod.z;
        double r   = sqrt(rx*rx + ry*ry + rz*rz);
        d[i].index = i;
        d[i].value = r;
    }

    std::sort(d.begin(), d.end());

    for (unsigned int i = 0; i < ns->n; i++)
    {
        if (core_mass > ns->total_mass*CORE_MASS)
        {
            i -= 1;
            radius = d[i].value;
            break;
        }
        core_mass += ns->h_r[d[i].index].w;
    }

    return radius;
}

/** Half mass relaxation time calculation.
 * Please note that this method uses a lot of empirical values and approximations.
 */
double NbodyUtils::get_half_mass_relaxation_time()
{
    cod = get_center_of_density();
    float r_h = get_halfmass_radius();
    ns->r_hm = r_h;
    float a = sqrt( (ns->n * r_h * r_h * r_h) / ( G * (ns->total_mass/(ns->n)) ));
    //float b = 1/log(r_h/sqrt(ns->e2));
    float b = 1/log(0.11 * ns->n); // non-softening depending
    float t_rh = 0.138 * a * b;

    return t_rh;
}

/** Center of density calculation */
double3 NbodyUtils::get_center_of_density()
{
    std::vector<double> p(ns->n);
    Distance empty;
    float radius, dsum;
    float aa, bb;

    empty.index = 0;
    empty.value = 0.0;

    unsigned int j = 0;
    #pragma omp parallel for private(j)
    for (unsigned int i = 0; i < ns->n; i++)
    {
        std::vector<Distance> d(ns->n);
        std::fill(d.begin(), d.end(), empty);
        #pragma omp parallel for
        for (j = 0; j < ns->n; j++)
        {
            if (i != j)
            {
                double rx = (ns->h_r[j].x - ns->h_r[i].x);
                double ry = (ns->h_r[j].y - ns->h_r[i].y);
                double rz = (ns->h_r[j].z - ns->h_r[i].z);
                d[j].value = sqrt(rx*rx + ry*ry + rz*rz);
                d[j].index = j;
            }
        }

        // Sorting the distances between the i particle and the whole system (j)
        std::sort(d.begin(), d.end());

        // We define the maximum radius using the J-th neighbor (J = 10)
        radius = d[J].value;

        // Density calculation
        aa = (J-1) * ns->h_r[i].w;
        bb = (4.0 * M_PI * radius * radius * radius)/3.0;
        p[i] = aa / bb;
    }

    double3 density_center = {0.0, 0.0, 0.0};
    dsum = 0.0;

    for (unsigned int i = 0; i < ns->n; i++)
    {
        dsum += p[i];
        density_center.x += ns->h_r[i].x * p[i];
        density_center.y += ns->h_r[i].y * p[i];
        density_center.z += ns->h_r[i].z * p[i];
    }

    density_center.x /= dsum;
    density_center.y /= dsum;
    density_center.z /= dsum;

    return density_center;
}

/** Half mass radius calculation */
double NbodyUtils::get_halfmass_radius()
{
    float half_mass;
    double r_h;

    std::vector<Distance> distances(ns->n);

    half_mass = 0;
    unsigned int j = 0;

    #pragma omp parallel for
    for (unsigned int i = 0; i < ns->n; i++)
    {
        double rx = (cod.x - ns->h_r[i].x);
        double ry = (cod.y - ns->h_r[i].y);
        double rz = (cod.z - ns->h_r[i].z);

        distances[i].index = i;
        distances[i].value = sqrt( rx * rx + ry * ry + rz * rz);
    }

    std::sort(distances.begin(), distances.end());

    for (unsigned int i = 0; i < ns->n; i++)
    {
        if(half_mass >= ns->total_mass/2.0)
        {
            j = i;
            break;
        }
        half_mass += ns->h_r[distances[i].index].w;
    }

    r_h = distances[j-1].value;

    return r_h;
}

/** Lagrange radii calculation */
void NbodyUtils::lagrange_radii()
{

    // Use the center of density (cod)
    cod = get_center_of_density();

    // Calculate all the radii of the particles of the system
    get_radii();

    // Sorting the distances related to the center of density
    sort(radii.begin(), radii.end());

    // Get the layers of the particles using a certain 'ratio' (by default 5%)
    // We are going from the center of density to the outside calculating
    // the different layers to get the lagrange radii
    get_layers();
}

/** Radii respect the center of density calculation, to be able to calculate
 * the layers of the Lagrange radii */
void NbodyUtils::get_radii()
{
    #pragma omp parallel for
    for(unsigned int i = 0; i < ns->n; i++)
    {
        double rx = ns->h_r[i].x - cod.x;
        double ry = ns->h_r[i].y - cod.y;
        double rz = ns->h_r[i].z - cod.z;
        radii[i].index = i;
        radii[i].value = sqrt(rx*rx + ry*ry + rz*rz);
    }

}

/** Layers of the Lagrange radii calculation.
 * This method consider the distribution in LAGRANGE_RADII to select
 * the % of mass for each layer
 */
void NbodyUtils::get_layers()
{
    float tmp_mass = 0.0;

    unsigned int layer_id = 0;
    //float m_ratio = ratio * layer_id;

    for (unsigned int i = 0; i < ns->n; i++)
    {
        //if (tmp_mass > m_ratio)
        //{
        //    layers_radii[layer_id - 1] = radii[i-1].value;
        //    layer_id += 1;
        //    m_ratio = ns->ratio * layer_id;
        //    if (m_ratio >= 1.0)
        //        break;
        //}
        if (tmp_mass >= ns->total_mass)
            break;

        if (tmp_mass > LAGRANGE_RADII[layer_id])
        {
            layers_radii[layer_id] = radii[i].value;
            layer_id += 1;
        }

        tmp_mass += ns->h_r[radii[i].index].w;
    }
}

/** Vector magnitude calculation */
double NbodyUtils::get_magnitude(double x, double y, double z)
{
    return sqrt(x*x + y*y + z*z);
}

/** Time step calculation */
double NbodyUtils::get_timestep_normal(unsigned int i, float ETA)
{
    // Calculating a_{1,i}^{(2)} = a_{0,i}^{(2)} + dt * a_{0,i}^{(3)}
    double ax1_2 = ns->h_a2[i].x + ns->h_dt[i] * ns->h_a3[i].x;
    double ay1_2 = ns->h_a2[i].y + ns->h_dt[i] * ns->h_a3[i].y;
    double az1_2 = ns->h_a2[i].z + ns->h_dt[i] * ns->h_a3[i].z;

    // |a_{1,i}|
    double abs_a1 = get_magnitude(ns->h_f[i].a[0],
                                  ns->h_f[i].a[1],
                                  ns->h_f[i].a[2]);
    // |j_{1,i}|
    double abs_j1 = get_magnitude(ns->h_f[i].a1[0],
                                  ns->h_f[i].a1[1],
                                  ns->h_f[i].a1[2]);
    // |j_{1,i}|^{2}
    double abs_j12  = abs_j1 * abs_j1;
    // a_{1,i}^{(3)} = a_{0,i}^{(3)} because the 3rd-order interpolation
    double abs_a1_3 = get_magnitude(ns->h_a3[i].x,
                                    ns->h_a3[i].y,
                                    ns->h_a3[i].z);
    // |a_{1,i}^{(2)}|
    double abs_a1_2 = get_magnitude(ax1_2, ay1_2, az1_2);
    // |a_{1,i}^{(2)}|^{2}
    double abs_a1_22  = abs_a1_2 * abs_a1_2;

    double normal_dt = sqrt(ETA * ((abs_a1 * abs_a1_2 + abs_j12) / (abs_j1 * abs_a1_3 + abs_a1_22)));
    return normal_dt;
}

/** Normalization of the timestep.
 * This method take care of the limits conditions to avoid large jumps between
 * the timestep distribution
 */
double NbodyUtils::normalize_dt(double new_dt, double old_dt, double t, unsigned int i)
{
    if (new_dt <= old_dt/8)
    {
        new_dt = D_TIME_MIN;
    }
    else if ( old_dt/8 < new_dt && new_dt <= old_dt/4)
    {
        new_dt = old_dt / 8;
    }
    else if ( old_dt/4 < new_dt && new_dt <= old_dt/2)
    {
        new_dt = old_dt / 4;
    }
    else if ( old_dt/2 < new_dt && new_dt <= old_dt)
    {
        new_dt = old_dt / 2;
    }
    else if ( old_dt < new_dt && new_dt <= old_dt * 2)
    {
        new_dt = old_dt;
    }
    else if (2 * old_dt < new_dt)
    {
        double val = t/(2 * old_dt);
        //float val = t/(2 * old_dt);
        if(std::ceil(val) == val)
        {
            new_dt = 2.0 * old_dt;
        }
        else
        {
            new_dt = old_dt;
        }
    }
    else
    {
        //std::cerr << "this will never happen...I promise" << std::endl;
        new_dt = old_dt;
    }

    //if (new_dt <= D_TIME_MIN)
    if (new_dt < D_TIME_MIN)
    {
        new_dt = D_TIME_MIN;
    }
    //else if (new_dt >= D_TIME_MAX)
    else if (new_dt > D_TIME_MAX)
    {
        new_dt = D_TIME_MAX;
    }

    return new_dt;
}

/** Kinetic and Potential energy calculation */
double NbodyUtils::get_energy(double ext)
{
    ns->en.potential = 0.0;
    ns->en.kinetic   = 0.0;
    #ifdef PN
    double energy_pn = 0.0;
    #endif

    #pragma omp parallel for
    for (unsigned int i = 0; i < ns->n; i++)
    {
        double epot_tmp = 0.0;
        for (unsigned int j = i+1; j < ns->n; j++)
        {
            double rx = ns->h_r[j].x - ns->h_r[i].x;
            double ry = ns->h_r[j].y - ns->h_r[i].y;
            double rz = ns->h_r[j].z - ns->h_r[i].z;
            double r2 = rx*rx + ry*ry + rz*rz;

            epot_tmp -= (ns->h_r[i].w * ns->h_r[j].w) / sqrt(r2);
            #ifdef PN
            double vx = ns->h_v[j].x - ns->h_v[i].x;
            double vy = ns->h_v[j].y - ns->h_v[i].y;
            double vz = ns->h_v[j].z - ns->h_v[i].z;
            double v_2 = vx*vx + vy*vy + vz*vz;
            double cinv = 1.0/SPEED_OF_LIGHT;
            double c2inv = cinv * cinv;
            double c4inv = c2inv * c2inv;
            double c5inv = c4inv * cinv;
            double G2 = G*G;
            double G3 = G*G2;
            double m1 =  ns->h_r[i].w;
            double m2 =  ns->h_r[j].w;
            double m1_2 = m1*m1;
            double m1_3 = m1*m2;
            double m2_2 = m2*m2;
            double rinv = 1.0/sqrt(r2);
            double r2inv = rinv*rinv;
            double r3inv = rinv*r2inv;

            double nv[3] = {rx * rinv, ry * rinv, rz * rinv};
            double nv1 = nv[0] * ns->h_v[i].x +
                         nv[1] * ns->h_v[i].y +
                         nv[2] * ns->h_v[i].z;
            double nv2 = nv[0] * ns->h_v[j].x +
                         nv[1] * ns->h_v[j].y +
                         nv[2] * ns->h_v[j].z;

            double nv1_2 = nv1 * nv1;
            double nv1_3 = nv1 * nv1_2;
            double nv2_2 = nv2 * nv2;

            double v1 = sqrt(ns->h_v[i].x * ns->h_v[i].x +
                             ns->h_v[i].y * ns->h_v[i].y +
                             ns->h_v[i].z * ns->h_v[i].z);
            double v2 = sqrt(ns->h_v[j].x * ns->h_v[j].x +
                             ns->h_v[j].y * ns->h_v[j].y +
                             ns->h_v[j].z * ns->h_v[j].z);

            double v1_2 = v1*v1;
            double v1_4 = v1_2*v1_2;
            double v1_6 = v1_4*v1_2;

            double v2_2 = v2*v2;

            double v1v2 = v1*v2;
            double v1v2_2 = v1v2*v1v2;

            energy_pn += c2inv * (0.5 * G2 * m1_2 * m2 * r2inv + 3.0/8.0 * m1 *
                         v1_4 + G * m1 * m2 * rinv * (-0.25 * nv1 * nv2 + 1.5 *
                         v1_2 - 7.0/4.0 * v1v2))
                         + c4inv * (-0.5 * G3 * m1_3 * m2 * r3inv - 19.0/8.0 * G3 *
                         m1_2 * m2_2 * r3inv + 5.0/16.0 * m1 * v1_6 + G * m1 * m2 *
                         rinv * (3.0/8.0 * nv1_3 * nv2 + 3.0/16.0 * nv1_2 * nv2_2 -
                         9.0/8.0 * nv1 * nv2 * v1_2 - 13.0/8.0 * nv2_2 + 21.0/8.0 *
                         v1_4 + 13.0/8.0 * nv1_2 * v1v2 + 0.75 * nv1 * nv2 * v1v2 -
                         55.0/8.0 * v1_2 * v1v2 + 17.0/8.0 * v1v2_2 + 31.0/16.0 *
                         v1_2 * v2_2) + G2 * m1_2 * m2 * r2inv * (29.0/4.0 * nv1_2 -
                         13.0/4.0 * nv1 * nv2 + 0.5 * nv2_2 - 1.5*v1_2 + 7.0/4.0 *
                         v2_2));
            energy_pn += 4.0/5.0 * G2 * m1_2 * m2 * c5inv * r2inv * nv1 * (v_2 - 2 *
                         G * (m1 - m2) * rinv);
            #endif
        }

        double vvx = ns->h_v[i].x * ns->h_v[i].x;
        double vvy = ns->h_v[i].y * ns->h_v[i].y;
        double vvz = ns->h_v[i].z * ns->h_v[i].z;
        double vv2 = vvx + vvy + vvz;

        double ekin_tmp = 0.5 * ns->h_r[i].w * vv2;

        #pragma omp atomic
        ns->en.kinetic += ekin_tmp;
        #pragma omp atomic
        ns->en.potential += epot_tmp;
    }

    #ifdef PN
    ext += energy_pn;
    #endif
    return ns->en.kinetic + ns->en.potential + ext;
}


/** Potential energy calculation. This method is auxiliary and use only
 * for verification procedures */
double NbodyUtils::get_potential()
{
    double epot = 0.0;

    #pragma omp parallel for
    for (unsigned int i = 0; i < ns->n; i++)
    {
        double epot_tmp = 0.0;
        for (unsigned int j = i+1; j < ns->n; j++)
        {
            double rx = ns->h_r[j].x - ns->h_r[i].x;
            double ry = ns->h_r[j].y - ns->h_r[i].y;
            double rz = ns->h_r[j].z - ns->h_r[i].z;
            double r2 = rx*rx + ry*ry + rz*rz;

            epot_tmp -= (ns->h_r[i].w * ns->h_r[j].w) / sqrt(r2);
        }

        #pragma omp atomic
        epot += epot_tmp;
    }

    return epot;
}

/** Kinetic energy calculation. This method is auxiliary and use only
 * for verification procedures */
double NbodyUtils::get_kinetic()
{
    double ekin = 0.0;

    #pragma omp parallel for
    for (unsigned int i = 0; i < ns->n; i++)
    {
        double vx = ns->h_v[i].x * ns->h_v[i].x;
        double vy = ns->h_v[i].y * ns->h_v[i].y;
        double vz = ns->h_v[i].z * ns->h_v[i].z;
        double v2 = vx + vy + vz;

        double ekin_tmp = 0.5 * ns->h_r[i].w * v2;

        #pragma omp atomic
        ekin += ekin_tmp;
    }

    return ekin;
}
