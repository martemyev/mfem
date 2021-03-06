// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.
//
//            -----------------------------------------------------
//            Volta Miniapp:  Simple Electrostatics Simulation Code
//            -----------------------------------------------------
//
// This miniapp solves a simple 2D or 3D electrostatic problem.
//
//                            Div eps Grad Phi = rho
//
// The permittivity function is that of the vacuum with an optional dielectric
// sphere. The charge density is either zero or a user defined sphere of charge.
//
// Boundary conditions for the electric potential consist of a user defined
// piecewise constant potential or a potential leading to a user selected
// uniform electric field.
//
// We discretize the electric potential with H1 finite elements. The electric
// field E is discretized with Nedelec finite elements.
//
// Compile with: make volta
//
// Sample runs:
//
//   A cylinder at constant voltage in a square, grounded metal pipe:
//      mpirun -np 4 volta -m ../../data/square-disc.mesh
//                         -dbcs '1 2 3 4 5 6 7 8' -dbcv '0 0 0 0 1 1 1 1'
//
//   A cylinder with a constant surface charge density in a square,
//   grounded metal pipe:
//      mpirun -np 4 volta -m ../../data/square-disc.mesh
//                         -nbcs '5 6 7 8' -nbcv '5e-11 5e-11 5e-11 5e-11'
//                         -dbcs '1 2 3 4'
//
//   A cylindrical voltaic pile within a grounded metal sphere:
//      mpirun -np 4 volta -dbcs 1 -vp '0 -0.5 0 0 0.5 0 0.2 1'
//
//   A charged sphere, off-center, within a grounded metal sphere:
//      mpirun -np 4 volta -dbcs 1 -cs '0.0 0.5 0.0 0.2 2.0e-11'
//
//   A dielectric sphere suspended in a uniform electric field:
//      mpirun -np 4 volta -dbcs 1 -dbcg -ds '0.0 0.0 0.0 0.2 8.0'
//
//   By default the sources and fields are all zero:
//      mpirun -np 4 volta

#include "volta_solver.hpp"
#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;
using namespace mfem::electromagnetics;

// Permittivity Function
static Vector ds_params_(0);  // Center, Radius, and Permittivity
//                               of dielectric sphere
double dielectric_sphere(const Vector &);

// Charge Density Function
static Vector cs_params_(0);  // Center, Radius, and Total Charge
//                               of charged sphere
double charged_sphere(const Vector &);

// Polarization
static Vector vp_params_(0);  // Axis Start, Axis End, Cylinder Radius,
//                               and Polarization Magnitude
void voltaic_pile(const Vector &, Vector &);

// Phi Boundary Condition
static Vector e_uniform_(0);
double phi_bc_uniform(const Vector &);

// Prints the program's logo to the given output stream
void display_banner(ostream & os);

