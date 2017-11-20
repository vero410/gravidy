GraviDy
=======

The astrophysics "N-body problem" consist in a dynamical
N celestial bodies model which interact gravitationally.

Solving this problem implies understand and predict
the movement of the system components, for example,
the Solar System, a stars cumulus, or a galaxy.

To systems with more than 3 bodies,
which are most of the interesting astronomical systems,
the problem do not have an analytic solution,
and it is necessary to use computational methods,
which are progresively more expensives to largest values of N.

GraviDy is a new GPU,
direct-summation N-body integrator written from scratch and based on the
Hermite scheme. The most important features of the code are:

 * Written in C/C++,
 * Using parallel computing techniques and libraries like OpenMP, MPI and CUDA,
 * full double-precision resolution,
 * its high modularity, which allows users to readily introduce new physics into it,
 * the exploitation of all high-performance computing resources available,
 * its maintenance and improvement cycle,
 * the fact that the code is publicly released under a BSD license and will be maintained via planned, public, regular updates.

Install
=======

Enter the src directory and proceed to edit the Makefile with the Boost, CUDA and OpenMPI paths if they are not installed on the system, otherwise the flags are not needed.

Every version can be generated by:

        make pn, CPU version
        make mpi, MPI version
        make gpu, GPU version


Input submodule
=======

The initial conditions are in a different git repository, that you can obtain by executing the script setup-submodule, in the root of the repository. You need an account on gitlabs first and to register your public ssh key.

