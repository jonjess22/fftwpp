// TODO:
// Add strides
//
// Can user request allowing overlap of input and output arrays,
// for possibly reduced performance?

#include <cfloat>
#include <climits>

#include "convolution.h"
#include "utils.h"

using namespace std;
using namespace utils;
using namespace fftwpp;

// Constants used for initialization and testing.
const Complex I(0.0,1.0);
const double E=exp(1.0);
const Complex iF(sqrt(3.0),sqrt(7.0));
const Complex iG(sqrt(5.0),sqrt(11.0));

bool Test=false;

unsigned int mOption=0;
unsigned int DOption=0;

unsigned int A=2; // number of inputs
unsigned int B=1; // number of outputs


unsigned int surplusFFTsizes=25;

#ifndef _GNU_SOURCE
inline void sincos(const double x, double *sinx, double *cosx)
{
  *sinx=sin(x); *cosx=cos(x);
}
#endif

inline Complex expi(double phase)
{
  double cosy,siny;
  sincos(phase,&siny,&cosy);
  return Complex(cosy,siny);
}

template<class T>
T pow(T x, unsigned int y)
{
  if(y == 0) return 1;
  if(x == 0) return 0;

  unsigned int r = 1;
  while(true) {
    if(y & 1) r *= x;
    if((y >>= 1) == 0) return r;
    x *= x;
  }
}

unsigned int nextfftsize(unsigned int m)
{
  unsigned int N=-1;
  unsigned int ni=1;
  for(unsigned int i=0; ni < 7*m; ni=pow(7,i), ++i) {
    unsigned int nj=ni;
    for(unsigned int j=0; nj < 5*m; nj=ni*pow(5,j), ++j) {
      unsigned int nk=nj;
      for(unsigned int k=0; nk < 3*m; nk=nj*pow(3,k), ++k) {
        N=min(N,nk*ceilpow2(ceilquotient(m,nk)));
      }
    }
  }
  return N;
}

// This multiplication routine is for binary convolutions and takes two inputs
// of size m.
// F0[j] *= F1[j];
void multbinary(Complex *F0, Complex *F1,unsigned int m,
                unsigned int threads=1)
{
#ifdef __SSE2__
  PARALLEL(
    for(unsigned int j=0; j < m; ++j) {
      Complex *p=F0+j;
      STORE(p,ZMULT(LOAD(p),LOAD(F1+j)));
    }
    );
#else
  PARALLEL(
    for(unsigned int j=0; j < m; ++j)
      F0[jg] *= F1[j];
    );
#endif
}

class FFTpad {
public:
  unsigned int L;
  unsigned int M;
  unsigned int m;
  unsigned int p;
  unsigned int q;
  unsigned int n;
  unsigned int Q;
  unsigned int D;
  Complex *W0; // Temporary work memory for testing accuracy
protected:
  fft1d *fftM,*ifftM;
  mfft1d *fftm,*ifftm;
  mfft1d *fftm2,*ifftm2;
  mfft1d *fftp,*ifftp;
  Complex *Zetaqp;
  Complex *Zetaqm;
  utils::statistics S;
public:

