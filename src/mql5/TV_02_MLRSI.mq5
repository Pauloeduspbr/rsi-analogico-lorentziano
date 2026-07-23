//+------------------------------------------------------------------+
//|                                            TV_02_MLRSI.mq5       |
//|   Porte MQL5 do "Machine Learning RSI | AI Classification &      |
//|   Ranking (Zeiierman)"                                           |
//|   Pine v6 original: (c) Zeiierman                                |
//|                                                                  |
//|   LICENCA: CC BY-NC-SA 4.0 (NonCommercial + ShareAlike).         |
//|   Uso pessoal/estudo livre; uso COMERCIAL nao permitido.         |
//|   https://creativecommons.org/licenses/by-nc-sa/4.0/            |
//|                                                                  |
//| Motor kNN + Fisher em ml_rsi.dll (C++). Contrato em             |
//| cpp/02_ml_rsi/ml_rsi.h.                                          |
//|                                                                  |
//| PAINEL SEPARADO. O Pine usa force_overlay para desenhar o        |
//| Supertrend, a nuvem e as setas no grafico de preco — o que MQL5  |
//| nao permite a partir de uma janela separada. Os valores estao    |
//| nos buffers 13..21 e o indicador companheiro                     |
//| TV_02_MLRSI_Trend.mq5 os plota no grafico via iCustom.           |
//| ANEXE OS DOIS: so este aqui deixa o grafico de preco sem nada.   |
//|                                                                  |
//| Requer "Permitir importacoes de DLL" HABILITADO.                 |
//+------------------------------------------------------------------+
#property copyright "Porte C++/MQL5 — original (c) Zeiierman (CC BY-NC-SA 4.0)"
#property link      "https://creativecommons.org/licenses/by-nc-sa/4.0/"
#property version   "1.10"
#property description "ML RSI (Zeiierman) — kNN + discriminante de Fisher via ml_rsi.dll"
#property description "Anexe tambem TV_02_MLRSI_Trend para o trailing stop e a nuvem."
#property description "Original CC BY-NC-SA 4.0 — uso comercial NAO permitido."

#property indicator_separate_window
#property indicator_buffers 22
#property indicator_plots   8

//--- ORDEM DOS PLOTS = ordem de desenho. A linha ML RSI vem PRIMEIRO
//    porque e o conteudo principal do painel; os realces de zona sao
//    opcionais e vem por ultimo (ver InpShowZones).
//--- 0: ML RSI (colorido por gradiente)
#property indicator_label1  "ML RSI"
#property indicator_type1   DRAW_COLOR_LINE
#property indicator_width1  2
//--- 1: linha de sinal
#property indicator_label2  "RSI Signal Line"
#property indicator_type2   DRAW_LINE
#property indicator_color2  C'227,177,58'
#property indicator_width2  1
//--- 2/3: bandas de Bollinger
#property indicator_label3  "BB Upper"
#property indicator_type3   DRAW_LINE
#property indicator_color3  C'150,120,45'
#property indicator_style3  STYLE_DOT
#property indicator_label4  "BB Lower"
#property indicator_type4   DRAW_LINE
#property indicator_color4  C'150,120,45'
#property indicator_style4  STYLE_DOT
//--- 4: preenchimento das bandas
#property indicator_label5  "BB Fill"
#property indicator_type5   DRAW_FILLING
#property indicator_color5  C'55,45,18',C'55,45,18'
//--- 5: setas de sinal (o Pine as desenha no grafico; aqui tambem no painel)
#property indicator_label6  "Signal"
#property indicator_type6   DRAW_COLOR_ARROW
#property indicator_width6  2
//--- 6/7: realce das zonas extremas (Pine 525/526). DRAW_FILLING TEM de
//    ter as duas cores declaradas aqui no #property — definir so por
//    PlotIndexSetInteger deixa o MT5 cair nas cores padrao (verde/vermelho).
#property indicator_label7  "OB Zone"
#property indicator_type7   DRAW_FILLING
#property indicator_color7  C'14,38,66',C'14,38,66'
#property indicator_label8  "OS Zone"
#property indicator_type8   DRAW_FILLING
#property indicator_color8  C'46,46,46',C'46,46,46'

