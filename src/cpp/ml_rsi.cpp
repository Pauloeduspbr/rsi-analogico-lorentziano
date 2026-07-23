//+------------------------------------------------------------------+
//| ml_rsi.cpp                                                       |
//| Porte C++ do "Machine Learning RSI (Zeiierman)" — CC BY-NC-SA 4.0|
//|                                                                  |
//| Mapa Pine -> C++ (linhas do source.pine):                        |
//|   206-212  scale01 / compress          -> scale01() / compress() |
//|   215-228  ma()                        -> signal_line()          |
//|   232-274  autoFeatureWeights (Fisher) -> fisher_weights()       |
//|   281-285  gapTo                       -> gap_to()               |
//|   287-299  consider (top-K)            -> consider()             |
//|   301-309  tally                       -> voto no loop principal |
//|   316-326  feature engine              -> build_features()       |
//|   330-354  feature bank                -> NAO materializado, ver  |
//|                                           a nota no .h           |
//|   356-372  auto weight optimizer       -> loop principal         |
//|   375-408  neighbor engine + votacao   -> loop principal         |
//|   410-446  contexto / supertrend       -> loop principal         |
//|   450-486  stance / rank / confidence  -> loop principal         |
//|   488-502  sinais                      -> loop principal         |
//|   504-536  visualizacao (mlRSI, MA/BB) -> passada final          |
//+------------------------------------------------------------------+
#include "ml_rsi.h"
#include <cmath>
#include <vector>
#include <limits>

