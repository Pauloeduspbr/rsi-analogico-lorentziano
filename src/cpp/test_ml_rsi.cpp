//+------------------------------------------------------------------+
//| test_ml_rsi.cpp — teste de mesa da ml_rsi.dll                    |
//|                                                                  |
//| Num porte de ML, "compila e desenha" nao prova nada. Os testes   |
//| que carregam o peso da prova aqui sao:                           |
//|   T4  o kNN e REALMENTE top-K (avgGap cresce monotonicamente     |
//|       com k — se pegasse vizinhos arbitrarios, nao cresceria)    |
//|   T5  com k=1 a concordancia e exatamente 1.0 (um voto so)       |
//|   T6  o Fisher discrimina: pesos em [floor,10] com maximo == 10  |
//|   T7  determinismo: sem estado entre chamadas                    |
//|                                                                  |
//| O #11 (kNN) deste mesmo lote tinha um algoritmo que DEGENERAVA   |
//| em media movel; T4/T5 sao exatamente a rede que pega isso.       |
//+------------------------------------------------------------------+
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <vector>

#define SRC_CLOSE 0
#define SRC_HL2   4
#define MA_SMA_BB 2

typedef int (__stdcall *fn_ver_t)(void);
typedef int (__stdcall *fn_w_t)(double*, int);
typedef int (__stdcall *fn_calc_t)(
   const double*, const double*, const double*, const double*, const double*, int,
   int, int, int, int, double,
   int, double, const double*,
   int, int, int, int, int, int,
   int, double, double, int,
   int, int, double,
   double*, double*, double*, double*, double*, double*, double*, double*,
   double*, double*, double*, double*, double*);

static fn_ver_t  MlRsiVersion = nullptr;
static fn_w_t    MlRsiWeights = nullptr;
static fn_w_t    MlRsiDiag    = nullptr;
static fn_calc_t MlRsiCalc    = nullptr;
static int g_fail = 0;

static void check(bool ok, const char *name, const char *detail = "")
{
   std::printf("%-6s %s %s\n", ok ? "[ OK ]" : "[FAIL]", name, detail);
   if(!ok) ++g_fail;
}

//--- serie sintetica com tendencias, reversoes e volatilidade variavel
static void make_series(int n, std::vector<double> &o, std::vector<double> &h,
                        std::vector<double> &l, std::vector<double> &c,
                        std::vector<double> &v)
{
   o.resize(n); h.resize(n); l.resize(n); c.resize(n); v.resize(n);
   double px = 100.0;
   for(int i = 0; i < n; ++i)
   {
      const double trend = 8.0*std::sin(i/140.0);
      const double swing = 2.5*std::sin(i/17.0) + 1.2*std::cos(i/6.0);
      const double noise = 0.35*std::sin(i*12.9898);      // deterministico
      px = 100.0 + trend + swing + noise;
      const double rng = 0.30 + 0.20*std::fabs(std::sin(i/33.0));
      o[i] = px - 0.1*std::sin(i/4.0);
      c[i] = px;
      h[i] = (o[i] > c[i] ? o[i] : c[i]) + rng;
      l[i] = (o[i] < c[i] ? o[i] : c[i]) - rng;
      v[i] = 1000.0 + 500.0*std::fabs(std::sin(i/23.0));
   }
}

struct Out
{
   std::vector<double> ml, sig, bbu, bbl, st, dir, rank, conf, bias, lg, sh, fu, fd;
   explicit Out(int n) : ml(n), sig(n), bbu(n), bbl(n), st(n), dir(n), rank(n),
                         conf(n), bias(n), lg(n), sh(n), fu(n), fd(n) {}
};

static int run(const std::vector<double> &o, const std::vector<double> &h,
               const std::vector<double> &l, const std::vector<double> &c,
               const std::vector<double> &v, Out &out,
               int k = 8, int autoW = 1, const double *manual = nullptr,
               int gateRank = 60, int gateConf = 50, int memory = 500,
               int useTrend = 1, int useVol = 1, int useChop = 1)
{
   return MlRsiCalc(o.data(), h.data(), l.data(), c.data(), v.data(), (int)c.size(),
                    SRC_CLOSE, 14, memory, k, 0.5,
                    autoW, 1.0, manual,
                    useTrend, useVol, 20, useChop, gateRank, gateConf,
                    SRC_HL2, 1.5, 1.0, 10,
                    MA_SMA_BB, 14, 2.0,
                    out.ml.data(), out.sig.data(), out.bbu.data(), out.bbl.data(),
                    out.st.data(), out.dir.data(), out.rank.data(), out.conf.data(),
                    out.bias.data(), out.lg.data(), out.sh.data(),
                    out.fu.data(), out.fd.data());
}

