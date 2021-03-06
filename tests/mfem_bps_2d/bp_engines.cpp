
#include <mfem.hpp>
#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;

int main(int argc, char *argv[])
{
   // 1. Initialize MPI.
   int num_procs, myid;
   MPI_Init(&argc, &argv);
   MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   MPI_Comm_rank(MPI_COMM_WORLD, &myid);
   const double start_time = MPI_Wtime();

   // 2. Parse command-line options.
   const char *empty_string = "(empty)";
   const char *mesh_file = empty_string;
   int ser_ref_levels = -1;
   int par_ref_levels = -1;
   Array<int> nxyz;
   int order = 1;
   int problem = 1; // 0 - mass, 1 - diffusion
   bool force_cuda_aware_mpi = false;
   bool visualization = 1;
   const bool required = true;

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.", required);
   args.AddOption(&ser_ref_levels, "-rs", "--refine-serial",
                  "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&par_ref_levels, "-rp", "--refine-parallel",
                  "Number of times to refine the mesh uniformly in parallel.");
   args.AddOption(&nxyz, "-c", "--cartesian-partitioning",
                  "Use Cartesian partitioning.");
   args.AddOption(&order, "-o", "--order",
                  "Finite element order (polynomial degree).");
   args.AddOption(&problem, "-p", "--problem",
                  "Problem type: 0 - mass, 1 - diffusion.");
   args.AddOption(&force_cuda_aware_mpi, "-cm", "--force-cuda-aware-mpi",
                  "-no-cm", "--dont-force-cuda-aware-mpi",
                  "Force/autodetect the use of CUDA-aware MPI.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
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

#ifdef MFEM_USE_BACKENDS
   /// Engine *engine = EngineDepot.Select(spec);

   // string occa_spec("mode: 'Serial'");
   string occa_spec;
   {
      stringstream occa_spec_ss;
      // const int nGPUs = 4;
      // occa_spec_ss << "mode: 'CUDA', device_id: " << (myid % nGPUs);
      occa_spec_ss << "mode: 'CUDA', device_id: 0";
      occa_spec = occa_spec_ss.str();
   }
   // string occa_spec("mode: 'OpenMP', threads: 4");
   // string occa_spec("mode: 'OpenCL', device_id: 0, platform_id: 0");

   SharedPtr<Engine> engine(new mfem::occa::Engine(MPI_COMM_WORLD, occa_spec));
   engine.As<mfem::occa::Engine>()->SetForceCudaAwareMPI(force_cuda_aware_mpi);
#endif

   // 3. Read the (serial) mesh from the given mesh file on all processors.
   Mesh *mesh = new Mesh(mesh_file, 1, 1);
#ifdef MFEM_USE_BACKENDS
   mesh->SetEngine(*engine);
#endif
   int dim = mesh->Dimension();

   // 4. Refine the serial mesh on all processors to increase the resolution.
   {
      int ref_levels = (int)floor(log(10000./mesh->GetNE())/log(2.)/dim);
      ref_levels = ser_ref_levels >= 0 ? ser_ref_levels : ref_levels;
      if (myid == 0)
      {
         cout << "Serial refinement levels: " << ref_levels << endl;
      }
      for (int l = 0; l < ref_levels; l++)
      {
         mesh->UniformRefinement();
      }
   }

   // 5. Define a parallel mesh by a partitioning of the serial mesh.
   MFEM_VERIFY(nxyz.Size() == 0 || nxyz.Size() == mesh->SpaceDimension(),
               "Expected " << mesh->SpaceDimension() << " integers with the "
               "option --cartesian-partitioning.");
   int *partitioning = nxyz.Size() ? mesh->CartesianPartitioning(nxyz) : NULL;
   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh, partitioning);
   delete [] partitioning;
   delete mesh;

   // 6. Refine the mesh further in parallel to increase the resolution.
   {
      par_ref_levels = par_ref_levels >= 0 ? par_ref_levels : 2;
      if (myid == 0)
      {
         cout << "Parallel refinement levels: " << par_ref_levels << endl;
      }
      for (int l = 0; l < par_ref_levels; l++)
      {
         pmesh->UniformRefinement();
      }
   }
   pmesh->PrintInfo(cout);
   if (myid == 0)
   {
      cout << "\nMesh refinement done. Elapsed time: "
           << MPI_Wtime()-start_time << " sec\n" << endl;
   }

   // 7. Define a parallel finite element space on the parallel mesh. Here we
   //    use continuous Lagrange finite elements of the specified order.
   FiniteElementCollection *fec;
   fec = new H1_FECollection(order, dim);
   ParFiniteElementSpace *fespace = new ParFiniteElementSpace(pmesh, fec);
   HYPRE_Int size = fespace->GlobalTrueVSize();
   if (myid == 0)
   {
      cout << "Number of finite element unknowns: " << size << endl;

      const FiniteElement &fe = *fespace->GetFE(0);
      const IntegrationRule *ir;
      if (problem == 0)
      {
         ElementTransformation &T = *pmesh->GetElementTransformation(0);
         ir = &MassIntegrator::GetRule(fe, fe, T, 0);
      }
      else
      {
         ir = &DiffusionIntegrator::GetRule(fe, fe);
      }
      cout << "Number of qudrature points per element = " << ir->GetNPoints()
           << '\n' << endl;
      cout << "ParFiniteElementSpace construction done. Elapsed time: "
           << MPI_Wtime()-start_time << " sec" << endl;
   }

   // 8. Define the list of true essential (boundary) dofs.
   Array<int> ess_tdof_list;
   if (pmesh->bdr_attributes.Size())
   {
      Array<int> ess_bdr(pmesh->bdr_attributes.Max());
      ess_bdr = 1;
      fespace->GetEssentialTrueDofs(ess_bdr, ess_tdof_list);
   }
   if (myid == 0)
   {
      cout << "\nBoundary conditions done. Elapsed time: "
           << MPI_Wtime()-start_time << " sec" << endl;
   }

   // 9. Set up the parallel linear form b(.) and assemble it. This is the
   //    right-hand side of the linear system.
   ParLinearForm *b = new ParLinearForm(fespace);
   ConstantCoefficient one(1.0);
   b->AddDomainIntegrator(new DomainLFIntegrator(one));
   b->Assemble();
   if (myid == 0)
   {
      cout << "\nRHS assembly done. Elapsed time: "
           << MPI_Wtime()-start_time << " sec" << endl;
   }

   // 10. Define the solution vector x on the fespace and initialize it with
   //     zero. These values will serve as the initial guess for the CG solver
   //     and as the essential boundary conditions.
   ParGridFunction x(fespace);
   x.Fill(0.0);

   // 11. Set up the parallel bilinear form a(.,.) on the finite element space.
   ParBilinearForm *a = new ParBilinearForm(fespace);
   if (problem == 0)
   {
      a->AddDomainIntegrator(new MassIntegrator(one));
   }
   else
   {
      a->AddDomainIntegrator(new DiffusionIntegrator(one));
   }

   // 12. Assemble the parallel bilinear form locally.
   a->Assemble();
   if (myid == 0)
   {
      cout << "\nOperator assembly done. Elapsed time: "
           << MPI_Wtime()-start_time << " sec" << endl;
   }

   // 13. Initialize the linear system components A, X, and B based on their
   //     FE counterparts a, x, and b.
   OperatorHandle A(Operator::ANY_TYPE);
   Vector B, X;
   a->FormLinearSystem(ess_tdof_list, x, *b, A, X, B);
   if (myid == 0)
   {
      cout << "\nFormLinearSystem() done. Elapsed time: "
           << MPI_Wtime()-start_time << " sec" << endl;
   }

   // 14. Set up the conjugate gradients (CG) solver.
   CGSolver *cg = new CGSolver(MPI_COMM_WORLD);
   cg->SetRelTol(1e-6);
   cg->SetAbsTol(0.0);
   const int max_iter = 200;
   const int print_level = 3;
   cg->SetMaxIter(max_iter);
   cg->SetPrintLevel(print_level);
   cg->SetOperator(*A.Ptr());

   // 15. Run one CG iteration to make sure all kernels are loaded before
   //     measuring time.
   if (myid == 0)
   {
      cout << "\nRunning 1 CG iteration to load all kernels ..." << flush;
   }
   {
      Vector X2(X);
      cg->SetMaxIter(1);
      cg->SetPrintLevel(-1);
      cg->Mult(B, X2);
      cg->SetMaxIter(max_iter);
      cg->SetPrintLevel(print_level);
   }
   if (myid == 0)
   {
      cout << " done.\n"
           << "Elapsed time: " << MPI_Wtime()-start_time << " sec\n\n"
           << "Solving the linear system using CG ..." << endl;
   }

   // 16. Run the full CG solver, measuring and reporting the execution time.
   double cg_start_time = MPI_Wtime();
   cg->Mult(B, X);
   double cg_end_time = MPI_Wtime();
   double loc_time = cg_end_time - cg_start_time;
   double max_time, min_time;
   MPI_Allreduce(&loc_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
   MPI_Allreduce(&loc_time, &min_time, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
   if (myid == 0)
   {
      cout << "\nCG time: " << max_time << " sec (min: " << min_time << " sec)"
           << "\nDOFs/sec in CG: "
           << 1e-6*size*cg->GetNumIterations()/max_time << " ("
           << 1e-6*size*cg->GetNumIterations()/min_time << ") million.\n"
           << endl;
   }

   // 17. Recover the parallel grid function x from the solution vector X. This
   //     is the local finite element solution on each processor.
   a->RecoverFEMSolution(X, *b, x);

   // 18. Copy x to the host memory when needed.
   x.Pull();

   // 19. Save the refined mesh and the solution in parallel. This output can
   //     be viewed later using GLVis: "glvis -np <np> -m mesh -g sol".
   if (visualization)
   {
      ostringstream mesh_name, sol_name;
      mesh_name << "mesh." << setfill('0') << setw(6) << myid;
      sol_name << "sol." << setfill('0') << setw(6) << myid;

      ofstream mesh_ofs(mesh_name.str().c_str());
      mesh_ofs.precision(8);
      pmesh->Print(mesh_ofs);

      ofstream sol_ofs(sol_name.str().c_str());
      sol_ofs.precision(8);
      x.Save(sol_ofs);
   }

   // 20. Send the solution by socket to a GLVis server.
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      socketstream sol_sock(vishost, visport);
      sol_sock << "parallel " << num_procs << " " << myid << "\n";
      sol_sock.precision(8);
      sol_sock << "solution\n" << *pmesh << x << flush;
   }

   // 21. Free the used memory.
   delete cg;
   delete a;
   delete b;
   delete fespace;
   delete fec;
   delete pmesh;
   if (myid == 0)
   {
      cout << "Memory freed. Elapsed time: "
           << MPI_Wtime()-start_time << " sec\n" << endl;
   }

   MPI_Finalize();

   return 0;
}
