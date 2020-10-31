// TODO:
// decouple work memory were possible
// optimize memory use
// use out-of-place transforms
// vectorize and optimize Zeta computations

#include <cassert>
#include <cfloat>

#include "convolution.h"
#include "utils.h"

using namespace std;
using namespace utils;
using namespace fftwpp;

unsigned int K=100; // Number of tests

// Constants used for initialization and testing.
const Complex I(0.0,1.0);
const double E=exp(1.0);
const Complex iF(sqrt(3.0),sqrt(7.0));
const Complex iG(sqrt(5.0),sqrt(11.0));

bool Test=false;

unsigned int A=2; // number of inputs
unsigned int B=1; // number of outputs

const unsigned int Nsize=1000; // FIXME
unsigned int nsize=1000;
unsigned int size[Nsize];

class FFTpad {
protected:
  unsigned int L;
  unsigned int M;
  unsigned int m;
  unsigned int p;
  unsigned int q;
  fft1d *fftM;
  mfft1d *fftm;
  Complex *ZetaH,*ZetaL;
  Complex *g,*H,*G;
  utils::statistics S;
  FFTpad *inner;
  bool innerFFT;
public:

  void init(Complex *f) {
//    cout << "m=" << m << endl;
    if(p == q)
      fftM=new fft1d(M,1);
    else {
      unsigned int N=q*m;
      BuildZeta(N,N,ZetaH,ZetaL,1,N);//,threads);
      unsigned int n=q/p;
      G=ComplexAlign(N);
      innerFFT=p > 1 && n*p == q;
      if(innerFFT) {
//                         L,M,m,q
        inner=new FFTpad(f,p,q,p,n);
        fftm=new mfft1d(m,1,q,q,q,1,1,G,G);
      } else {
        fftm=new mfft1d(m,1,1,1,q,0,0,f,G);
        deleteAlign(G);
      }

      H=ComplexAlign(M);
      g=ComplexAlign(m);
    }
  }

  // Compute an fft padded to N=q*m >= M >= L=f.length
  FFTpad(Complex *f, unsigned int L, unsigned int M,
         unsigned int m, unsigned int q) :
    L(L), M(M), m(m), p(ceilquotient(L,m)), q(q) {
    init(f);
  }

  ~FFTpad() {
    if(p == q)
      delete fftM;
    else {
      deleteAlign(ZetaL);
      deleteAlign(ZetaH);
      if(innerFFT) {
        deleteAlign(G);
        delete inner;
      }
      deleteAlign(g);
      deleteAlign(H);
      delete fftm;
    }
  }

  class Opt {
  public:
    unsigned int m,q;

    // Determine optimal m,q values for padding L data values to
    // size >= M
    // If fixed=true then an FFT of size M is enforced.
    Opt(Complex *f, unsigned int L, unsigned int M, bool fixed=false)
    {
      assert(L <= M);
      m=M;
      q=1;
      double T=DBL_MAX;
      unsigned int i=0;

      while(i < nsize) {
        unsigned int m=size[i];
//        cout << "m=" << m << endl;
        if(!fixed || M % m == 0) {
          if(m > L) break; // Assume size 2 FFT is in table
          unsigned int p=ceilquotient(L,m);
          unsigned int q=ceilquotient(M,m);

          Complex *F=NULL;
          if(!fixed) {
            unsigned int q2=p*ceilquotient(M,m*p);
            if(q2 != q) {
              FFTpad fft(f,L,M,m,q2);
              Complex *F=ComplexAlign(fft.length());
              double t=fft.meantime(f,F,K);

              if(t < T) {
                this->m=m;
                this->q=q2;
                T=t;
              }
            }
          }

          FFTpad fft(f,L,M,m,q);
          if(!F) F=ComplexAlign(fft.length());
          double t=fft.meantime(f,F,K);
          utils::deleteAlign(F);

          if(t < T) {
            this->m=m;
            this->q=q;
            T=t;
          }
        }
        ++i;
      }

      cout << "Optimal values:" << endl;
      cout << "m=" << m << endl;
      cout << "p=" << ceilquotient(L,m) << endl;
      cout << "q=" << q << endl;
    }
  };