#property indicator_minimum -3
#property indicator_maximum 103
#property indicator_level1  70
#property indicator_level2  50
#property indicator_level3  30
#property indicator_levelstyle STYLE_DOT
#property indicator_levelcolor clrDimGray

#define EXPECTED_ABI_VERSION 1
#define NFEAT      8
#define GRAD_STEPS 32

//--- indices dos buffers de CALCULO, lidos por TV_02_MLRSI_Trend via
//    iCustom. Mudar a ordem dos plots MOVE estes indices — se mexer
//    aqui, atualize o companheiro na mesma hora.
#define BUF_STLINE 13
#define BUF_STDIR  14
#define BUF_RANK   15
#define BUF_CONF   16
#define BUF_BIAS   17
#define BUF_LONG   18
#define BUF_SHORT  19
#define BUF_FLIPUP 20
#define BUF_FLIPDN 21

#import "ml_rsi.dll"
int MlRsiVersion(void);
int MlRsiWeights(double &out[], int capacity);
int MlRsiDiag(double &out[], int capacity);
int MlRsiCalculate(const double &open[], const double &high[], const double &low[],
                   const double &close[], const double &volume[], int size,
                   int priceSrcMode, int rsiBase, int memoryDepth, int kNeighbors,
                   double atrFactor,
                   int autoWeightsOn, double autoSpeed, const double &manualWeights[],
                   int useTrendGate, int useVolBand, int volBandLo, int useChop,
                   int gateRank, int gateConf,
                   int stSrcMode, double stMultBase, double stMlResp, int stAtrLen,
                   int maType, int maLength, double bbMult,
                   double &mlRsiOut[], double &sigMaOut[], double &bbUpOut[], double &bbLoOut[],
                   double &stLineOut[], double &stDirOut[], double &rankOut[], double &confOut[],
                   double &biasOut[], double &longOut[], double &shortOut[],
                   double &flipUpOut[], double &flipDnOut[]);
#import

enum ENUM_SRC   { SRC_CLOSE=0, SRC_OPEN=1, SRC_HIGH=2, SRC_LOW=3,
                  SRC_HL2=4, SRC_HLC3=5, SRC_OHLC4=6, SRC_HLCC4=7 };
enum ENUM_MATYPE{ MA_NONE=0, MA_SMA=1, MA_SMA_BB=2, MA_EMA=3, MA_RMA=4, MA_WMA=5, MA_VWMA=6 };

