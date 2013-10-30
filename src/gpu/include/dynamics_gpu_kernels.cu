#include "dynamics_gpu_kernels.cuh"

/*
 * @fn k_energy
 *
 * @desc GPU Kernel which calculates the energy of the system.
 *
 */
__global__ void k_energy(double4 *r,
                         double4 *v,
                         double *ekin,
                         double *epot,
                         float *m,
                         int n)
{
    int i = threadIdx.x + blockDim.x * blockIdx.x;
    int j;
    double ekin_tmp = 0.0;
    double epot_tmp = 0.0;

    if (i < n)
    {
        epot_tmp = 0.0;
        for (j = i+1; j < n; j++)
        {
            double rx = r[j].x - r[i].x;
            double ry = r[j].y - r[i].y;
            double rz = r[j].z - r[i].z;
            double r2 = rx*rx + ry*ry + rz*rz;
            epot_tmp -= (m[i] * m[j]) * rsqrt(r2);
        }

        double vx = v[i].x * v[i].x;
        double vy = v[i].y * v[i].y;
        double vz = v[i].z * v[i].z;
        double v2 = vx + vy + vz;
        ekin_tmp = 0.5 * m[i] * v2;

        ekin[i] = ekin_tmp;
        epot[i] = epot_tmp;
    }
}

/*
 * @fn k_init_acc_jrk
 *
 * @desc GPU Kernel which calculates the initial acceleration and jerk
 * of all the particles of the system.
 *
 */
__global__ void k_init_acc_jrk (double4 *r,
                                double4 *v,
                                Forces *d_f,
                                int n,
                                double e2)
{

    extern __shared__ double4 sh[];
    double4 *sr = (double4*)sh;
    double4 *sv = (double4*)&sr[blockDim.x];

    Forces ff;
    ff.a[0] = 0.0;
    ff.a[1] = 0.0;
    ff.a[2] = 0.0;
    ff.a1[0] = 0.0;
    ff.a1[1] = 0.0;
    ff.a1[2] = 0.0;

    int id = threadIdx.x + blockDim.x * blockIdx.x;
    int tx = threadIdx.x;

    if (id < n)
    {
        double4 pos = r[id];
        double4 vel = v[id];

        int tile = 0;
        for (int i = 0; i < n; i += BSIZE)
        {
            int idx = tile * BSIZE + tx;

            sr[tx]   = r[idx];
            sv[tx]   = v[idx];
            __syncthreads();

            for (int k = 0; k < BSIZE; k++)
            {
                k_force_calculation(pos, vel, sr[k], sv[k], ff,e2);
            }
            __syncthreads();
            tile++;
        }

        d_f[id] = ff;
    }
}

/*
 * @fn k_force_calculation
 *
 * @desc GPU Kernel which calculates the interaction between
 *       a i-particle and a j-particle.
 *
 */
__device__ void k_force_calculation(double4 i_pos, double4 i_vel,
                                    double4 j_pos, double4 j_vel,
                                    Forces &d_f,
                                    double e2)
{
    double rx = j_pos.x - i_pos.x;
    double ry = j_pos.y - i_pos.y;
    double rz = j_pos.z - i_pos.z;

    double vx = j_vel.x - i_vel.x;
    double vy = j_vel.y - i_vel.y;
    double vz = j_vel.z - i_vel.z;

    double r2 = rx*rx + ry*ry + rz*rz + e2;
    double rinv = rsqrt(r2);
    double r2inv = rinv  * rinv;
    double r3inv = r2inv * rinv;
    double r5inv = r2inv * r3inv;
    double mr3inv = r3inv * j_pos.w;
    double mr5inv = r5inv * j_pos.w;

    double rv = rx*vx + ry*vy + rz*vz;

    d_f.a[0] += (rx * mr3inv);
    d_f.a[1] += (ry * mr3inv);
    d_f.a[2] += (rz * mr3inv);

    d_f.a1[0] += (vx * mr3inv - (3 * rv) * rx * mr5inv);
    d_f.a1[1] += (vy * mr3inv - (3 * rv) * ry * mr5inv);
    d_f.a1[2] += (vz * mr3inv - (3 * rv) * rz * mr5inv);
}