  // Normal entry point.
  // Compute an fft of length L padded to at least M
  // (or exactly M if fixed=true)
  FFTpad(Complex *f, unsigned int L, unsigned int M, bool fixed=false) :
    L(L), M(M) {
    Opt opt=Opt(f,L,M,fixed);
    m=opt.m;
    p=ceilquotient(L,m);
    q=opt.q;
    init(f);
  }

  void forwards(Complex *f, Complex *F) {
    if(p == q) {
      for(unsigned int i=0; i < L; ++i)
        F[i]=f[i];
      for(unsigned int i=L; i < M; ++i)
        F[i]=0.0;
      fftM->fft(F);
      return;
    }

    for(unsigned int i=0; i < L; ++i)
      H[i]=f[i];
    unsigned int pm=p*m;
    for(unsigned int i=L; i < pm; ++i)
      H[i]=0.0;

    if(innerFFT) {
      for(unsigned int s=0; s < m; ++s) {
        for(unsigned int t=0; t < p; ++t)
          G[t]=H[t*m+s];
        Complex *Fsq=F+s*q;
        inner->forwards(G,Fsq);
        for(unsigned int r=0; r < q; ++r)
          Fsq[r] *= ZetaL[r*s];
      }
      fftm->fft(F);
    } else {
      if(p == 1) {
        fftm->fft(H,F);
        for(unsigned int r=1; r < q; ++r) {
          for(unsigned int s=0; s < m; ++s) {
            unsigned int c=r*s;
//          unsigned int a=c/S;
//          Complex Zeta=ZetaH[a]*ZetaL[c-S*a];
            Complex Zeta=ZetaL[c];
            g[s]=Zeta*H[s];
          }
          fftm->fft(g,F+r);
        }
      } else {
        for(unsigned int s=0; s < m; ++s) {
          Complex sum=0.0;
          for(unsigned int t=0; t < p; ++t)
            sum += H[t*m+s];
          g[s]=sum;
        }
        fftm->fft(g,F);
        
        unsigned int N=q*m;
        for(unsigned int r=1; r < q; ++r) {
          for(unsigned int s=0; s < m; ++s) {
            Complex sum=0.0;
            for(unsigned int t=0; t < p; ++t) {
              unsigned int j=t*m+s;
              unsigned int c=(r*j) % N;
//          unsigned int a=c/S;
//          Complex Zeta=ZetaH[a]*ZetaL[c-S*a];
              Complex Zeta=ZetaL[c];
              sum += Zeta*H[j];
            }
            g[s]=sum;
          }
          fftm->fft(g,F+r);
        }
      }
    }
  }

  unsigned int padding() {
    return p*m-L;
  }
  unsigned int length() {
    return p == q ? M : m*q;
  }

  double meantime(Complex *f, Complex *F, unsigned int K,
                  double *stdev=NULL) {
    S.clear();
    for(unsigned int j=0; j < L; ++j)
      f[j]=j;
    forwards(f,F); // Create wisdom

    for(unsigned int i=0; i < K; ++i) {
      for(unsigned int j=0; j < L; ++j)
        f[j]=j;
      double t0=utils::totalseconds();
      forwards(f,F);
      double t=utils::totalseconds();
      S.add(t-t0);
    }
    if(stdev) *stdev=S.stdev();
    return S.mean();
  }
};



