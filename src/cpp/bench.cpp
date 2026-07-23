#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>
typedef int (__stdcall *fn_t)(const double*,const double*,const double*,const double*,const double*,int,
   int,int,int,int,double,int,double,const double*,int,int,int,int,int,int,int,double,double,int,int,int,double,
   double*,double*,double*,double*,double*,double*,double*,double*,double*,double*,double*,double*,double*);
int main(){
  HMODULE hm=LoadLibraryA("ml_rsi.dll");
  fn_t f=(fn_t)(void*)GetProcAddress(hm,"MlRsiCalculate");
  const int sizes[]={3000,6216,20000,50000,100000};
  std::printf("  barras |    tempo | ms/barra | veredito MT5 (limite ~1000ms)\n");
  for(int t=0;t<5;++t){
    const int N=sizes[t];
    std::vector<double> o(N),h(N),l(N),c(N),v(N),w(8,1.0);
    for(int i=0;i<N;++i){ double p=100+8*std::sin(i/140.0)+2*std::sin(i/17.0);
      o[i]=p; c[i]=p+0.1; h[i]=p+0.4; l[i]=p-0.4; v[i]=1000; }
    std::vector<double> a(N),b(N),cc(N),d(N),e(N),g(N),i2(N),j(N),k(N),m(N),n2(N),p2(N),q(N);
    LARGE_INTEGER fr,t0,t1; QueryPerformanceFrequency(&fr);
    QueryPerformanceCounter(&t0);
    f(o.data(),h.data(),l.data(),c.data(),v.data(),N,0,14,500,8,0.5,1,1.0,w.data(),
      1,1,20,1,60,50,4,1.5,1.0,10,1,14,2.0,
      a.data(),b.data(),cc.data(),d.data(),e.data(),g.data(),i2.data(),j.data(),
      k.data(),m.data(),n2.data(),p2.data(),q.data());
    QueryPerformanceCounter(&t1);
    const double ms=1000.0*(t1.QuadPart-t0.QuadPart)/fr.QuadPart;
    std::printf("  %6d | %7.0f ms | %8.4f | %s\n",N,ms,ms/N, ms>1000?"TRAVA o grafico":"ok");
  }
  FreeLibrary(hm); return 0;
}