  void init() {
    p=ceilquotient(L,m);
    n=q/p;
    if(m > M) M=m;
    if(q == 1) {
      fftM=new fft1d(M,1);
      ifftM=new fft1d(M,-1);
    } else {
      unsigned int N=m*q;
      double twopibyN=twopi/N;

      if(p > 1) {
        Q=n;
        Zetaqp=ComplexAlign((n-1)*(p-1))-p;
        double twopibyq=twopi/q;
        for(unsigned int r=1; r < n; ++r)
          for(unsigned int t=1; t < p; ++t)
            Zetaqp[p*r-r+t]=expi(r*t*twopibyq);
      } else {
        Q=q;
        Zetaqp=ComplexAlign((q-1)*(p-1))-p;
        for(unsigned int r=1; r < q; ++r)
          for(unsigned int t=1; t < p; ++t)
            Zetaqp[p*r-r+t]=expi((m*r*t % N)*twopibyN);
      }

      unsigned int d=p*D;
      Complex *G=ComplexAlign(m*d);
      Complex *H=ComplexAlign(m*d);

      fftm=new mfft1d(m,1,d, 1,1, m,m, G,H);
      ifftm=new mfft1d(m,-1,d, 1,1, m,m, G,H);

      unsigned int extra=Q % D;
      if(extra > 0) {
        d=p*extra;
        fftm2=new mfft1d(m,1,d, 1,1, m,m, G,H);
        ifftm2=new mfft1d(m,-1,d, 1,1, m,m, G,H);
      }

      if(p > 1) {// L'=p, M'=q, m'=p, p'=1, q'=n
        fftp=new mfft1d(p,1,m, m,m, 1,1, G,G);
        ifftp=new mfft1d(p,-1,m, m,m, 1,1, G,G);
      }

      deleteAlign(H);
      deleteAlign(G);

      Zetaqm=ComplexAlign((q-1)*(m-1))-m;
      for(unsigned int r=1; r < q; ++r)
        for(unsigned int s=1; s < m; ++s)
          Zetaqm[m*r-r+s]=expi(r*s*twopibyN);

    }
  }

  // Compute an fft padded to N=m*q >= M >= L=f.length
  FFTpad(unsigned int L, unsigned int M,
         unsigned int m, unsigned int q, unsigned int D) :
    L(L), M(M), m(m), p(ceilquotient(L,m)), q(q), D(D) {
    init();
  }

  ~FFTpad() {
    if(q == 1) {
      delete fftM;
      delete ifftM;
    } else {
      if(p > 1) {
        delete fftp;
        delete ifftp;
      }
      deleteAlign(Zetaqp+p);
      deleteAlign(Zetaqm+m);
      delete fftm;
      delete ifftm;
      if(Q % D > 0) {
        delete fftm2;
        delete ifftm2;
      }
    }
  }

  class Opt {
  public:
    unsigned int m,q,D;
    double T;

    void check(unsigned int L, unsigned int M,
               unsigned int m, bool fixed=false) {
//      cout << "m=" << m << endl;
      unsigned int p=ceilquotient(L,m);
      unsigned int q=ceilquotient(M,m);

      if(p == q && p > 1) return;

      if(!fixed) {
        unsigned int n=ceilquotient(M,m*p);
        unsigned int q2=p*n;
        if(q2 != q) {
          unsigned int start=DOption > 0 ? min(DOption,n) : 1;
          unsigned int stop=DOption > 0 ? min(DOption,n) : n;
          if(fixed) start=stop=1;
          for(unsigned int D=start; D <= stop; D *= 2) {
            if(2*D > stop) D=stop;
//            cout << "q2=" << q2 << endl;
//            cout << "D=" << D << endl;

            FFTpad fft(L,M,m,q2,D);
            double t=fft.meantime();
            if(t < T) {
              this->m=m;
              this->q=q2;
              this->D=D;
              T=t;
            }
          }
        }
      }

      if(q % p != 0) return;

      unsigned int start=DOption > 0 ? min(DOption,q) : 1;
      unsigned int stop=DOption > 0 ? min(DOption,q) : q;
      if(fixed) start=stop=1;
      for(unsigned int D=start; D <= stop; D *= 2) {
        if(2*D > stop) D=stop;
//        cout << "D=" << D << endl;
        FFTpad fft(L,M,m,q,D);
        double t=fft.meantime();

        if(t < T) {
          this->m=m;
          this->q=q;
          this->D=D;
          T=t;
        }
      }
    }

