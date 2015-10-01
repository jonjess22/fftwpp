#include <cstring>
#include "fftw++.h"

namespace fftwpp {

const double fftw::twopi=2.0*acos(-1.0);

// User settings:
unsigned int fftw::effort=FFTW_MEASURE;
const char *fftw::WisdomName="wisdom3.txt";
unsigned int fftw::maxthreads=1;
double fftw::testseconds=1.0; // Time limit for threading efficiency tests
fftw_plan (*fftw::planner)(fftw *f, Complex *in, Complex *out)=Planner;
char *fftw::Wisdom;

const char *fftw::oddshift="Shift is not implemented for odd nx";
const char *inout=
  "constructor and call must be both in place or both out of place";

fft1d::Table fft1d::threadtable;
mfft1d::Table mfft1d::threadtable;
rcfft1d::Table rcfft1d::threadtable;
crfft1d::Table crfft1d::threadtable;
mrcfft1d::Table mrcfft1d::threadtable;
mcrfft1d::Table mcrfft1d::threadtable;
fft2d::Table fft2d::threadtable;

void fftw::LoadWisdom()
{
  std::ifstream ifWisdom;
  ifWisdom.open(WisdomName);
  std::ostringstream wisdom;
  wisdom << ifWisdom.rdbuf();
  ifWisdom.close();
  fftw_import_wisdom_from_string(wisdom.str().c_str());
}

void fftw::SaveWisdom()
{
  static size_t WisdomLength=0;
  Wisdom=fftw_export_wisdom_to_string();
  size_t len=strlen(Wisdom);
  if(len != WisdomLength) {
    std::ofstream ofWisdom;
    ofWisdom.open(WisdomName);
    ofWisdom << Wisdom;
    ofWisdom.close();
    WisdomLength=len;
  }
}

fftw_plan Planner(fftw *F, Complex *in, Complex *out) {
  static bool Wise=false;
  if(!Wise) {
    fftw::LoadWisdom();
    Wise=true;
  }
  fftw_plan plan=F->Plan(in,out);
  fftw::SaveWisdom();
  fftw_free(fftw::Wisdom);
  return plan;
}
  
}