int main()
{
   HMODULE hm = LoadLibraryA("ml_rsi.dll");
   if(!hm){ std::printf("[FAIL] LoadLibrary erro %lu\n", GetLastError()); return 1; }
   MlRsiVersion = (fn_ver_t)(void*)GetProcAddress(hm,"MlRsiVersion");
   MlRsiWeights = (fn_w_t)(void*)GetProcAddress(hm,"MlRsiWeights");
   MlRsiDiag    = (fn_w_t)(void*)GetProcAddress(hm,"MlRsiDiag");
   MlRsiCalc    = (fn_calc_t)(void*)GetProcAddress(hm,"MlRsiCalculate");
   if(!MlRsiVersion||!MlRsiWeights||!MlRsiDiag||!MlRsiCalc)
   { std::printf("[FAIL] GetProcAddress\n"); return 1; }

   check(MlRsiVersion()==1, "T1 ABI version == 1");

   const int N = 1500;
   std::vector<double> o,h,l,c,v;
   make_series(N,o,h,l,c,v);
   Out out(N);

   //--- T2 fail-fast
   {
      Out t(N);
      const int r1 = MlRsiCalc(nullptr,h.data(),l.data(),c.data(),v.data(),N,
                               SRC_CLOSE,14,500,8,0.5,1,1.0,nullptr,1,1,20,1,60,50,
                               SRC_HL2,1.5,1.0,10,MA_SMA_BB,14,2.0,
                               t.ml.data(),nullptr,nullptr,nullptr,nullptr,nullptr,
                               nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
      const int r2 = MlRsiCalc(o.data(),h.data(),l.data(),c.data(),v.data(),N,
                               SRC_CLOSE,14,10,8,0.5,1,1.0,nullptr,1,1,20,1,60,50,
                               SRC_HL2,1.5,1.0,10,MA_SMA_BB,14,2.0,
                               t.ml.data(),nullptr,nullptr,nullptr,nullptr,nullptr,
                               nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
      const int r3 = MlRsiCalc(o.data(),h.data(),l.data(),c.data(),v.data(),N,
                               SRC_CLOSE,14,500,999,0.5,1,1.0,nullptr,1,1,20,1,60,50,
                               SRC_HL2,1.5,1.0,10,MA_SMA_BB,14,2.0,
                               t.ml.data(),nullptr,nullptr,nullptr,nullptr,nullptr,
                               nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
      check(r1==-1&&r2==-1&&r3==-1, "T2 fail-fast em argumento invalido");
   }

   //--- T3 calcula e devolve valores dentro do dominio
   {
      const int r = run(o,h,l,c,v,out);
      int bad = 0, valid = 0;
      for(int i = 0; i < N; ++i)
      {
         if(out.ml[i] != out.ml[i]) continue;
         ++valid;
         if(out.ml[i] < 0.0 || out.ml[i] > 100.0) ++bad;
         if(out.rank[i] < 0.0 || out.rank[i] > 100.0) ++bad;
         if(out.conf[i] < 0.0 || out.conf[i] > 100.0) ++bad;
      }
      char d[160];
      std::snprintf(d,sizeof(d),"(%d barras, %d validas, %d fora do dominio)", r, valid, bad);
      check(r==N && valid > N/2 && bad==0, "T3 dominio: ML RSI/rank/conf em [0,100]", d);
   }

   //--- T4 CRITICO: o kNN e top-K de verdade.
   //    Aumentar k so pode INCLUIR vizinhos mais distantes, entao a
   //    distancia media tem de crescer monotonicamente. Um algoritmo
   //    degenerado (como o do #11) nao exibe essa monotonicidade.
   {
      const int ks[] = {1, 2, 4, 8, 16, 32};
      double gaps[6], dg[5];
      for(int t = 0; t < 6; ++t)
      {
         Out tmp(N);
         run(o,h,l,c,v,tmp,ks[t]);
         MlRsiDiag(dg,5);
         gaps[t] = dg[0];
      }
      bool mono = true;
      for(int t = 1; t < 6; ++t) if(gaps[t] < gaps[t-1] - 1e-12) mono = false;
      char d[220];
      std::snprintf(d,sizeof(d),"(avgGap k=1:%.4f k=2:%.4f k=4:%.4f k=8:%.4f k=16:%.4f k=32:%.4f)",
                    gaps[0],gaps[1],gaps[2],gaps[3],gaps[4],gaps[5]);
      check(mono && gaps[5] > gaps[0], "T4 kNN e top-K real (avgGap cresce com k)", d);
   }

   //--- T5 com k=1 ha um unico voto, logo a concordancia e total
   {
      Out tmp(N); double dg[5];
      run(o,h,l,c,v,tmp,1);
      MlRsiDiag(dg,5);
      const bool ok = (dg[4] == 1.0) && (std::fabs(dg[2]-1.0) < 1e-12 || dg[3] == 0.0);
      char d[190];
      std::snprintf(d,sizeof(d),"(k=1 -> kCount=%.0f agreeFrac=%.6f analogScore=%.4f)",
                    dg[4],dg[2],dg[3]);
      check(ok, "T5 k=1 => concordancia 1.0 (voto unico)", d);
   }

   //--- T6 o Fisher discrimina: escala para [floor,10] com maximo == 10
   {
      run(o,h,l,c,v,out);
      double w[8];
      MlRsiWeights(w,8);
      double mx = w[0], mn = w[0];
      for(int j=1;j<8;++j){ if(w[j]>mx) mx=w[j]; if(w[j]<mn) mn=w[j]; }
      const bool ok = (mn >= 0.5-1e-9) && (mx <= 10.0+1e-9) && (mx > 9.0) && (mx-mn > 1.0);
      char d[220];
      std::snprintf(d,sizeof(d),"(w=%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f)",
                    w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]);
      check(ok, "T6 Fisher discrimina (pesos em [0.5,10], max~10, ha spread)", d);
   }

   //--- T7 sem estado: duas chamadas identicas dao o mesmo resultado
   {
      Out a(N), b(N);
      run(o,h,l,c,v,a);
      Out other(N);
      std::vector<double> o2,h2,l2,c2,v2;
      make_series(N/2,o2,h2,l2,c2,v2);       // roda OUTRA serie no meio
      Out mid((int)c2.size());
      MlRsiCalc(o2.data(),h2.data(),l2.data(),c2.data(),v2.data(),(int)c2.size(),
                SRC_CLOSE,14,500,8,0.5,1,1.0,nullptr,1,1,20,1,60,50,
                SRC_HL2,1.5,1.0,10,MA_SMA_BB,14,2.0,
                mid.ml.data(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
                nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
      run(o,h,l,c,v,b);
      double maxDif = 0.0;
      for(int i=0;i<N;++i)
      {
         if(a.ml[i]!=a.ml[i] && b.ml[i]!=b.ml[i]) continue;
         const double d0 = std::fabs(a.ml[i]-b.ml[i]);
         if(d0 > maxDif) maxDif = d0;
      }
      char d[160];
      std::snprintf(d,sizeof(d),"(maior diferenca = %.3e)", maxDif);
      check(maxDif == 0.0, "T7 sem estado: repeticao identica apos outra serie", d);
   }

   //--- T8 cooldown de 5 barras entre sinais
   {
      run(o,h,l,c,v,out);
      int last = -1000, viol = 0, total = 0;
      for(int i=0;i<N;++i)
         if(out.lg[i] > 0.5 || out.sh[i] > 0.5)
         { ++total; if(i - last < 5) ++viol; last = i; }
      char d[160];
      std::snprintf(d,sizeof(d),"(%d sinais, %d violacoes de cooldown)", total, viol);
      check(viol==0 && total > 0, "T8 cooldown de 5 barras respeitado", d);
   }

   //--- T9 os gates realmente filtram: exigir mais rank reduz os sinais
   {
      int cnt[3]; const int gates[] = {0, 60, 95};
      for(int t=0;t<3;++t)
      {
         Out tmp(N);
         run(o,h,l,c,v,tmp,8,1,nullptr,gates[t],0);
         int s=0;
         for(int i=0;i<N;++i) if(tmp.lg[i]>0.5||tmp.sh[i]>0.5) ++s;
         cnt[t]=s;
      }
      char d[190];
      std::snprintf(d,sizeof(d),"(gateRank 0:%d sinais | 60:%d | 95:%d)",cnt[0],cnt[1],cnt[2]);
      check(cnt[0] >= cnt[1] && cnt[1] >= cnt[2], "T9 gate de rank filtra monotonicamente", d);
   }

   //--- T10 supertrend: direcao coerente com o lado da linha
   {
      run(o,h,l,c,v,out);
      int bad=0, flips=0, checked=0;
      for(int i=1;i<N;++i)
      {
         if(out.st[i]!=out.st[i]) continue;
         ++checked;
         if(out.dir[i] > 0 && out.st[i] > h[i]) ++bad;   // stop de alta acima do topo
         if(out.dir[i] < 0 && out.st[i] < l[i]) ++bad;   // stop de baixa abaixo do fundo
         if(out.fu[i] > 0.5 || out.fd[i] > 0.5) ++flips;
      }
      char d[190];
      std::snprintf(d,sizeof(d),"(%d barras checadas, %d incoerentes, %d flips)",checked,bad,flips);
      check(bad==0 && flips>2, "T10 supertrend do lado correto do preco", d);
   }

   //--- T11 pesos manuais mudam o resultado (o vetor de peso importa)
   {
      double wA[8] = {10,0.5,0.5,0.5,0.5,0.5,0.5,0.5};
      double wB[8] = {0.5,0.5,0.5,0.5,0.5,0.5,0.5,10};
      Out a(N), b(N);
      run(o,h,l,c,v,a,8,0,wA);
      run(o,h,l,c,v,b,8,0,wB);
      int dif=0;
      for(int i=0;i<N;++i)
      {
         if(a.ml[i]!=a.ml[i]||b.ml[i]!=b.ml[i]) continue;
         if(std::fabs(a.ml[i]-b.ml[i]) > 1e-9) ++dif;
      }
      char d[160];
      std::snprintf(d,sizeof(d),"(%d barras diferem entre os dois vetores de peso)",dif);
      check(dif > 10, "T11 os pesos das features afetam a saida", d);
   }

   //--- T12 custo por recalculo completo (o wrapper so chama em barra nova)
   {
      LARGE_INTEGER f,t0,t1;
      QueryPerformanceFrequency(&f);
      QueryPerformanceCounter(&t0);
      Out tmp(N); run(o,h,l,c,v,tmp);
      QueryPerformanceCounter(&t1);
      const double ms = 1000.0*(t1.QuadPart-t0.QuadPart)/f.QuadPart;
      char d[160];
      std::snprintf(d,sizeof(d),"(%d barras em %.1f ms = %.3f ms/barra)", N, ms, ms/N);
      check(ms < 5000.0, "T12 custo do recalculo completo", d);
   }

   //--- T13 QUANTO HISTORICO E PRECISO?
   //    O motor recalcula tudo, e supertrend/stance sao path-dependent
   //    (carregam estado por centenas de barras). Medimos, nas MESMAS
   //    200 barras finais, o erro de alimentar janelas menores.
   //    Isso vira recomendacao de uso, nao e um defeito a corrigir.
   {
      const int FULL = 4000;
      std::vector<double> o2,h2,l2,c2,v2;
      make_series(FULL,o2,h2,l2,c2,v2);
      Out ref(FULL);
      run(o2,h2,l2,c2,v2,ref);

      const int wins[] = {600, 1000, 1500, 2000, 3000};
      std::printf("       janela | pior erro nas 200 barras finais\n");
      bool converge = true; double prevErr = 1e9;
      for(int t = 0; t < 5; ++t)
      {
         const int W = wins[t];
         std::vector<double> so(o2.end()-W,o2.end()), sh2(h2.end()-W,h2.end()),
                             sl(l2.end()-W,l2.end()), sc(c2.end()-W,c2.end()),
                             sv(v2.end()-W,v2.end());
         Out sub(W);
         run(so,sh2,sl,sc,sv,sub);
         double mx = 0.0;
         for(int j = 0; j < 200; ++j)
         {
            const double a = ref.ml[FULL-1-j], b = sub.ml[W-1-j];
            if(a!=a || b!=b) continue;
            const double d = std::fabs(a-b); if(d > mx) mx = d;
         }
         std::printf("       %5d  | %.4f\n", W, mx);
         if(mx > prevErr + 1e-9) converge = false;
         prevErr = mx;
      }
      char d[160];
      std::snprintf(d,sizeof(d),"(erro cai monotonicamente com a janela; ver tabela acima)");
      check(converge, "T13 mais historico => convergencia ao calculo completo", d);
   }

   FreeLibrary(hm);
   std::printf("\n%s  (%d falha(s))\n", g_fail==0?"TODOS OS TESTES PASSARAM":"TESTES FALHARAM", g_fail);
   return g_fail==0?0:1;
}