int main(int argc, char *argv[])
{
   // Initialize MPI.
   int num_procs, myid;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);

   if ( myid == 0 ) { display_banner(cout); }

   // Parse command-line options.
   const char *mesh_file = "butterfly_3d.mesh";
   int order = 1;
   int sr = 0, pr = 0;
   bool visualization = true;
   bool visit = true;

   Array<int> dbcs;
   Array<int> nbcs;

   Vector dbcv;
   Vector nbcv;

   bool dbcg = false;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree).");
   args.AddOption(&sr, "-rs", "--serial-ref-levels",
                  "Number of serial refinement levels.");
   args.AddOption(&pr, "-rp", "--parallel-ref-levels",
                  "Number of parallel refinement levels.");
   args.AddOption(&e_uniform_, "-uebc", "--uniform-e-bc",
                  "Specify if the three components of the constant electric field");
   args.AddOption(&ds_params_, "-ds", "--dielectric-sphere-params",
                  "Center, Radius, and Permittivity of Dielectric Sphere");
   args.AddOption(&cs_params_, "-cs", "--charged-sphere-params",
                  "Center, Radius, and Total Charge of Charged Sphere");
   args.AddOption(&vp_params_, "-vp", "--voltaic-pile-params",
                  "Axis End Points, Radius, and Polarization of Cylindrical Voltaic Pile");
   args.AddOption(&dbcs, "-dbcs", "--dirichlet-bc-surf",
                  "Dirichlet Boundary Condition Surfaces");
   args.AddOption(&dbcv, "-dbcv", "--dirichlet-bc-vals",
                  "Dirichlet Boundary Condition Values");
   args.AddOption(&dbcg, "-dbcg", "--dirichlet-bc-gradient",
                  "-no-dbcg", "--no-dirichlet-bc-gradient",
                  "Dirichlet Boundary Condition Gradient (phi = -z)");
   args.AddOption(&nbcs, "-nbcs", "--neumann-bc-surf",
                  "Neumann Boundary Condition Surfaces");
   args.AddOption(&nbcv, "-nbcv", "--neumann-bc-vals",
                  "Neumann Boundary Condition Values");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&visit, "-visit", "--visit", "-no-visit", "--no-visit",
                  "Enable or disable VisIt visualization.");
   args.Parse();
   if (!args.Good())
   {
      if (myid == 0)
      {
         args.PrintUsage(cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myid == 0)
   {
      args.PrintOptions(cout);
   }

   // Read the (serial) mesh from the given mesh file on all processors.  We
   // can handle triangular, quadrilateral, tetrahedral, hexahedral, surface
   // and volume meshes with the same code.
   Mesh *mesh;
   ifstream imesh(mesh_file);
   if (!imesh)
   {
      if (myid == 0)
      {
         cerr << "\nCan not open mesh file: " << mesh_file << '\n' << endl;
      }
      MPI_Finalize();
      return 2;
   }
   mesh = new Mesh(imesh, 1, 1);
   imesh.close();

   int sdim = mesh->SpaceDimension();

   // Refine the serial mesh on all processors to increase the resolution. In
   // this example we do 'ref_levels' of uniform refinement. NURBS meshes are
   // refined at least twice, as they are typically coarse.
   if (myid == 0) { cout << "Starting initialization." << endl; }
   {
      int ref_levels = sr;
      if (mesh->NURBSext && ref_levels < 2)
      {
         ref_levels = 2;
      }
      for (int l = 0; l < ref_levels; l++)
      {
         mesh->UniformRefinement();
      }
   }

   // Project a NURBS mesh to a piecewise-quadratic curved mesh. Make sure that
   // the mesh is non-conforming.
   if (mesh->NURBSext)
   {
      mesh->SetCurvature(2);
   }
   mesh->EnsureNCMesh();

   // Define a parallel mesh by a partitioning of the serial mesh. Refine
   // this mesh further in parallel to increase the resolution. Once the
   // parallel mesh is defined, the serial mesh can be deleted.
   ParMesh pmesh(MPI_COMM_WORLD, *mesh);
   delete mesh;

   // Refine this mesh in parallel to increase the resolution.
   int par_ref_levels = pr;
   for (int l = 0; l < par_ref_levels; l++)
   {
      pmesh.UniformRefinement();
   }

   // If the gradient bc was selected but the E field was not specified
   // set a default vector value.
   if ( dbcg && e_uniform_.Size() != sdim )
   {
      e_uniform_.SetSize(sdim);
      e_uniform_ = 0.0;
      e_uniform_(sdim-1) = 1.0;
   }

   // If values for Dirichlet BCs were not set assume they are zero
   if ( dbcv.Size() < dbcs.Size() && !dbcg )
   {
      dbcv.SetSize(dbcs.Size());
      dbcv = 0.0;
   }

   // If values for Neumann BCs were not set assume they are zero
   if ( nbcv.Size() < nbcs.Size() )
   {
      nbcv.SetSize(nbcs.Size());
      nbcv = 0.0;
   }

   // Create the Electrostatic solver
   VoltaSolver Volta(pmesh, order, dbcs, dbcv, nbcs, nbcv,
                     ( ds_params_.Size() > 0 ) ? dielectric_sphere : NULL,
                     ( e_uniform_.Size() > 0 ) ? phi_bc_uniform    : NULL,
                     ( cs_params_.Size() > 0 ) ? charged_sphere    : NULL,
                     ( vp_params_.Size() > 0 ) ? voltaic_pile      : NULL);

   // Initialize GLVis visualization
   if (visualization)
   {
      Volta.InitializeGLVis();
   }

   // Initialize VisIt visualization
   VisItDataCollection visit_dc("Volta-AMR-Parallel", &pmesh);

   if ( visit )
   {
      Volta.RegisterVisItFields(visit_dc);
   }
   if (myid == 0) { cout << "Initialization done." << endl; }

   // The main AMR loop. In each iteration we solve the problem on the current
   // mesh, visualize the solution, estimate the error on all elements, refine
   // the worst elements and update all objects to work with the new mesh.  We
   // refine until the maximum number of dofs in the nodal finite element space
   // reaches 10 million.
   const int max_dofs = 10000000;
   for (int it = 1; it <= 100; it++)
   {
      if (myid == 0)
      {
         cout << "\nAMR Iteration " << it << endl;
      }

      // Display the current number of DoFs in each finite element space
      Volta.PrintSizes();

      // Solve the system and compute any auxiliary fields
      Volta.Solve();

      // Determine the current size of the linear system
      int prob_size = Volta.GetProblemSize();

      // Write fields to disk for VisIt
      if ( visit )
      {
         Volta.WriteVisItFields(it);
      }

      // Send the solution by socket to a GLVis server.
      if (visualization)
      {
         Volta.DisplayToGLVis();
      }
      if (myid == 0 && (visit || visualization)) { cout << "done." << endl; }

      if (myid == 0)
      {
         cout << "AMR iteration " << it << " complete." << endl;
      }

      // Check stopping criteria
      if (prob_size > max_dofs)
      {
         if (myid == 0)
         {
            cout << "Reached maximum number of dofs, exiting..." << endl;
         }
         break;
      }

      // Wait for user input. Ask every 10th iteration.
      char c = 'c';
      if (myid == 0 && (it % 10 == 0))
      {
         cout << "press (q)uit or (c)ontinue --> " << flush;
         cin >> c;
      }
      MPI_Bcast(&c, 1, MPI_CHAR, 0, MPI_COMM_WORLD);

      if (c != 'c')
      {
         break;
      }

      // Estimate element errors using the Zienkiewicz-Zhu error estimator.
      Vector errors(pmesh.GetNE());
      Volta.GetErrorEstimates(errors);

      double local_max_err = errors.Max();
      double global_max_err;
      MPI_Allreduce(&local_max_err, &global_max_err, 1,
                    MPI_DOUBLE, MPI_MAX, pmesh.GetComm());

      // Make a list of elements whose error is larger than a fraction
      // of the maximum element error. These elements will be refined.
      Array<int> ref_list;
      const double frac = 0.7;
      double threshold = frac * global_max_err;
      for (int i = 0; i < errors.Size(); i++)
      {
         if (errors[i] >= threshold) { ref_list.Append(i); }
      }

      // Refine the selected elements. Since we are going to transfer the
      // grid function x from the coarse mesh to the new fine mesh in the
      // next step, we need to request the "two-level state" of the mesh.
      if (myid == 0) { cout << " Refinement ..." << flush; }
      pmesh.GeneralRefinement(ref_list);

      // Update the electrostatic solver to reflect the new state of the mesh.
      Volta.Update();
   }

   MPI_Finalize();

   return 0;
}

// Print the Volta ascii logo to the given ostream
void display_banner(ostream & os)
{
   os << "  ____   ____     __   __            " << endl
      << "  \\   \\ /   /___ |  |_/  |______     " << endl
      << "   \\   Y   /  _ \\|  |\\   __\\__  \\    " << endl
      << "    \\     (  <_> )  |_|  |  / __ \\_  " << endl
      << "     \\___/ \\____/|____/__| (____  /  " << endl
      << "                                \\/   " << endl << flush;
}

// A sphere with constant permittivity.  The sphere has a radius,
// center, and permittivity specified on the command line and stored
// in ds_params_.
double dielectric_sphere(const Vector &x)
{
   double r2 = 0.0;

   for (int i=0; i<x.Size(); i++)
   {
      r2 += (x(i)-ds_params_(i))*(x(i)-ds_params_(i));
   }

   if ( sqrt(r2) <= ds_params_(x.Size()) )
   {
      return ds_params_(x.Size()+1) * epsilon0_;
   }
   return epsilon0_;
}

// A sphere with constant charge density.  The sphere has a radius,
// center, and total charge specified on the command line and stored
// in cs_params_.
double charged_sphere(const Vector &x)
{
   double r2 = 0.0;
   double rho = 0.0;

   if ( cs_params_(x.Size()) > 0.0 )
   {
      switch ( x.Size() )
      {
         case 2:
            rho = cs_params_(x.Size()+1)/(M_PI*pow(cs_params_(x.Size()),2));
            break;
         case 3:
            rho = 0.75*cs_params_(x.Size()+1)/(M_PI*pow(cs_params_(x.Size()),3));
            break;
         default:
            rho = 0.0;
      }
   }

   for (int i=0; i<x.Size(); i++)
   {
      r2 += (x(i)-cs_params_(i))*(x(i)-cs_params_(i));
   }

   if ( sqrt(r2) <= cs_params_(x.Size()) )
   {
      return rho;
   }
   return 0.0;
}

// A Cylindrical Rod of constant polarization.  The cylinder has two
// axis end points, a radius, and a constant electric polarization oriented
// along the axis.
void voltaic_pile(const Vector &x, Vector &p)
{
   p.SetSize(x.Size());
   p = 0.0;

   Vector  a(x.Size());  // Normalized Axis vector
   Vector xu(x.Size());  // x vector relative to the axis end-point

   xu = x;

   for (int i=0; i<x.Size(); i++)
   {
      xu[i] -= vp_params_[i];
      a[i]   = vp_params_[x.Size()+i] - vp_params_[i];
   }

   double h = a.Norml2();

   if ( h == 0.0 )
   {
      return;
   }

   double  r = vp_params_[2*x.Size()];
   double xa = xu*a;

   if ( h > 0.0 )
   {
      xu.Add(-xa/(h*h),a);
   }

   double xp = xu.Norml2();

   if ( xa >= 0.0 && xa <= h*h && xp <= r )
   {
      p.Add(vp_params_[2*x.Size()+1]/h,a);
   }
}

// To produce a uniform electric field the potential can be set
// to (- Ex x - Ey y - Ez z).
double phi_bc_uniform(const Vector &x)
{
   double phi = 0.0;

   for (int i=0; i<x.Size(); i++)
   {
      phi -= x(i) * e_uniform_(i);
   }

   return phi;
}