    // Determine optimal m,q values for padding L data values to
    // size >= M
    // If fixed=true then an FFT of size M is enforced.
    Opt(unsigned int L, unsigned int M, bool Explicit=false, bool fixed=false)
    {
      if(L > M) {
        cerr << "L=" << L << " is greater than M=" << M << "." << endl;
        exit(-1);
      }
      m=M;
      q=1;
      D=1;

      if(Explicit && fixed)
        return;

      T=DBL_MAX;
      unsigned int i=0;

      unsigned int stop=M-1;
      for(unsigned int k=0; k < surplusFFTsizes; ++k)
        stop=nextfftsize(stop+1);

      unsigned int m0=1;

      if(mOption >= 1 && !Explicit)
        check(L,M,mOption,fixed);
      else
        while(true) {
          m0=nextfftsize(m0+1);
          if(Explicit) {
            if(m0 > stop) break;
            if(m0 < M) {++i; continue;}
            M=m0;
          } else if(m0 > stop) break;
//        } else if(m0 > L) break;
          if(!fixed || Explicit || M % m0 == 0)
            check(L,M,m0,fixed || Explicit);
          ++i;
        }

      unsigned int p=ceilquotient(L,m);
      cout << endl;
      cout << "Optimal values:" << endl;
      cout << "m=" << m << endl;
      cout << "p=" << p << endl;
      cout << "q=" << q << endl;
      cout << "D=" << D << endl;
      cout << "Padding:" << m*p-L << endl;
    }
  };

  // Normal entry point.
  // Compute an fft of length L padded to at least M
  // (or exactly M if fixed=true)
  FFTpad(unsigned int L, unsigned int M, bool Explicit=false, bool fixed=false) :
    L(L), M(M) {
    Opt opt=Opt(L,M,Explicit,fixed);
    m=opt.m;
    if(Explicit) this->M=M=m;
    p=ceilquotient(L,M);
    q=opt.q;
    D=opt.D;
    init();
  }

  // Explicitly pad to m.
  void pad(Complex *W) {
    for(unsigned int d=0; d < D; ++d) {
      Complex *F=W+m*d;
      for(unsigned int s=L; s < m; ++s)
        F[s]=0.0;
    }
  }

  // TODO: Check for cases when arrays f and F must be distinct
  void forward(Complex *f, Complex *F) {
    if(q == 1) {
      for(unsigned int i=0; i < L; ++i)
        F[i]=f[i];
      for(unsigned int i=L; i < M; ++i)
        F[i]=0.0;
      fftM->fft(F);
    } else {
      if(p == 1 && L < m)
        pad(W0);
      unsigned int b=m*p;
      for(unsigned int r=0; r < Q; r += D)
        forward(f,F+b*r,r,W0);
    }
  }

  void backward(Complex *F, Complex *f) {
    if(q == 1) {
      ifftM->fft(F);
      for(unsigned int i=0; i < L; ++i)
        f[i]=F[i];
    } else {
      unsigned int b=m*p;
      for(unsigned int r=0; r < Q; r += D)
        backward(F+b*r,f,r,W0);
    }
  }