//--- ATENCAO: nada de `input group` (desloca os parametros de iCustom)
input ENUM_SRC    InpPriceSrc   = SRC_CLOSE; // [Brain] Price Source
input int         InpRsiBase    = 14;        // [Brain] Base RSI Length
input int         InpMemory     = 500;       // [Brain] Memory Depth (bars)
input int         InpK          = 8;         // [Brain] Analog Count (k)
input double      InpAtrFactor  = 0.5;       // [Signals] Learning Sensitivity (xATR)
input int         InpGateRank   = 60;        // [Signals] Min Rank to Signal
input int         InpGateConf   = 50;        // [Signals] Min Confidence to Signal
input bool        InpTrendGate  = true;      // [Signals] Trend Gate
input bool        InpVolBand    = true;      // [Signals] Volatility Band
input int         InpVolBandLo  = 20;        // [Signals] Min Vol Rank
input bool        InpChop       = true;      // [Signals] Chop Filter
input bool        InpAutoW      = true;      // [Auto] Auto-Optimize Weights
input double      InpAutoSpeed  = 1.0;       // [Auto] Adaptation Speed
input double      InpWVal       = 1.0;       // [Weights] RSI Value
input double      InpWSlp       = 1.0;       // [Weights] RSI Slope
input double      InpWAcc       = 1.0;       // [Weights] RSI Acceleration
input double      InpWMid       = 1.0;       // [Weights] RSI Midpoint Dist
input double      InpWPct       = 1.0;       // [Weights] RSI Percentile
input double      InpWChn       = 1.0;       // [Weights] RSI Volatility
input double      InpWSpr       = 1.0;       // [Weights] RSI Fast/Slow Spread
input double      InpWReg       = 1.0;       // [Weights] RSI Regime
input ENUM_SRC    InpStSrc      = SRC_HL2;   // [Supertrend] Source
input double      InpStMult     = 1.5;       // [Supertrend] ATR Multiplier
input double      InpStMlResp   = 1.0;       // [Supertrend] ML Band Adaptivity
input int         InpStAtrLen   = 10;        // [Supertrend] ATR Length
input ENUM_MATYPE InpMaType     = MA_SMA;    // [SignalLine] Type
input int         InpMaLength   = 14;        // [SignalLine] Length
input double      InpBbMult     = 2.0;       // [SignalLine] BB StdDev
input color       InpRsiBull    = clrDodgerBlue;  // [Colors] RSI Bullish
input color       InpRsiBear    = clrWhiteSmoke;  // [Colors] RSI Bearish
input color       InpSigBull    = clrLime;        // [Colors] Long
input color       InpSigBear    = clrRed;         // [Colors] Short
input bool        InpLogWeights = false;     // [Diag] Registrar pesos aprendidos no log
input bool        InpShowZones  = false;     // [Colors] Realcar zonas >70 / <30
//--- NOTA DE DESEMPENHO (medida, nao estimada)
//    O motor faz recalculo completo a ~0.034 ms/barra. Num grafico com
//    todo o historico carregado isso estoura o orcamento do MT5:
//        3 000 barras ->    94 ms   ok
//       20 000 barras ->   678 ms   ok
//       50 000 barras -> 1 706 ms   "indicator is too slow"
//      100 000 barras -> 3 408 ms   "indicator is too slow"
//    E o MT5 processa TODOS os indicadores de um grafico na MESMA
//    thread: um lento trava os outros, e o grafico inteiro PISCA.
//    Foi exatamente o que aconteceu (log do terminal: 3375/8172/3406 ms).
//    Por isso o padrao passou de "todas" para 5000 barras (~170 ms).
//    Aumente so se precisar de historico maior E aceitar a lentidao.
input int         InpMaxBars    = 5000;      // [Perf] Barras calculadas (0 = todas; VER NOTA)

//--- plotados (a ordem dos SetIndexBuffer define a associacao com os plots)
double BufObA[], BufObB[], BufOsA[], BufOsB[], BufBbFA[], BufBbFB[],
       BufRsi[], BufRsiCol[], BufSig[], BufBbU[], BufBbL[],
       BufArrow[], BufArrowCol[];
//--- calculo (expostos por iCustom ao indicador de sobreposicao)
double BufStLine[], BufStDir[], BufRank[], BufConf[], BufBias[],
       BufLong[], BufShort[], BufFlipUp[], BufFlipDn[];

double   g_manualW[NFEAT];
datetime g_lastBar = 0;

//+------------------------------------------------------------------+
color Blend(const color a, const color b, const double t)
{
   const int ar=(int)(a&0xFF), ag=(int)((a>>8)&0xFF), ab=(int)((a>>16)&0xFF);
   const int br=(int)(b&0xFF), bg=(int)((b>>8)&0xFF), bb=(int)((b>>16)&0xFF);
   const int r=(int)(ar+(br-ar)*t), g=(int)(ag+(bg-ag)*t), bl=(int)(ab+(bb-ab)*t);
   return (color)(r | (g<<8) | (bl<<16));
}

