import graph;

size(15cm,10cm,IgnoreAspect);

file in=input("optimal.dat").line();
real[][] a=in;
a=sort(a);
a=transpose(a);

real[] size=a[0];
real[] mean=a[1];

//int[] P={13,11,7,5,3,2};
int[] P={7,5,3,2};

// Return the smallest power of 2 greater than or equal to n.
int ceilpow2(int n)
{
  --n;

  n=OR(n,n#2);
  n=OR(n,n#4);
  n=OR(n,n#16);
  n=OR(n,n#256);
  n=OR(n,n#65536);
  n=OR(n,n#4294967296);

  return ++n;
}

for(int i=0; i < size.length; ++i) {
  int m=(int) size[i];
  int M=m;
  real Mean=mean[i];
  pair z=(m,Mean);
  for(int p : P) {
    //    if(p >= 11 && (m % 121 == 0 || m % 143 == 0 || m % 169 == 0)) continue;
    if(m % p == 0) {
      while(m % p == 0)
        m #= p;
      frame f;
      fill(f,scale(0.5*p^0.6)*unitcircle,Pen(p));
      add(currentpicture,f,z);
    }
  }
  //  if(m > 1) {
  int j=find(size >= ceilpow2(M));
  if(j == -1) j=mean.length-1;
  if(Mean <= min(mean[i:j+1])) {
    frame f;
    fill(f,unitcircle,black);
    add(currentpicture,f,z);
    ++count;
  }
}

xaxis("length",BottomTop,LeftTicks);
yaxis("time (s)",LeftRight,RightTicks);
