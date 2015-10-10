//#include "Array.h"

#include "mpifftw++.h"
#include "mpiconvolution.h"
#include "mpiutils.h" // For output of distributed arrays

using namespace std;
using namespace fftwpp;

inline void init(double *f, split d) 
{
  unsigned int c=0;
  for(unsigned int i=0; i < d.x; ++i) {
    unsigned int ii=d.x0+i;
    for(unsigned int j=0; j < d.Y; j++) {
      f[c++]=j+ii;
    }
  }
}

int main(int argc, char* argv[])
{
#ifndef __SSE2__
  fftw::effort |= FFTW_NO_SIMD;
#endif

  unsigned int mx=4;
  unsigned int my=4;
  
  int divisor=1;
  int alltoall=1;
  convolveOptions options;
  options.xcompact=false;
  options.ycompact=false;
  options.mpi=mpiOptions(fftw::maxthreads,divisor,alltoall);
    
  unsigned int nx=2*mx-1+!options.xcompact;
  unsigned int ny=2*my-1+!options.ycompact;
  unsigned int nyp=my+!options.ycompact;
  
  int provided;
  MPI_Init_thread(&argc,&argv,MPI_THREAD_MULTIPLE,&provided);

  MPIgroup group(MPI_COMM_WORLD,nyp);

  if(group.size > 1 && provided < MPI_THREAD_FUNNELED)
    fftw::maxthreads=1;

  if(group.rank == 0) {
    cout << "Configuration: " 
	 << group.size << " nodes X " << fftw::maxthreads
	 << " threads/node" << endl;
  }

  if(group.rank < group.size) { 
    bool main=group.rank == 0;
    if(main) {
      cout << "mx=" << mx << ", my=" << my << endl;
      cout << "nx=" << nx << ", ny=" << ny << " " << nyp << nyp << endl;
    } 

     // Set up per-process dimensions
    split df(nx,ny,group.active);
    split dg(nx,nyp,group.active);
    split du(mx+options.xcompact,nyp,group.active);

    // Allocate complex-aligned memory
    double *f0=doubleAlign(df.n);
    double *f1=doubleAlign(df.n);
    Complex *g0=ComplexAlign(dg.n);
    Complex *g1=ComplexAlign(dg.n);

    // Create instance of FFT
    rcfft2dMPI rcfft(df,dg,f0,g0,options.mpi);

    // Create instance of convolution
    Complex *G[]={g0,g1};
    ImplicitHConvolution2MPI C(mx,my,dg,du,g0,2,1,
                               convolveOptions(options,fftw::maxthreads));
    
    if(main) cout << "\nDistributed input (split in x-direction):" << endl;
    if(main) cout << "f0:" << endl;
    init(f0,df);
    show(f0,df.x,df.Y,group.active);
    if(main) cout << "f1:" << endl;
    init(f1,df);
    show(f1,df.x,df.Y,group.active);
      
    if(main) cout << "\nDistributed output (split in y-direction:)" << endl;
    if(main) cout << "g0:" << endl;
    rcfft.Forwards0(f0,g0);
    show(g0,dg.X,dg.y,group.active);
    if(main) cout << "g1:" << endl;
    rcfft.Forwards0(f1,g1);
    show(g1,dg.X,dg.y,group.active);

    if(main) cout << "\nAfter convolution (split in y-direction):" << endl;
    C.convolve(G,multbinary);
    
    if(main) cout << "g0:" << endl;
//    for(unsigned int i=0; i < dg.y; ++i) g0[i]=0.0;
//    Array::array2<Complex> h(dg.X,dg.y,g0);
//    h[0][0]=... // We don't calculate the x Nyquuist modes!
    show(g0,dg.X,dg.y,group.active);

    if(main) cout << "\nTransformed back to real-space (split in x-direction):"
		  << endl;
    if(main) cout << "f0:" << endl;
    rcfft.Backwards0Normalized(g0,f0);
    show(f0,df.x,df.Y,group.active);

    deleteAlign(f0);
    deleteAlign(f1);
    deleteAlign(g0);
    deleteAlign(g1);
  }
  
  MPI_Finalize();
}