//+------------------------------------------------------------------+
int OnInit()
{
   const int v = MlRsiVersion();
   if(v != EXPECTED_ABI_VERSION)
   {
      PrintFormat("[TV_02_MLRSI] ABI incompativel: ml_rsi.dll=%d, esperado=%d. "
                  "Recompile (cpp/02_ml_rsi/build.sh).", v, EXPECTED_ABI_VERSION);
      return INIT_FAILED;
   }
   if(InpRsiBase < 2)                           { Print("[TV_02] Base RSI Length >= 2");         return INIT_PARAMETERS_INCORRECT; }
   if(InpMemory < 80 || InpMemory > 5000)       { Print("[TV_02] Memory Depth em [80,5000]");    return INIT_PARAMETERS_INCORRECT; }
   if(InpK < 1 || InpK > 64)                    { Print("[TV_02] Analog Count em [1,64]");       return INIT_PARAMETERS_INCORRECT; }
   if(InpAutoSpeed < 0.005 || InpAutoSpeed > 1.0){Print("[TV_02] Adaptation Speed em [0.005,1]");return INIT_PARAMETERS_INCORRECT;}
   if(InpStMult < 0.5)                          { Print("[TV_02] ATR Multiplier >= 0.5");        return INIT_PARAMETERS_INCORRECT; }
   if(InpMaLength < 1)                          { Print("[TV_02] Signal Line Length >= 1");      return INIT_PARAMETERS_INCORRECT; }
   if(InpMaxBars != 0 && InpMaxBars < InpMemory + 500)
   { PrintFormat("[TV_02] Limitar barras a %d com Memory Depth %d deixa aquecimento "
                 "insuficiente. Use 0 ou >= %d.", InpMaxBars, InpMemory, InpMemory+500);
     return INIT_PARAMETERS_INCORRECT; }

   SetIndexBuffer(0,  BufRsi,      INDICATOR_DATA);
   SetIndexBuffer(1,  BufRsiCol,   INDICATOR_COLOR_INDEX);
   SetIndexBuffer(2,  BufSig,      INDICATOR_DATA);
   SetIndexBuffer(3,  BufBbU,      INDICATOR_DATA);
   SetIndexBuffer(4,  BufBbL,      INDICATOR_DATA);
   SetIndexBuffer(5,  BufBbFA,     INDICATOR_DATA);
   SetIndexBuffer(6,  BufBbFB,     INDICATOR_DATA);
   SetIndexBuffer(7,  BufArrow,    INDICATOR_DATA);
   SetIndexBuffer(8,  BufArrowCol, INDICATOR_COLOR_INDEX);
   SetIndexBuffer(9,  BufObA,      INDICATOR_DATA);
   SetIndexBuffer(10, BufObB,      INDICATOR_DATA);
   SetIndexBuffer(11, BufOsA,      INDICATOR_DATA);
   SetIndexBuffer(12, BufOsB,      INDICATOR_DATA);
   SetIndexBuffer(BUF_STLINE, BufStLine, INDICATOR_CALCULATIONS);
   SetIndexBuffer(BUF_STDIR,  BufStDir,  INDICATOR_CALCULATIONS);
   SetIndexBuffer(BUF_RANK,   BufRank,   INDICATOR_CALCULATIONS);
   SetIndexBuffer(BUF_CONF,   BufConf,   INDICATOR_CALCULATIONS);
   SetIndexBuffer(BUF_BIAS,   BufBias,   INDICATOR_CALCULATIONS);
   SetIndexBuffer(BUF_LONG,   BufLong,   INDICATOR_CALCULATIONS);
   SetIndexBuffer(BUF_SHORT,  BufShort,  INDICATOR_CALCULATIONS);
   SetIndexBuffer(BUF_FLIPUP, BufFlipUp, INDICATOR_CALCULATIONS);
   SetIndexBuffer(BUF_FLIPDN, BufFlipDn, INDICATOR_CALCULATIONS);

   ArraySetAsSeries(BufObA,false);   ArraySetAsSeries(BufObB,false);
   ArraySetAsSeries(BufOsA,false);   ArraySetAsSeries(BufOsB,false);
   ArraySetAsSeries(BufBbFA,false);  ArraySetAsSeries(BufBbFB,false);
   ArraySetAsSeries(BufRsi,false);   ArraySetAsSeries(BufRsiCol,false);
   ArraySetAsSeries(BufSig,false);   ArraySetAsSeries(BufBbU,false);
   ArraySetAsSeries(BufBbL,false);   ArraySetAsSeries(BufArrow,false);
   ArraySetAsSeries(BufArrowCol,false);
   ArraySetAsSeries(BufStLine,false);ArraySetAsSeries(BufStDir,false);
   ArraySetAsSeries(BufRank,false);  ArraySetAsSeries(BufConf,false);
   ArraySetAsSeries(BufBias,false);  ArraySetAsSeries(BufLong,false);
   ArraySetAsSeries(BufShort,false); ArraySetAsSeries(BufFlipUp,false);
   ArraySetAsSeries(BufFlipDn,false);

   //--- gradiente do Pine: color.from_gradient(mlRSI, 28, 72, bear, bull)
   PlotIndexSetInteger(0, PLOT_COLOR_INDEXES, GRAD_STEPS);
   for(int i = 0; i < GRAD_STEPS; ++i)
      PlotIndexSetInteger(0, PLOT_LINE_COLOR, i,
                          Blend(InpRsiBear, InpRsiBull, i/(double)(GRAD_STEPS-1)));

   PlotIndexSetInteger(5, PLOT_COLOR_INDEXES, 2);
   PlotIndexSetInteger(5, PLOT_LINE_COLOR, 0, InpSigBull);
   PlotIndexSetInteger(5, PLOT_LINE_COLOR, 1, InpSigBear);
   PlotIndexSetInteger(5, PLOT_ARROW, 159);

   for(int i = 0; i < 8; ++i) PlotIndexSetDouble(i, PLOT_EMPTY_VALUE, EMPTY_VALUE);
   if(InpMaType == MA_NONE) PlotIndexSetInteger(1, PLOT_DRAW_TYPE, DRAW_NONE);
   if(InpMaType != MA_SMA_BB)
      for(int i = 2; i <= 4; ++i) PlotIndexSetInteger(i, PLOT_DRAW_TYPE, DRAW_NONE);
   if(!InpShowZones)
   {
      PlotIndexSetInteger(6, PLOT_DRAW_TYPE, DRAW_NONE);
      PlotIndexSetInteger(7, PLOT_DRAW_TYPE, DRAW_NONE);
   }

   g_manualW[0]=InpWVal; g_manualW[1]=InpWSlp; g_manualW[2]=InpWAcc; g_manualW[3]=InpWMid;
   g_manualW[4]=InpWPct; g_manualW[5]=InpWChn; g_manualW[6]=InpWSpr; g_manualW[7]=InpWReg;

   g_lastBar = 0;
   IndicatorSetInteger(INDICATOR_DIGITS, 2);
   IndicatorSetString(INDICATOR_SHORTNAME,
                      StringFormat("ML RSI b0723a (%d, k=%d, mem=%d)", InpRsiBase, InpK, InpMemory));
   PrintFormat("[TV_02_MLRSI] build 0723a | zonas=%s maType=%d maxBars=%d",
               InpShowZones?"ON":"off", (int)InpMaType, InpMaxBars);
   return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
int OnCalculate(const int rates_total,
                const int prev_calculated,
                const datetime &time[],
                const double &open[],
                const double &high[],
                const double &low[],
                const double &close[],
                const long &tick_volume[],
                const long &volume[],
                const int &spread[])
{
   if(rates_total < 150) return 0;

   //--- AQUECIMENTO: o banco precisa de InpMemory linhas + ~100 barras
   //    de janela das features. Medido em EURUSD H1: com 500 barras o
   //    ML RSI erra ate 8.0 pontos; a partir de ~1000 ele bate EXATO
   //    com o calculo sobre o historico inteiro. Mesma razao do
   //    max_bars_back=2100 declarado no Pine.
   static bool warned = false;
   const int needed = InpMemory + 150;
   if(!warned && rates_total < needed)
   {
      warned = true;
      PrintFormat("[TV_02_MLRSI] Historico curto: %d barras, recomendado >= %d "
                  "(Memory Depth %d + aquecimento). As barras iniciais ficam "
                  "imprecisas ate o banco encher.", rates_total, needed, InpMemory);
   }

   //--- CUSTO: o motor recalcula TUDO (ver a nota de arquitetura no .h).
   //    Medido: 0.037 ms/barra -> 6216 barras = 229 ms. Com um grafico
   //    de 100 mil barras isso vira ~4 s de espera na primeira carga.
   //    InpMaxBars limita a janela para quem quer carga instantanea.
   const int usable = (InpMaxBars > 0 && InpMaxBars < rates_total) ? InpMaxBars : rates_total;

   //--- o MT5 reclama ("indicator is too slow") acima de ~1 s e, como
   //    todos os indicadores do grafico dividem a MESMA thread, o
   //    grafico inteiro passa a piscar. Avisamos uma vez se a janela
   //    escolhida for grande o bastante para chegar la.
   static bool avisouLento = false;
   if(!avisouLento && usable > 20000)
   {
      avisouLento = true;
      PrintFormat("[%s] Calculando %d barras (~%.0f ms). Acima de ~30000 o MT5 "
                  "acusa 'indicator is too slow' e o grafico pisca. Reduza "
                  "'Barras calculadas' se isso acontecer.",
                  "TV_02_MLRSI", usable, usable*0.034);
   }
   const ulong tIni = GetMicrosecondCount();
   const int off    = rates_total - usable;

   if(prev_calculated > 0 && g_lastBar == time[rates_total-1]) return rates_total;
   g_lastBar = time[rates_total-1];

   static double vo[], vh[], vl[], vc[], vv[];
   if(ArraySize(vc) != usable)
   {
      ArrayResize(vo,usable); ArrayResize(vh,usable); ArrayResize(vl,usable);
      ArrayResize(vc,usable); ArrayResize(vv,usable);
      ArraySetAsSeries(vo,false); ArraySetAsSeries(vh,false); ArraySetAsSeries(vl,false);
      ArraySetAsSeries(vc,false); ArraySetAsSeries(vv,false);
   }
   for(int i = 0; i < usable; ++i)
   {
      vo[i]=open[off+i]; vh[i]=high[off+i]; vl[i]=low[off+i]; vc[i]=close[off+i];
      vv[i]=(double)tick_volume[off+i];
   }

   static double ml[],sg[],bu[],bl[],st[],dr[],rk[],cf[],bs[],lg[],sh[],fu[],fd[];
   if(ArraySize(ml) != usable)
   {
      ArrayResize(ml,usable);ArrayResize(sg,usable);ArrayResize(bu,usable);
      ArrayResize(bl,usable);ArrayResize(st,usable);ArrayResize(dr,usable);
      ArrayResize(rk,usable);ArrayResize(cf,usable);ArrayResize(bs,usable);
      ArrayResize(lg,usable);ArrayResize(sh,usable);ArrayResize(fu,usable);
      ArrayResize(fd,usable);
      ArraySetAsSeries(ml,false);ArraySetAsSeries(sg,false);ArraySetAsSeries(bu,false);
      ArraySetAsSeries(bl,false);ArraySetAsSeries(st,false);ArraySetAsSeries(dr,false);
      ArraySetAsSeries(rk,false);ArraySetAsSeries(cf,false);ArraySetAsSeries(bs,false);
      ArraySetAsSeries(lg,false);ArraySetAsSeries(sh,false);ArraySetAsSeries(fu,false);
      ArraySetAsSeries(fd,false);
   }

   const int n = MlRsiCalculate(vo, vh, vl, vc, vv, usable,
                                (int)InpPriceSrc, InpRsiBase, InpMemory, InpK,
                                InpAtrFactor,
                                InpAutoW ? 1 : 0, InpAutoSpeed, g_manualW,
                                InpTrendGate ? 1 : 0, InpVolBand ? 1 : 0,
                                InpVolBandLo, InpChop ? 1 : 0,
                                InpGateRank, InpGateConf,
                                (int)InpStSrc, InpStMult, InpStMlResp, InpStAtrLen,
                                (int)InpMaType, InpMaLength, InpBbMult,
                                ml,sg,bu,bl,st,dr,rk,cf,bs,lg,sh,fu,fd);
   if(n <= 0)
   {
      PrintFormat("[TV_02_MLRSI] MlRsiCalculate falhou (rc=%d, barras=%d)", n, usable);
      return 0;
   }

   {
      const double ms = (GetMicrosecondCount()-tIni)/1000.0;
      static bool avisouTempo = false;
      if(!avisouTempo && ms > 800.0)
      {
         avisouTempo = true;
         PrintFormat("[TV_02_MLRSI] O calculo levou %.0f ms em %d barras. O MT5 "
                     "considera lento acima de ~1000 ms e o grafico pisca. "
                     "Reduza 'Barras calculadas' (atual: %d).", ms, usable, InpMaxBars);
      }
   }

   for(int i = 0; i < off; ++i)
   {
      BufRsi[i]=BufSig[i]=BufBbU[i]=BufBbL[i]=BufArrow[i]=EMPTY_VALUE;
      BufObA[i]=BufObB[i]=BufOsA[i]=BufOsB[i]=BufBbFA[i]=BufBbFB[i]=EMPTY_VALUE;
      BufRsiCol[i]=BufArrowCol[i]=0;
      BufStLine[i]=BufStDir[i]=BufRank[i]=BufConf[i]=BufBias[i]=0;
      BufLong[i]=BufShort[i]=BufFlipUp[i]=BufFlipDn[i]=0;
   }

   for(int i = 0; i < usable; ++i)
   {
      const int b = off + i;
      BufStLine[b]=st[i]; BufStDir[b]=dr[i]; BufRank[b]=rk[i]; BufConf[b]=cf[i];
      BufBias[b]=bs[i];   BufLong[b]=lg[i];  BufShort[b]=sh[i];
      BufFlipUp[b]=fu[i]; BufFlipDn[b]=fd[i];

      const double r = ml[i];
      if(r == r)
      {
         BufRsi[b] = r;
         double t = (r - 28.0) / (72.0 - 28.0);
         if(t < 0) t = 0; else if(t > 1) t = 1;
         BufRsiCol[b] = (double)(int)(t * (GRAD_STEPS-1) + 0.5);

         //--- Pine 525/526: destaque das zonas extremas. O Pine usa um
         //    gradiente vertical, que MQL5 nao tem; aqui e area cheia
         //    entre a linha e o nivel, so quando a ultrapassa.
         //    ARMADILHA 14: o gate por InpShowZones tem de estar AQUI,
         //    no buffer — o DRAW_NONE do OnInit nao basta, porque o MT5
         //    restaura cor/estilo persistidos no grafico e desenha o
         //    que houver no buffer (faixa fantasma no painel).
         if(InpShowZones && r > 70.0) { BufObA[b] = r;    BufObB[b] = 70.0; }
         else                         { BufObA[b] = BufObB[b] = EMPTY_VALUE; }
         if(InpShowZones && r < 30.0) { BufOsA[b] = 30.0; BufOsB[b] = r;    }
         else                         { BufOsA[b] = BufOsB[b] = EMPTY_VALUE; }
      }
      else
      {
         BufRsi[b] = EMPTY_VALUE; BufRsiCol[b] = 0;
         BufObA[b]=BufObB[b]=BufOsA[b]=BufOsB[b]=EMPTY_VALUE;
      }

      BufSig[b] = (sg[i]==sg[i]) ? sg[i] : EMPTY_VALUE;
      BufBbU[b] = (bu[i]==bu[i]) ? bu[i] : EMPTY_VALUE;
      BufBbL[b] = (bl[i]==bl[i]) ? bl[i] : EMPTY_VALUE;
      BufBbFA[b] = BufBbU[b];
      BufBbFB[b] = BufBbL[b];

      if(lg[i] > 0.5)      { BufArrow[b] = 8.0;  BufArrowCol[b] = 0; }
      else if(sh[i] > 0.5) { BufArrow[b] = 92.0; BufArrowCol[b] = 1; }
      else                   BufArrow[b] = EMPTY_VALUE;
   }

   if(InpLogWeights && InpAutoW)
   {
      double w[]; ArrayResize(w, NFEAT);
      if(MlRsiWeights(w, NFEAT) == NFEAT)
         PrintFormat("[TV_02_MLRSI] pesos aprendidos: val=%.2f slp=%.2f acc=%.2f mid=%.2f "
                     "pct=%.2f chn=%.2f spr=%.2f reg=%.2f",
                     w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]);
   }
   return rates_total;
}
//+------------------------------------------------------------------+