  void forward(Complex *f, Complex *F0, unsigned int r0, Complex *W) {
    unsigned int D0=r0+D > Q ? Q-r0 : D;
    unsigned int first=r0 == 0;
    unsigned int stop=min(L,m);
    if(p > 1) {
      if(first) {
        for(unsigned int t=0; m*t < L; ++t) {
          unsigned int mt=m*t;
          Complex *Ft=W+mt;
          Complex *ft=f+mt;
          unsigned int stop=min(L-mt,m);
          for(unsigned int s=0; s < stop; ++s)
            Ft[s]=ft[s];
          for(unsigned int s=stop; s < m; ++s)
            Ft[s]=0.0;
        }
        fftp->fft(W);
        for(unsigned int t=1; t < p; ++t) {
          unsigned int R=n*t;
          Complex *Ft=W+m*t;
          Complex *Zetar=Zetaqm+m*R-R;
          for(unsigned int s=1; s < m; ++s)
            Ft[s] *= Zetar[s];
        }
      }

      unsigned int b=m*p;
      for(unsigned int d=first; d < D0; ++d) {
        Complex *F=W+b*d;
        unsigned int r=r0+d;
        for(unsigned int s=0; s < stop; ++s)
          F[s]=f[s];
        for(unsigned int s=stop; s < m; ++s)
          F[s]=0.0;
        Complex *Zetaqr=Zetaqp+p*r-r;
        for(unsigned int t=1; m*t < L; ++t) {
          unsigned int mt=m*t;
          Complex *Ft=F+mt;
          Complex *ft=f+mt;
          unsigned int stop=min(L-mt,m);
          Complex Zeta=Zetaqr[t];
          for(unsigned int s=0; s < stop; ++s)
            Ft[s]=Zeta*ft[s];
          for(unsigned int s=stop; s < m; ++s)
            Ft[s]=0.0;
        }
        fftp->fft(F);
        for(unsigned int t=0; t < p; ++t) {
          unsigned int R=n*t+r;
          Complex *Ft=F+m*t;
          Complex *Zetar=Zetaqm+m*R-R;
          for(unsigned int s=1; s < m; ++s)
            Ft[s] *= Zetar[s];
        }
      }
    } else {
      if(first) {
        for(unsigned int i=0; i < L; ++i)
          W[i]=f[i];
      }
      for(unsigned int d=first; d < D0; ++d) {
        Complex *F=W+m*d;
        unsigned int r=r0+d;
        F[0]=f[0];
        Complex *Zetar=Zetaqm+m*r-r;
        for(unsigned int s=1; s < stop; ++s)
          F[s]=Zetar[s]*f[s];
      }
    }
    (D0 == D ? fftm : fftm2)->fft(W,F0);
  }

// Compute an inverse fft of length N=m*q unpadded back
// to size m*p >= L.
//    if(F0 == f) {
//      cerr << "input and output arrays must be distinct"
//           << endl;
//    }
  // Input F destroyed
  void backward(Complex *F0, Complex *f, unsigned int r0, Complex *W) {
    unsigned int D0=r0+D > Q ? Q-r0 : D;
    (D0 == D ? ifftm : ifftm2)->fft(F0,W);
    unsigned int first=r0 == 0;

    if(p > 1) {
      if(first) {
        for(unsigned int t=1; t < p; ++t) {
          unsigned int R=n*t;
          Complex *Ft=W+m*t;
          Complex *Zetar=Zetaqm+m*R-R;
          for(unsigned int s=1; s < m; ++s)
            Ft[s] *= conj(Zetar[s]);
        }
        ifftp->fft(W);
        for(unsigned int t=0; t < p; ++t) {
          unsigned int mt=m*t;
          Complex *Ft=W+mt;
          Complex *ft=f+mt;
          for(unsigned int s=0; s < m; ++s)
            ft[s]=Ft[s];
        }
      }

      unsigned int b=m*p;
      for(unsigned int d=first; d < D0; ++d) {
        Complex *F=W+b*d;
        unsigned int r=r0+d;
        for(unsigned int t=0; t < p; ++t) {
          unsigned int R=n*t+r;
          Complex *Ft=F+m*t;
          Complex *Zetar=Zetaqm+m*R-R;
          for(unsigned int s=1; s < m; ++s)
            Ft[s] *= conj(Zetar[s]);
        }
        ifftp->fft(F);
        for(unsigned int s=0; s < m; ++s)
          f[s] += F[s];
        Complex *Zetaqr=Zetaqp+p*r-r;
        for(unsigned int t=1; t < p; ++t) {
          unsigned int mt=m*t;
          Complex *Ft=F+mt;
          Complex *ft=f+mt;
          Complex Zeta=conj(Zetaqr[t]);
          for(unsigned int s=0; s < m; ++s)
            ft[s] += Zeta*Ft[s];
        }
      }
    } else {
      if(first) {
        for(unsigned int s=0; s < m; ++s)
          f[s]=W[s];
      }
      for(unsigned int d=first; d < D0; ++d) {
        Complex *F=W+m*d;
        unsigned int r=r0+d;
        f[0] += F[0];
        Complex *Zetamr=Zetaqm+m*r-r;
        for(unsigned int s=1; s < m; ++s)
          f[s] += F[s]*conj(Zetamr[s]);
      }
    }
  }

  unsigned int inverseLength() {
    return q == 1 ? L : m*p;
  }

  unsigned int length() {
    return q == 1 ? M : m*q;
  }