__device__ void k_force_calculation2(Predictor i_p,
                                     Predictor j_p,
                                     Forces &d_f,
                                     double e2)
{
    double rx = j_p.r[0] - i_p.r[0];
    double ry = j_p.r[1] - i_p.r[1];
    double rz = j_p.r[2] - i_p.r[2];

    double vx = j_p.v[0] - i_p.v[0];
    double vy = j_p.v[1] - i_p.v[1];
    double vz = j_p.v[2] - i_p.v[2];

    double r2     = rx*rx + ry*ry + rz*rz + e2;
    double rinv   = rsqrt(r2);
    double r2inv  = rinv  * rinv;
    double r3inv  = r2inv * rinv;
    double r5inv  = r2inv * r3inv;
    double mr3inv = r3inv * j_p.m;
    double mr5inv = r5inv * j_p.m;

    double rv = rx*vx + ry*vy + rz*vz;

    d_f.a[0] += (rx * mr3inv);
    d_f.a[1] += (ry * mr3inv);
    d_f.a[2] += (rz * mr3inv);

    d_f.a1[0] += (vx * mr3inv - (3 * rv) * rx * mr5inv);
    d_f.a1[1] += (vy * mr3inv - (3 * rv) * ry * mr5inv);
    d_f.a1[2] += (vz * mr3inv - (3 * rv) * rz * mr5inv);
}

/*
 * @fn k_update()
 *
 * @brief Gravitational interaction kernel.
 */
__global__ void k_update(Predictor *d_i,
                         Predictor *d_j,
                         Forces *d_fout,
                         int n,
                         int total,
                         double e2)
{
    int ibid = blockIdx.x;
    int jbid = blockIdx.y;
    int tid = threadIdx.x;
    int iaddr = tid + blockDim.x * ibid;
    int jstart = (n * (jbid  )) / NJBLOCK;
    int jend   = (n * (jbid+1)) / NJBLOCK;

    Predictor ip = d_i[iaddr];
    Forces fo;
    fo.a[0] = 0.0;
    fo.a[1] = 0.0;
    fo.a[2] = 0.0;
    fo.a1[0] = 0.0;
    fo.a1[1] = 0.0;
    fo.a1[2] = 0.0;

    for(int j=jstart; j<jend; j+=BSIZE){
        __shared__ Predictor jpshare[BSIZE];
        __syncthreads();
        Predictor *src = (Predictor *)&d_j[j];
        Predictor *dst = (Predictor *)jpshare;
        dst[      tid] = src[      tid];
        dst[BSIZE+tid] = src[BSIZE+tid];
        __syncthreads();

        // If the total amount of particles is not a multiple of BSIZE
        if(jend-j < BSIZE){
            //#pragma unroll 4
            for(int jj=0; jj<jend-j; jj++){
                Predictor jp = jpshare[jj];
                k_force_calculation2(ip, jp, fo, e2);
            }
        }
        else{
            //#pragma unroll 4
            for(int jj=0; jj<BSIZE; jj++){
                Predictor jp = jpshare[jj];
                k_force_calculation2(ip, jp, fo, e2);
            }
        }
    }
    d_fout[iaddr*NJBLOCK + jbid] = fo;

}

/*
 * @fn reduce()
 *
 * @brief Forces reduction kernel
 */
__global__ void reduce(Forces *d_in,
                       Forces *d_out,
                       unsigned int total)
{
    extern __shared__ Forces sdata[];

    const int xid   = threadIdx.x;
    const int bid   = blockIdx.x;
    const int iaddr = xid + blockDim.x * bid;

    sdata[xid] = d_in[iaddr];
    __syncthreads();

    if(xid < 8) sdata[xid] += sdata[xid + 8];
    if(xid < 4) sdata[xid] += sdata[xid + 4];
    if(xid < 2) sdata[xid] += sdata[xid + 2];
    if(xid < 1) sdata[xid] += sdata[xid + 1];

    if(xid == 0){
        d_out[bid] = sdata[0];
    }
}