namespace {

const double NA = std::numeric_limits<double>::quiet_NaN();
inline bool isna(double v) { return v != v; }

/* ~~ constantes internas do Pine (nao sao inputs) ~~ */
const int    WIN_LEN     = 100;   // linha 130 winLen
const int    SPACING     = 4;     // linha 131 spacingBars
const int    HORIZON     = 4;     // linha 132 horizonBars
const int    STEP_LEN    = 3;     // linha 320 stepLen
const int    TREND_LEN   = 50;    // linha 145 trendLen
const double CHOP_CUT    = 0.5;   // linha 146 chopCut
const int    VOL_BAND_HI = 85;    // linha 147 volBandHi
const double AUTO_FLOOR  = 0.5;   // linha 153 autoFloor
const int    AUTO_MINROWS= 60;    // linha 154 autoMinRows
const int    SMOOTH_LEN  = 10;    // linha 176 smoothLen
const int    COOL_BARS   = 5;     // linha 493 coolBars
const int    NFEAT       = 8;

typedef std::vector<double> Vec;

//--- pesos aprendidos na ultima barra, so para diagnostico (MlRsiWeights).
//    NAO participa do calculo: e escrito no fim e nunca lido de volta.
double g_lastWeights[NFEAT] = {1,1,1,1,1,1,1,1};
double g_diag[5] = {0,0,0,0,0};   // avgGap, gapTight, agreeFrac, analogScore, kCount

//+------------------------------------------------------------------+
//| Primitivas Pine                                                   |
//+------------------------------------------------------------------+
void src_series(int mode, const double *o, const double *h,
                const double *l, const double *c, int n, Vec &out)
{
   out.assign(n, NA);
   for(int i = 0; i < n; ++i)
      switch(mode)
      {
         case MLRSI_SRC_OPEN:  out[i] = o[i]; break;
         case MLRSI_SRC_HIGH:  out[i] = h[i]; break;
         case MLRSI_SRC_LOW:   out[i] = l[i]; break;
         case MLRSI_SRC_HL2:   out[i] = (h[i]+l[i])/2.0; break;
         case MLRSI_SRC_HLC3:  out[i] = (h[i]+l[i]+c[i])/3.0; break;
         case MLRSI_SRC_OHLC4: out[i] = (o[i]+h[i]+l[i]+c[i])/4.0; break;
         case MLRSI_SRC_HLCC4: out[i] = (h[i]+l[i]+c[i]+c[i])/4.0; break;
         default:              out[i] = c[i]; break;
      }
}

//--- ta.rma: alpha = 1/len, semeado pela SMA dos primeiros `len` validos
void rma(const Vec &s, int len, Vec &out)
{
   const int n = (int)s.size();
   out.assign(n, NA);
   if(len < 1) return;
   int start = 0;
   while(start < n && isna(s[start])) ++start;
   if(start + len > n) return;

   double sum = 0.0;
   for(int i = start; i < start + len; ++i) sum += s[i];
   double v = sum / len;
   out[start + len - 1] = v;
   const double a = 1.0 / len;
   for(int i = start + len; i < n; ++i)
   {
      v = a * s[i] + (1.0 - a) * v;
      out[i] = v;
   }
}

//--- ta.ema: alpha = 2/(len+1), semeado pelo primeiro valor valido
void ema(const Vec &s, int len, Vec &out)
{
   const int n = (int)s.size();
   out.assign(n, NA);
   if(len < 1) return;
   int start = 0;
   while(start < n && isna(s[start])) ++start;
   if(start >= n) return;
   const double a = 2.0 / (len + 1.0);
   double v = s[start];
   out[start] = v;
   for(int i = start + 1; i < n; ++i)
   {
      v = a * s[i] + (1.0 - a) * v;
      out[i] = v;
   }
}

void sma(const Vec &s, int len, Vec &out)
{
   const int n = (int)s.size();
   out.assign(n, NA);
   if(len < 1) return;
   for(int i = len - 1; i < n; ++i)
   {
      double sum = 0.0; bool bad = false;
      for(int j = i - len + 1; j <= i; ++j)
      { if(isna(s[j])) { bad = true; break; } sum += s[j]; }
      if(!bad) out[i] = sum / len;
   }
}

void wma(const Vec &s, int len, Vec &out)
{
   const int n = (int)s.size();
   out.assign(n, NA);
   if(len < 1) return;
   const double denom = len * (len + 1) / 2.0;
   for(int i = len - 1; i < n; ++i)
   {
      double sum = 0.0; bool bad = false;
      for(int j = 0; j < len; ++j)
      {
         const double v = s[i - len + 1 + j];
         if(isna(v)) { bad = true; break; }
         sum += v * (j + 1);
      }
      if(!bad) out[i] = sum / denom;
   }
}

void vwma(const Vec &s, const double *vol, int len, Vec &out)
{
   const int n = (int)s.size();
   out.assign(n, NA);
   if(len < 1) return;
   for(int i = len - 1; i < n; ++i)
   {
      double num = 0.0, den = 0.0; bool bad = false;
      for(int j = i - len + 1; j <= i; ++j)
      { if(isna(s[j])) { bad = true; break; } num += s[j]*vol[j]; den += vol[j]; }
      if(!bad && den > 0.0) out[i] = num / den;
   }
}

//--- ta.stdev: desvio padrao POPULACIONAL (divisor N), como no Pine
void stdev(const Vec &s, int len, Vec &out)
{
   const int n = (int)s.size();
   out.assign(n, NA);
   if(len < 1) return;
   for(int i = len - 1; i < n; ++i)
   {
      double sum = 0.0; bool bad = false;
      for(int j = i - len + 1; j <= i; ++j)
      { if(isna(s[j])) { bad = true; break; } sum += s[j]; }
      if(bad) continue;
      const double m = sum / len;
      double acc = 0.0;
      for(int j = i - len + 1; j <= i; ++j) acc += (s[j]-m)*(s[j]-m);
      out[i] = std::sqrt(acc / len);
   }
}

//--- ta.highest / ta.lowest: janela de `len` barras INCLUINDO a atual
void hi_lo(const Vec &s, int len, Vec &hiOut, Vec &loOut)
{
   const int n = (int)s.size();
   hiOut.assign(n, NA); loOut.assign(n, NA);
   if(len < 1) return;
   for(int i = len - 1; i < n; ++i)
   {
      double hi = -1e308, lo = 1e308; bool bad = false;
      for(int j = i - len + 1; j <= i; ++j)
      {
         if(isna(s[j])) { bad = true; break; }
         if(s[j] > hi) hi = s[j];
         if(s[j] < lo) lo = s[j];
      }
      if(!bad) { hiOut[i] = hi; loOut[i] = lo; }
   }
}

//--- Pine scale01(): posicao normalizada na janela; 0.5 se a janela e plana
void scale01(const Vec &s, int len, Vec &out)
{
   Vec hi, lo;
   hi_lo(s, len, hi, lo);
   const int n = (int)s.size();
   out.assign(n, NA);
   for(int i = 0; i < n; ++i)
   {
      if(isna(s[i]) || isna(hi[i]) || isna(lo[i])) continue;
      out[i] = (hi[i] == lo[i]) ? 0.5 : (s[i] - lo[i]) / (hi[i] - lo[i]);
   }
}

//--- ta.percentrank: % dos `len` valores ANTERIORES que sao <= o atual
void percentrank(const Vec &s, int len, Vec &out)
{
   const int n = (int)s.size();
   out.assign(n, NA);
   if(len < 1) return;
   for(int i = len; i < n; ++i)
   {
      if(isna(s[i])) continue;
      int cnt = 0; bool bad = false;
      for(int j = 1; j <= len; ++j)
      {
         if(isna(s[i-j])) { bad = true; break; }
         if(s[i-j] <= s[i]) ++cnt;
      }
      if(!bad) out[i] = 100.0 * cnt / len;
   }
}

//--- ta.rsi via rma de ganhos/perdas
void rsi(const Vec &s, int len, Vec &out)
{
   const int n = (int)s.size();
   out.assign(n, NA);
   if(len < 1 || n < 2) return;
   Vec up(n, NA), dn(n, NA);
   for(int i = 1; i < n; ++i)
   {
      if(isna(s[i]) || isna(s[i-1])) continue;
      const double ch = s[i] - s[i-1];
      up[i] = ch > 0 ?  ch : 0.0;
      dn[i] = ch < 0 ? -ch : 0.0;
   }
   Vec ru, rd;
   rma(up, len, ru);
   rma(dn, len, rd);
   for(int i = 0; i < n; ++i)
   {
      if(isna(ru[i]) || isna(rd[i])) continue;
      if(rd[i] == 0.0)      out[i] = 100.0;
      else if(ru[i] == 0.0) out[i] = 0.0;
      else                  out[i] = 100.0 - 100.0 / (1.0 + ru[i]/rd[i]);
   }
}

//--- ta.atr = rma(tr(true)); na 1a barra tr = high-low
void atr(const double *h, const double *l, const double *c, int n, int len, Vec &out)
{
   Vec tr(n, NA);
   for(int i = 0; i < n; ++i)
   {
      if(i == 0) { tr[i] = h[i]-l[i]; continue; }
      const double a = h[i]-l[i];
      const double b = std::fabs(h[i]-c[i-1]);
      const double d = std::fabs(l[i]-c[i-1]);
      tr[i] = a > b ? (a > d ? a : d) : (b > d ? b : d);
   }
   rma(tr, len, out);
}

inline double compress(double d) { return std::log(1.0 + std::fabs(d)); }
inline double clamp(double v, double lo, double hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

//+------------------------------------------------------------------+
//| Discriminante de Fisher sobre o banco rotulado (Pine 232-274)     |
//| Mede, por feature, a separacao entre as classes bull e bear:      |
//|   (mediaB - mediaBe)^2 / (varB + varBe)                           |
//| Depois reescala as 8 para [floor, 10].                            |
//| `rows` sao pares (indice de features, indice de outcome).         |
//+------------------------------------------------------------------+
void fisher_weights(const Vec feat[NFEAT], const Vec &outcome,
                    const std::vector<int> &featIdx,
                    const std::vector<int> &outIdx,
                    double floorW, int minRows, double *imp)
{
   for(int j = 0; j < NFEAT; ++j) imp[j] = 1.0;
   const int n = (int)featIdx.size();
   if(n < minRows) return;

   double sumB[NFEAT] = {0}, sumBe[NFEAT] = {0};
   double sqB[NFEAT]  = {0}, sqBe[NFEAT]  = {0};
   int cntB = 0, cntBe = 0;

   for(int i = 0; i < n; ++i)
   {
      const double o = outcome[outIdx[i]];
      if(isna(o) || o == 0.0) continue;      // Pine: `if o > 0 or o < 0`
      const bool isB = o > 0;
      const int  fi  = featIdx[i];
      bool bad = false;
      for(int j = 0; j < NFEAT; ++j) if(isna(feat[j][fi])) { bad = true; break; }
      if(bad) continue;
      for(int j = 0; j < NFEAT; ++j)
      {
         const double v = feat[j][fi];
         if(isB) { sumB[j]  += v; sqB[j]  += v*v; }
         else    { sumBe[j] += v; sqBe[j] += v*v; }
      }
      if(isB) ++cntB; else ++cntBe;
   }

   if(cntB <= 2 || cntBe <= 2) return;

   double fish[NFEAT], maxF = 0.0;
   for(int j = 0; j < NFEAT; ++j)
   {
      const double mB  = sumB[j]  / cntB;
      const double mBe = sumBe[j] / cntBe;
      const double vB  = std::fmax(0.0, sqB[j] /cntB  - mB*mB);
      const double vBe = std::fmax(0.0, sqBe[j]/cntBe - mBe*mBe);
      fish[j] = (mB-mBe)*(mB-mBe) / (vB + vBe + 1e-6);
      if(fish[j] > maxF) maxF = fish[j];
   }
   for(int j = 0; j < NFEAT; ++j)
   {
      const double norm = maxF > 0 ? fish[j]/maxF : 1.0;
      imp[j] = std::fmax(floorW, norm * 10.0);
   }
}

//--- Pine method consider(): top-K real, substituindo o PIOR vizinho
struct Neighbor { double gap; int cls; };

void consider(std::vector<Neighbor> &nbrs, int kMax, double gap, int cls)
{
   if((int)nbrs.size() < kMax) { Neighbor c = {gap, cls}; nbrs.push_back(c); return; }
   int worst = 0;
   double worstGap = nbrs[0].gap;
   for(int i = 1; i < (int)nbrs.size(); ++i)
      if(nbrs[i].gap > worstGap) { worstGap = nbrs[i].gap; worst = i; }
   if(gap < worstGap) { nbrs[worst].gap = gap; nbrs[worst].cls = cls; }
}

} // namespace

//+------------------------------------------------------------------+
int __stdcall MlRsiVersion(void) { return MLRSI_ABI_VERSION; }

//+------------------------------------------------------------------+
int __stdcall MlRsiWeights(double *out, int capacity)
{
   if(out == nullptr || capacity < NFEAT) return -1;
   for(int j = 0; j < NFEAT; ++j) out[j] = g_lastWeights[j];
   return NFEAT;
}

//+------------------------------------------------------------------+
int __stdcall MlRsiDiag(double *out, int capacity)
{
   if(out == nullptr || capacity < 5) return -1;
   for(int j = 0; j < 5; ++j) out[j] = g_diag[j];
   return 5;
}

//+------------------------------------------------------------------+
int __stdcall MlRsiCalculate(
   const double *open, const double *high, const double *low,
   const double *close, const double *volume, int size,
   int priceSrcMode, int rsiBase, int memoryDepth, int kNeighbors,
   double atrFactor,
   int autoWeightsOn, double autoSpeed, const double *manualWeights,
   int useTrendGate, int useVolBand, int volBandLo, int useChop,
   int gateRank, int gateConf,
   int stSrcMode, double stMultBase, double stMlResp, int stAtrLen,
   int maType, int maLength, double bbMult,
   double *mlRsiOut, double *sigMaOut, double *bbUpOut, double *bbLoOut,
   double *stLineOut, double *stDirOut, double *rankOut, double *confOut,
   double *biasOut, double *longOut, double *shortOut,
   double *flipUpOut, double *flipDnOut)
{
   if(open==nullptr || high==nullptr || low==nullptr || close==nullptr ||
      volume==nullptr)                                    return -1;
   if(size < 50)                                          return -1;
   if(rsiBase < 2)                                        return -1;
   if(memoryDepth < 80 || memoryDepth > 5000)             return -1;
   if(kNeighbors < 1 || kNeighbors > 64)                  return -1;
   if(atrFactor < 0.0)                                    return -1;
   if(autoSpeed < 0.005 || autoSpeed > 1.0)               return -1;
   if(autoWeightsOn == 0 && manualWeights == nullptr)      return -1;
   if(volBandLo < 0 || volBandLo > 100)                   return -1;
   if(stMultBase < 0.5 || stAtrLen < 1)                   return -1;
   if(stMlResp < 0.0 || stMlResp > 1.0)                   return -1;
   if(maLength < 1)                                       return -1;

   const int n = size;

   //=================================================================
   // 1. Series base
   //=================================================================
   Vec src, stSrc;
   src_series(priceSrcMode, open, high, low, close, n, src);
   src_series(stSrcMode,    open, high, low, close, n, stSrc);

   Vec rOsc, rOscF, rOscS;
   rsi(src, rsiBase, rOsc);
   {
      int lf = (int)std::floor(rsiBase/2.0 + 0.5);   // math.round
      if(lf < 2) lf = 2;
      rsi(src, lf, rOscF);
   }
   rsi(src, rsiBase * 2, rOscS);

   Vec atrVal, stAtr;
   atr(high, low, close, n, 14, atrVal);
   atr(high, low, close, n, stAtrLen, stAtr);

   //=================================================================
   // 2. As 8 features (Pine 322-325)
   //=================================================================
   Vec fVal(n, NA), dSlope(n, NA), dAccel(n, NA), fMid(n, NA), sdOsc, spread(n, NA), regRaw(n, NA);
   Vec emaOsc20;
   ema(rOsc, 20, emaOsc20);
   stdev(rOsc, 14, sdOsc);

   for(int i = 0; i < n; ++i)
   {
      if(!isna(rOsc[i]))
      {
         fVal[i] = rOsc[i] / 100.0;
         fMid[i] = std::fabs(rOsc[i] - 50.0) / 50.0;
      }
      if(i >= STEP_LEN && !isna(rOsc[i]) && !isna(rOsc[i-STEP_LEN]))
         dSlope[i] = rOsc[i] - rOsc[i-STEP_LEN];
      if(i >= 2*STEP_LEN && !isna(rOsc[i]) && !isna(rOsc[i-STEP_LEN]) && !isna(rOsc[i-2*STEP_LEN]))
         dAccel[i] = (rOsc[i] - rOsc[i-STEP_LEN]) - (rOsc[i-STEP_LEN] - rOsc[i-2*STEP_LEN]);
      if(!isna(rOscF[i]) && !isna(rOscS[i]))
         spread[i] = rOscF[i] - rOscS[i];
      if(!isna(emaOsc20[i]))
         regRaw[i] = emaOsc20[i] - 50.0;
   }

   Vec feat[NFEAT];
   feat[0] = fVal;
   scale01(dSlope, WIN_LEN, feat[1]);
   scale01(dAccel, WIN_LEN, feat[2]);
   feat[3] = fMid;
   {
      Vec pr; percentrank(rOsc, WIN_LEN, pr);
      feat[4].assign(n, NA);
      for(int i = 0; i < n; ++i) if(!isna(pr[i])) feat[4][i] = pr[i]/100.0;
   }
   scale01(sdOsc,  WIN_LEN, feat[5]);
   scale01(spread, WIN_LEN, feat[6]);
   scale01(regRaw, WIN_LEN, feat[7]);

   //=================================================================
   // 3. Rotulos (outcome) — Pine 332-335
   //=================================================================
   Vec outcome(n, NA);
   for(int i = HORIZON; i < n; ++i)
   {
      if(isna(src[i]) || isna(src[i-HORIZON]) || isna(atrVal[i-HORIZON])) continue;
      const double mv = src[i] - src[i-HORIZON];
      const double bd = atrFactor * atrVal[i-HORIZON];
      outcome[i] = mv >  2*bd ?  3 : mv >  bd ?  2 : mv > 0 ?  1 :
                   mv < -2*bd ? -3 : mv < -bd ? -2 : mv < 0 ? -1 : 0;
   }

   //=================================================================
   // 4. Contexto que nao depende do motor
   //=================================================================
   Vec emaTrend, emaQuick, atrPct, ema5Osc;
   ema(src, TREND_LEN, emaTrend);
   ema(src, 5, emaQuick);
   percentrank(atrVal, 100, atrPct);
   ema(rOsc, 5, ema5Osc);

   //=================================================================
   // 5. LOOP PRINCIPAL — kNN, votacao, supertrend, stance, sinais
   //=================================================================
   Vec convS(n, NA), rankV(n, NA), confV(n, NA);
   std::vector<int> biasV(n, 0), stDirV(n, 0), longV(n, 0), shortV(n, 0),
                    flipUpV(n, 0), flipDnV(n, 0);
   Vec stLineV(n, NA);

   double wAuto[NFEAT] = {1,1,1,1,1,1,1,1};
   double stLong = NA, stShort = NA;
   int    stDir = 1;
   bool   stDirInit = false;
   double convSm = NA;
   int    stanceState = 0, stanceAge = 0;
   bool   chg1 = false, chg2 = false, chg3 = false;
   int    lastEntryBar = -1;
   bool   haveEntry = false;

   std::vector<Neighbor> nbrs;
   nbrs.reserve(kNeighbors);
   std::vector<int> featIdx, outIdx;
   featIdx.reserve(memoryDepth); outIdx.reserve(memoryDepth);

   for(int i = 0; i < n; ++i)
   {
      //--- banco vigente nesta barra (ver a nota de arquitetura no .h):
      //    linha idx = features da barra (i-idx-HORIZON), outcome de (i-idx)
      const int bankN = (i > HORIZON) ? (i - HORIZON) : 0;
      const int rows  = bankN < memoryDepth ? bankN : memoryDepth;

      //--- 5a. pesos: Fisher aprendido ou manual
      double w[NFEAT];
      if(autoWeightsOn)
      {
         featIdx.clear(); outIdx.clear();
         for(int idx = 0; idx < rows; ++idx)
         {
            const int fi = i - idx - HORIZON;
            const int oi = i - idx;
            if(fi < 0) break;
            featIdx.push_back(fi); outIdx.push_back(oi);
         }
         double wRaw[NFEAT];
         fisher_weights(feat, outcome, featIdx, outIdx, AUTO_FLOOR, AUTO_MINROWS, wRaw);
         for(int j = 0; j < NFEAT; ++j)
            wAuto[j] += autoSpeed * (wRaw[j] - wAuto[j]);
         for(int j = 0; j < NFEAT; ++j) w[j] = wAuto[j];
      }
      else
         for(int j = 0; j < NFEAT; ++j) w[j] = manualWeights[j];

      double wSum = 0.0;
      for(int j = 0; j < NFEAT; ++j) wSum += w[j];

      //--- 5b. kNN: so idx multiplo de SPACING (decorrelaciona vizinhos)
      nbrs.clear();
      bool curOk = true;
      for(int j = 0; j < NFEAT; ++j) if(isna(feat[j][i])) { curOk = false; break; }

      if(curOk && rows > 1)
         for(int idx = 0; idx < rows; idx += SPACING)
         {
            const int fi = i - idx - HORIZON;
            const int oi = i - idx;
            if(fi < 0) break;
            const double o = outcome[oi];
            if(isna(o)) continue;
            double g = 0.0; bool bad = false;
            for(int j = 0; j < NFEAT; ++j)
            {
               const double rv = feat[j][fi];
               if(isna(rv)) { bad = true; break; }
               g += w[j] * compress(feat[j][i] - rv);
            }
            if(!bad) consider(nbrs, kNeighbors, g, (int)o);
         }

      //--- 5c. votacao ponderada por 1/(1+gap)
      double vTotal = 0.0, vBull = 0.0, vBear = 0.0, vScore = 0.0, gapSum = 0.0;
      const int kCount = (int)nbrs.size();
      for(int j = 0; j < kCount; ++j)
      {
         const double wv = 1.0 / (1.0 + nbrs[j].gap);
         vTotal += wv;
         vScore += nbrs[j].cls * wv;      // cls e o outcome (-3..3), nao o sinal
         if(nbrs[j].cls > 0)      vBull += wv;
         else if(nbrs[j].cls < 0) vBear += wv;
         gapSum += nbrs[j].gap;
      }

      const double analogScore = vTotal > 0 ? vScore/vTotal : 0.0;
      const int    biasDir     = analogScore > 0.15 ? 1 : (analogScore < -0.15 ? -1 : 0);
      const double agreeFrac   = vTotal > 0
                                 ? (biasDir == 1 ? vBull : (biasDir == -1 ? vBear : 0.0)) / vTotal
                                 : 0.0;
      const double avgGap   = kCount > 0 ? gapSum/kCount : 0.0;
      const double gapScale = wSum * 0.45 + 1e-9;
      const double gapTight = clamp(1.0 - avgGap/gapScale, 0.0, 1.0);

      //--- 5d. conviccao suavizada -> largura adaptativa das bandas
      const double convInst = clamp(analogScore/1.5, -1.0, 1.0);
      if(isna(convSm)) convSm = convInst;
      else             convSm = (2.0/(SMOOTH_LEN+1.0))*convInst + (1.0-2.0/(SMOOTH_LEN+1.0))*convSm;
      convS[i] = convSm;

      const double trendForce = (!isna(atrVal[i]) && atrVal[i] > 0 &&
                                 !isna(emaQuick[i]) && !isna(emaTrend[i]))
                                ? std::fabs(emaQuick[i]-emaTrend[i])/atrVal[i] : 0.0;
      const bool chopRaw = trendForce < CHOP_CUT;
      const bool chopNow = useChop ? chopRaw : false;

      double mlDrive = clamp(std::fabs(convSm)*0.5 + gapTight*0.3 + agreeFrac*0.2, 0.0, 1.0);
      if(chopNow) mlDrive *= 0.35;

      const double adaptMult = stMultBase * (1.0 + stMlResp * (1.0 - mlDrive));

      //--- 5e. supertrend adaptativo (Pine 432-441)
      if(!isna(stAtr[i]) && !isna(stSrc[i]))
      {
         const double upBand = stSrc[i] - adaptMult * stAtr[i];
         const double dnBand = stSrc[i] + adaptMult * stAtr[i];
         const double pLong = stLong, pShort = stShort;

         stLong  = isna(pLong)  ? upBand
                 : (i > 0 && close[i-1] > pLong  ? std::fmax(upBand, pLong)  : upBand);
         stShort = isna(pShort) ? dnBand
                 : (i > 0 && close[i-1] < pShort ? std::fmin(dnBand, pShort) : dnBand);

         const int prevDir = stDir;
         if(!stDirInit) { stDir = 1; stDirInit = true; }
         else if(prevDir == -1 && !isna(pShort) && close[i] > pShort) stDir = 1;
         else if(prevDir ==  1 && !isna(pLong)  && close[i] < pLong)  stDir = -1;

         if(stDirInit && i > 0)
         {
            if(stDir == 1 && prevDir == -1)  flipUpV[i] = 1;
            if(stDir == -1 && prevDir == 1)  flipDnV[i] = 1;
         }
         stLineV[i] = (stDir == 1) ? stLong : stShort;
      }
      stDirV[i] = stDir;

      //--- 5f. contexto
      const bool upTrend    = (stDir == 1);
      const bool downTrend  = (stDir == -1);
      const double aPct     = isna(atrPct[i]) ? 0.0 : atrPct[i];
      const bool volHealthy = (aPct >= volBandLo && aPct <= VOL_BAND_HI);
      const double oscReg   = isna(emaOsc20[i]) ? 50.0 : emaOsc20[i];
      const bool slopeUp    = (i >= STEP_LEN && !isna(rOsc[i]) && !isna(rOsc[i-STEP_LEN]))
                              ? (rOsc[i] > rOsc[i-STEP_LEN]) : false;
      const bool slopeFit   = (biasDir == 1 && slopeUp) || (biasDir == -1 && !slopeUp);
      const bool stretched  = (biasDir == 1 && !isna(rOsc[i]) && rOsc[i] > 70) ||
                              (biasDir == -1 && !isna(rOsc[i]) && rOsc[i] < 30);
      const bool oscSmoothUp= (i > 0 && !isna(ema5Osc[i]) && !isna(ema5Osc[i-1]))
                              ? (ema5Osc[i] > ema5Osc[i-1]) : false;
      const bool aligned    = (biasDir == 1 && upTrend) || (biasDir == -1 && downTrend);

      //--- 5g. stance / persistencia
      const bool gatesPass = (!useTrendGate || aligned) &&
                             (!useVolBand   || volHealthy) && !chopNow;
      const int prevStance = stanceState;
      if(biasDir == 1 && gatesPass)       stanceState = 1;
      else if(biasDir == -1 && gatesPass) stanceState = -1;
      // senao mantem (Pine: nz(stanceState[1]))

      const bool changed = (stanceState != prevStance);
      stanceAge = changed ? 0 : stanceAge + 1;
      const bool earlyFlip = changed && (chg1 || chg2 || chg3);
      chg3 = chg2; chg2 = chg1; chg1 = changed;

      //--- 5h. rank e confidence (Pine 462-480)
      double rank = 0.0, conf = 0.0;
      if(biasDir != 0)
      {
         const double pAgree  = 25.0 * agreeFrac;
         const double pGap    = 15.0 * gapTight;
         const double pStruct = (slopeFit ? 10.0 : 0.0) + (stretched ? 0.0 : 5.0);
         const double pTrend  = aligned ? 10.0 : 0.0;
         const double pVol    = volHealthy ? 10.0 : (aPct < volBandLo ? 5.0 : 3.0);
         const bool   regFit  = (biasDir == 1 && oscReg > 55) || (biasDir == -1 && oscReg < 45);
         const double pReg    = regFit ? 10.0 : ((oscReg >= 45 && oscReg <= 55) ? 4.0 : 6.0);
         const double pSmooth = ((biasDir == 1 && oscSmoothUp) ||
                                 (biasDir == -1 && !oscSmoothUp)) ? 5.0 : 0.0;
         const double pHold   = std::fmin(5.0, (double)stanceAge);
         const double pPen    = std::fmin(20.0,
                                  (chopRaw ? 8.0 : 0.0) + (stretched ? 6.0 : 0.0) +
                                  (earlyFlip ? 6.0 : 0.0) +
                                  (kCount < kNeighbors
                                     ? 5.0*(kNeighbors-kCount)/kNeighbors : 0.0));
         rank = clamp(pAgree+pGap+pStruct+pTrend+pVol+pReg+pSmooth+pHold-pPen, 0.0, 100.0);

         const double craw = 40.0*agreeFrac + 25.0*gapTight
                           + 15.0*std::fmin(1.0, stanceAge/5.0)
                           + 10.0*(slopeFit ? 1.0 : 0.0)
                           - (earlyFlip ? 15.0 : 0.0)
                           - (kCount < kNeighbors
                                ? 10.0*(kNeighbors-kCount)/kNeighbors : 0.0);
         conf = clamp(craw, 0.0, 100.0);
      }
      rankV[i] = rank; confV[i] = conf; biasV[i] = biasDir;
      g_diag[0] = avgGap;   g_diag[1] = gapTight; g_diag[2] = agreeFrac;
      g_diag[3] = analogScore; g_diag[4] = (double)kCount;

      //--- 5i. sinais
      const bool flipLong  = (stanceState == 1  && prevStance != 1);
      const bool flipShort = (stanceState == -1 && prevStance != -1);
      const bool qualifies = (rank >= gateRank && conf >= gateConf);
      const bool coolOK    = !haveEntry || (i - lastEntryBar >= COOL_BARS);
      if(flipLong && qualifies && coolOK)  { longV[i]  = 1; lastEntryBar = i; haveEntry = true; }
      else if(flipShort && qualifies && coolOK) { shortV[i] = 1; lastEntryBar = i; haveEntry = true; }
   }

   for(int j = 0; j < NFEAT; ++j) g_lastWeights[j] = wAuto[j];

   //=================================================================
   // 6. Visualizacao: ML RSI = EMA3(clamp(rOsc + tilt)) — Pine 505-515
   //=================================================================
   Vec intens;
   ema(rankV, 3, intens);

   Vec mlRaw(n, NA);
   for(int i = 0; i < n; ++i)
   {
      if(isna(rOsc[i]) || isna(convS[i])) continue;
      const double it   = clamp((isna(intens[i]) ? 0.0 : intens[i])/100.0, 0.0, 1.0);
      const double tilt = clamp(convS[i], -1.0, 1.0) * it * 18.0;
      mlRaw[i] = clamp(rOsc[i] + tilt, 0.0, 100.0);
   }
   Vec mlRSI; ema(mlRaw, 3, mlRSI);

   Vec sigMa(n, NA), sd(n, NA);
   switch(maType)
   {
      case MLRSI_MA_SMA:
      case MLRSI_MA_SMA_BB: sma (mlRSI, maLength, sigMa); break;
      case MLRSI_MA_EMA:    ema (mlRSI, maLength, sigMa); break;
      case MLRSI_MA_RMA:    rma (mlRSI, maLength, sigMa); break;
      case MLRSI_MA_WMA:    wma (mlRSI, maLength, sigMa); break;
      case MLRSI_MA_VWMA:   vwma(mlRSI, volume, maLength, sigMa); break;
      default: break;   // NONE
   }
   if(maType == MLRSI_MA_SMA_BB) stdev(mlRSI, maLength, sd);

   //=================================================================
   // 7. Saidas
   //=================================================================
   for(int i = 0; i < n; ++i)
   {
      if(mlRsiOut)  mlRsiOut[i]  = mlRSI[i];
      if(sigMaOut)  sigMaOut[i]  = sigMa[i];
      if(bbUpOut)   bbUpOut[i]   = (maType==MLRSI_MA_SMA_BB && !isna(sigMa[i]) && !isna(sd[i]))
                                   ? sigMa[i] + sd[i]*bbMult : NA;
      if(bbLoOut)   bbLoOut[i]   = (maType==MLRSI_MA_SMA_BB && !isna(sigMa[i]) && !isna(sd[i]))
                                   ? sigMa[i] - sd[i]*bbMult : NA;
      if(stLineOut) stLineOut[i] = stLineV[i];
      if(stDirOut)  stDirOut[i]  = (double)stDirV[i];
      if(rankOut)   rankOut[i]   = rankV[i];
      if(confOut)   confOut[i]   = confV[i];
      if(biasOut)   biasOut[i]   = (double)biasV[i];
      if(longOut)   longOut[i]   = (double)longV[i];
      if(shortOut)  shortOut[i]  = (double)shortV[i];
      if(flipUpOut) flipUpOut[i] = (double)flipUpV[i];
      if(flipDnOut) flipDnOut[i] = (double)flipDnV[i];
   }
   return n;
}