  unsigned int blocksize() {
    return q == 1 ? M : m*p*D;
  }

  double meantime(double *Stdev=NULL) {
    S.clear();
    unsigned int b=inverseLength();
    unsigned int B=blocksize();

    Complex *f=ComplexAlign(b);
    Complex *g=ComplexAlign(b);
    bool loop2=Q == 2 && D == 1;
    Complex *h=D < Q && !loop2 ? ComplexAlign(b) : f;

    Complex *F=ComplexAlign(B);
    Complex *G=ComplexAlign(B);
    Complex *W=ComplexAlign(B);

// Assume f != F (out-of-place)
    for(unsigned int j=0; j < L; ++j) {
      f[j]=0.0;
      g[j]=0.0;
    }

    unsigned int K=1;
    double eps=0.1;
    unsigned int N=length();
    double scale=1.0/N;

    for(;;) {
      double t0,t;
      if(q == 1) {
        t0=totalseconds();
        for(unsigned int i=0; i < K; ++i) {

#define OUTPUT 0
#if OUTPUT
          for(unsigned int j=0; j < L; ++j) {
            f[j]=Complex(j,j+1);
            g[j]=Complex(j,2*j+1);
          }
#endif
          forward(f,F);
          forward(g,G);
          for(unsigned int i=0; i < N; ++i)
            F[i] *= G[i];
          backward(F,f);
          for(unsigned int i=0; i < L; ++i)
            f[i] *= scale;
        }
        t=totalseconds();
      } else {
        if(p == 1 && L < m)
          pad(W);

        t0=totalseconds();
        for(unsigned int i=0; i < K; ++i) {
#if OUTPUT
          for(unsigned int j=0; j < L; ++j) {
            f[j]=Complex(j,j+1);
            g[j]=Complex(j,2*j+1);
          }
#endif
          if(loop2) {
            forward(f,F,0,W);
            forward(g,G,0,W);
            for(unsigned int i=0; i < B; ++i)
              G[i] *= F[i];
            forward(f,F,1,W);
            backward(G,f,0,W);
            if(p == 1 && L < m)
              pad(W);
            forward(g,G,1,W);
            for(unsigned int i=0; i < B; ++i)
              G[i] *= F[i];
            backward(G,f,1,F);
            for(unsigned int i=0; i < L; ++i)
              f[i] *= scale;
          } else {
            for(unsigned int r=0; r < Q; r += D) {
              forward(f,F,r,W);
              forward(g,G,r,W);
              for(unsigned int i=0; i < B; ++i)
                F[i] *= G[i];
              backward(F,h,r,G);
            }
            for(unsigned int i=0; i < L; ++i)
              f[i]=h[i]*scale;
          }
        }
        t=totalseconds();
      }
      S.add(t-t0);

      double mean=S.mean();
      double stdev=S.stdev();
      if(S.count() < 7) continue;
      int threshold=5000;
      if(mean*CLOCKS_PER_SEC < threshold || eps*mean < stdev) {
        K *= 2;
        S.clear();
      } else {
        if(Stdev) *Stdev=stdev/K;
#if OUTPUT
        for(unsigned int i=0; i < L; ++i)
          cout << f[i] << endl;
#endif

        deleteAlign(F);
        deleteAlign(f);
        deleteAlign(G);
        deleteAlign(g);
        deleteAlign(W);
        if(D < Q && !loop2)
          deleteAlign(h);
        return mean/K;
      }
    }
    return 0.0;
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

unsigned int L=683;
unsigned int M=1025;

double report(FFTpad &fft)
{
  double stdev;
  cout << endl;

  double mean=fft.meantime(&stdev);

  cout << "mean=" << mean << " +/- " << stdev << endl;

  return mean;
}

void usage()
{
  std::cerr << "Options: " << std::endl;
  std::cerr << "-h\t\t help" << std::endl;
  std::cerr << "-m\t\t subtransform size" << std::endl;
  std::cerr << "-D\t\t number of blocks to process at a time" << std::endl;
  std::cerr << "-S\t\t number of surplus FFT sizes" << std::endl;
  std::cerr << "-L\t\t number of physical data values" << std::endl;
  std::cerr << "-M\t\t minimal number of padded data values" << std::endl;
  std::cerr << "-T\t\t number of threads" << std::endl;
}

int main(int argc, char* argv[])
{
  fftw::maxthreads=1;//get_max_threads();

#ifndef __SSE2__
  fftw::effort |= FFTW_NO_SIMD;
#endif

#ifdef __GNUC__
  optind=0;
#endif
  for (;;) {
    int c = getopt(argc,argv,"hD:L:M:S:T:m:");
    if (c == -1) break;

    switch (c) {
      case 0:
        break;
      case 'D':
        DOption=max(atoi(optarg),0);
        break;
      case 'L':
        L=atoi(optarg);
        break;
      case 'M':
        M=atoi(optarg);
        break;
      case 'S':
        surplusFFTsizes=atoi(optarg);
        break;
      case 'T':
        fftw::maxthreads=max(atoi(optarg),1);
        break;
      case 'm':
        mOption=max(atoi(optarg),0);
        break;
      case 'h':
      default:
        usage();
        exit(1);
    }
  }

  cout << "L=" << L << endl;
  cout << "M=" << M << endl;

  cout << "Explicit:" << endl;
  // Minimal explicit padding
  FFTpad fft0(L,M,true,true);
  double mean0=report(fft0);

  // Optimal explicit padding
  FFTpad fft1(L,M,true,false);
  double mean1=min(mean0,report(fft1));

  // Hybrid padding
  FFTpad fft(L,M);
  double mean=report(fft);

  if(mean0 > 0)
    cout << "minimal ratio=" << mean/mean0 << endl;
  cout << endl;

  if(mean1 > 0)
    cout << "optimal ratio=" << mean/mean1 << endl;
  cout << endl;

  unsigned int N=fft.length();

  Complex *f=ComplexAlign(L);
  Complex *F=ComplexAlign(N);
  fft.W0=ComplexAlign(fft.blocksize());

  for(unsigned int j=0; j < L; ++j)
    f[j]=j+1;
  fft.forward(f,F);

  Complex *f0=ComplexAlign(fft.inverseLength());
  Complex *F0=ComplexAlign(N);

  for(unsigned int j=0; j < fft.length(); ++j)
    F0[j]=F[j];

  fft.backward(F0,f0);

  if(L < 30) {
    cout << endl;
    cout << "Inverse:" << endl;
    unsigned int N=fft.length();
    for(unsigned int j=0; j < L; ++j)
      cout << f0[j]/N << endl;
    cout << endl;
  }

  Complex *F2=ComplexAlign(N);
  FFTpad fft2(L,N,N,1,1);
  for(unsigned int j=0; j < L; ++j)
    f[j]=j+1;
  fft2.forward(f,F2);

  double error=0.0, norm=0.0;
  double error2=0.0, norm2=0.0;

  unsigned int i=0;
  unsigned int m=fft.m;
  unsigned int p=fft.p;
  unsigned int q=fft.q;
  unsigned int n=fft.n;

  if(q == 1) {
    for(unsigned int k=0; k < N; ++k) {
      error += abs2(F[i]-F2[i]);
      norm += abs2(F2[i]);
      ++i;
    }
  } else {
    for(unsigned int s=0; s < m; ++s) {
      for(unsigned int t=0; t < p; ++t) {
        for(unsigned int r=0; r < n; ++r) {
          error += abs2(F[m*(p*r+t)+s]-F2[i]);
          norm += abs2(F2[i]);
          ++i;
        }
      }
    }
  }

  for(unsigned int j=0; j < L; ++j) {
    error2 += abs2(f0[j]/N-f[j]);
    norm2 += abs2(f[j]);
  }

  if(norm > 0) error=sqrt(error/norm);
  double eps=1e-12;
  if(error > eps || error2 > eps)
    cerr << endl << "WARNING: " << endl;
  cout << "forward error=" << error << endl;
  cout << "backward error=" << error2 << endl;

  return 0;
}