inline void init(Complex **F, unsigned int m, unsigned int A)
{
  if(A % 2 == 0) {
    unsigned int M=A/2;
    double factor=1.0/sqrt((double) M);
    for(unsigned int s=0; s < M; ++s) {
      double ffactor=(1.0+s)*factor;
      double gfactor=1.0/(1.0+s)*factor;
      Complex *fs=F[s];
      Complex *gs=F[s+M];
      if(Test) {
        for(unsigned int k=0; k < m; k++) {
          fs[k]=factor*iF*pow(E,k*I);
          gs[k]=factor*iG*pow(E,k*I);
        }
      } else {
        for(unsigned int k=0; k < m; k++) {
          fs[k]=ffactor*Complex(k,k+1);
          gs[k]=gfactor*Complex(k,2*k+1);
        }
      }
    }
  } else {
    for(unsigned int a=0; a < A; ++a) {
      for(unsigned int k=0; k < m; ++k) {
        F[a][k]=(a+1)*Complex(k,k+1);
      }
    }
  }
}

// Pair-wise binary multiply for A=2 or A=4.
// NB: example function, not optimised or threaded.
void multA(Complex **F, unsigned int m,
           const unsigned int indexsize,
           const unsigned int *index,
           unsigned int r, unsigned int threads)
{
  switch(A) {
    case 2: multbinary(F,m,indexsize,index,r,threads); break;
    case 4: multbinary2(F,m,indexsize,index,r,threads); break;
    default:
      cerr << "A=" << A << " is not yet implemented" << endl;
      exit(1);
  }

  for(unsigned int b=1; b < B; ++b) {
    double factor=1.0+b;
    for(unsigned int i=0; i < m; ++i) {
      F[b][i]=factor*F[0][i];
    }
  }
}

unsigned L,M;

double report(FFTpad &fft, Complex *f, Complex *F)
{
  double stdev;
  cout << endl;

  unsigned int K=100000;
  double mean=fft.meantime(f,F,K,&stdev);

  cout << "mean=" << mean << " +/- " << stdev << endl;

  unsigned int N=fft.length();
  if(N < 20) {
    for(unsigned int i=0; i < N; ++i)
      cout << F[i] << endl;
  }
  cout << endl;

  return mean;
}

int main(int argc, char* argv[])
{
  fftw::maxthreads=1;//get_max_threads();

  const char *name="optimalSorted.dat";
  ifstream fin(name);
  if(!fin) {
    cerr << name << " not found" << endl;
    exit(-1);
  }
  nsize=0;
  while(true) {
    unsigned int i;
    double mean,stdev;
    fin >> i >> mean >> stdev;
    if(fin.eof()) break;
    size[nsize]=i;
    ++nsize;
  }

  L=3;
  M=200;

  /*
    L=1000;
    M=7099;
  */

  /*
    L=512;
    M=2*L;
  */

  L=683;
  M=1025;

  /*
    L=1810;
    M=109090;
  */


  /*
    L=11111;//11;
    M=2*L;
  */

  /*
    L=683;
    M=1024;
  */

  /*
    L=13;
    M=16;
  */

  cout << "L=" << L << endl;
  cout << "M=" << M << endl;

  Complex *f=ComplexAlign(L);

  // Explicit padding
  FFTpad fft0(f,L,M,M,1);

  Complex *F0=ComplexAlign(M);
  double mean0=report(fft0,f,F0);
  deleteAlign(F0);

  // Hybrid padding
  FFTpad fft(f,L,M);

  cout << "Padding:" << fft.padding() << endl;

  unsigned int N=fft.length();
  Complex *F=ComplexAlign(N);

  double mean1=report(fft,f,F);

  cout << endl;
  if(mean0 > 0)
    cout << "ratio=" << mean1/mean0 << endl;
  cout << endl;

  Complex *F1=ComplexAlign(N);
  FFTpad fft1(f,L,N,N,1);
  for(unsigned int j=0; j < L; ++j)
    f[j]=j;
  fft1.forwards(f,F1);

  double error=0.0;
  double norm=0.0;
  for(unsigned int i=0; i < N; i++) {
    error += abs2(F[i]-F1[i]);
    norm += abs2(F1[i]);
  }

  if(norm > 0) error=sqrt(error/norm);
  cout << "error=" << error << endl;
  if (error > 1e-12)
    cerr << endl << "WARNING: error=" << error << endl;

  return 0;
}
