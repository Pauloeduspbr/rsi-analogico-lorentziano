//+------------------------------------------------------------------+
//|                                      TV_02_MLRSI_Trend.mq5       |
//|   Sobreposicao do "Machine Learning RSI (Zeiierman)"             |
//|   Original Pine v6: (c) Zeiierman — CC BY-NC-SA 4.0              |
//|                                                                  |
//|   LICENCA: CC BY-NC-SA 4.0 (NonCommercial + ShareAlike).         |
//|   Uso pessoal/estudo livre; uso COMERCIAL nao permitido.         |
//|                                                                  |
//| Reproduz no grafico de preco o que o Pine desenha via            |
//| force_overlay (linhas 540-581):                                  |
//|   - nuvem de 4 camadas entre o trailing stop e o preco           |
//|   - VELAS COLORIDAS pelo regime (azul em alta, branca em baixa)  |
//|   - trailing stop adaptativo, azul em alta / branco em baixa     |
//|   - setas de entrada ancoradas na extremidade de 5 barras        |
//| Todo o CALCULO roda em ml_rsi.dll (C++), a mesma DLL do painel.  |
//|                                                                  |
//| TRES REGRAS DE MQL5 APRENDIDAS A DURAS PENAS NESTE ARQUIVO:      |
//| 1) DRAW_FILLING exige as DUAS cores declaradas no #property.     |
//|    Defini-las so por PlotIndexSetInteger faz o MT5 cair nas      |
//|    cores padrao dele (verde/vermelho).                           |
//| 2) Todo plot posicionado DEPOIS de um INDICATOR_COLOR_INDEX      |
//|    fica ilegivel por CopyBuffer (erro 4806) — embora DESENHE     |
//|    normalmente. Como as velas coloridas precisam ficar no meio   |
//|    (acima da nuvem, abaixo das linhas), os plots seguintes sao   |
//|    inevitavelmente ilegiveis de fora.                            |
//| 3) Buffers INDICATOR_CALCULATIONS no FIM continuam legiveis      |
//|    mesmo depois de um COLOR_INDEX. Por isso os valores do stop   |
//|    e dos sinais sao ESPELHADOS nos buffers 17..20: e por eles    |
//|    que o validador headless confere o desenho.                   |
//|                                                                  |
//| Requer "Permitir importacoes de DLL" HABILITADO.                 |
//+------------------------------------------------------------------+
#property copyright "Porte C++/MQL5 — original (c) Zeiierman (CC BY-NC-SA 4.0)"
#property link      "https://creativecommons.org/licenses/by-nc-sa/4.0/"
#property version   "3.00"
#property description "ML RSI (Zeiierman) — nuvem, velas por regime, trailing stop e setas"
#property description "Motor kNN + Fisher em ml_rsi.dll. Funciona sozinho ou com o painel."
#property description "Original CC BY-NC-SA 4.0 — uso comercial NAO permitido."

#property indicator_chart_window
#property indicator_buffers 21
#property indicator_plots   9

//--- ORDEM = ordem de desenho. Nuvem ao fundo, velas por cima dela,
//    depois as linhas do stop e por fim as setas.
//    Em cada DRAW_FILLING a 1a cor vale quando buffer1 > buffer2
//    (downtrend: stop ACIMA do preco) e a 2a quando e o contrario
//    (uptrend: stop ABAIXO). Assim a nuvem troca de cor sozinha.
//    Os tons simulam o alfa do Pine (60/72/84/92) sobre fundo escuro,
//    do mais denso junto ao stop ao mais tenue junto ao preco.
#property indicator_label1  "Cloud 1"
#property indicator_type1   DRAW_FILLING
#property indicator_color1  C'110,110,110',C'13,65,115'
#property indicator_label2  "Cloud 2"
#property indicator_type2   DRAW_FILLING
#property indicator_color2  C'78,78,78',C'10,46,82'
#property indicator_label3  "Cloud 3"
#property indicator_type3   DRAW_FILLING
#property indicator_color3  C'49,49,49',C'6,29,51'
#property indicator_label4  "Cloud 4"
#property indicator_type4   DRAW_FILLING
#property indicator_color4  C'25,25,25',C'3,14,26'
//--- Pine 540-542: plotcandle com a cor do regime
#property indicator_label5  "Regime"
#property indicator_type5   DRAW_COLOR_CANDLES
#property indicator_color5  clrDodgerBlue,clrWhiteSmoke
//--- Pine 545-550: a linha do trailing stop, uma por direcao
#property indicator_label6  "ML Supertrend Up"
#property indicator_type6   DRAW_LINE
#property indicator_color6  clrDodgerBlue
#property indicator_width6  2
#property indicator_label7  "ML Supertrend Down"
#property indicator_type7   DRAW_LINE
#property indicator_color7  clrWhiteSmoke
#property indicator_width7  2
//--- Pine 578-581: setas de entrada
#property indicator_label8  "Long"
#property indicator_type8   DRAW_ARROW
#property indicator_color8  clrLime
#property indicator_width8  2
#property indicator_label9  "Short"
#property indicator_type9   DRAW_ARROW
#property indicator_color9  clrRed
#property indicator_width9  2

#define EXPECTED_ABI_VERSION 1
#define NFEAT 8

//--- espelhos legiveis por CopyBuffer (ver regra 3 no cabecalho)
#define MIR_STLINE 17
#define MIR_STDIR  18
#define MIR_LONG   19
#define MIR_SHORT  20

#import "ml_rsi.dll"
int MlRsiVersion(void);
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

//--- ATENCAO: nada de `input group` (desloca os parametros de iCustom).
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
input bool        InpShowCloud  = true;      // [Overlay] Nuvem entre o stop e o preco
input bool        InpPaintBars  = true;      // [Overlay] Colorir as velas pelo regime
input bool        InpShowSignals= true;      // [Overlay] Setas de entrada
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

double C1a[],C1b[],C2a[],C2b[],C3a[],C3b[],C4a[],C4b[];
double CndO[],CndH[],CndL[],CndC[],CndCol[];
double BufUp[],BufDn[],BufLong[],BufShort[];
double MirSt[],MirDir[],MirLong[],MirShort[];

double   g_manualW[NFEAT];
datetime g_lastBar = 0;

//+------------------------------------------------------------------+
int OnInit()
{
   const int v = MlRsiVersion();
   if(v != EXPECTED_ABI_VERSION)
   {
      PrintFormat("[TV_02_Trend] ABI incompativel: ml_rsi.dll=%d, esperado=%d. "
                  "Recompile (cpp/02_ml_rsi/build.sh).", v, EXPECTED_ABI_VERSION);
      return INIT_FAILED;
   }
   if(InpRsiBase < 2)                    { Print("[TV_02_Trend] Base RSI Length >= 2");     return INIT_PARAMETERS_INCORRECT; }
   if(InpMemory < 80 || InpMemory > 5000){ Print("[TV_02_Trend] Memory Depth em [80,5000]");return INIT_PARAMETERS_INCORRECT; }
   if(InpK < 1 || InpK > 64)             { Print("[TV_02_Trend] Analog Count em [1,64]");   return INIT_PARAMETERS_INCORRECT; }
   if(InpStMult < 0.5)                   { Print("[TV_02_Trend] ATR Multiplier >= 0.5");    return INIT_PARAMETERS_INCORRECT; }
   if(InpMaLength < 1)                   { Print("[TV_02_Trend] Signal Line Length >= 1");  return INIT_PARAMETERS_INCORRECT; }

   //--- a ordem dos SetIndexBuffer TEM de seguir a ordem dos plots e o
   //    numero de buffers que cada tipo consome:
   //    FILLING=2, COLOR_CANDLES=5 (O,H,L,C + cor), LINE=1, ARROW=1
   SetIndexBuffer(0,  C1a, INDICATOR_DATA); SetIndexBuffer(1,  C1b, INDICATOR_DATA);
   SetIndexBuffer(2,  C2a, INDICATOR_DATA); SetIndexBuffer(3,  C2b, INDICATOR_DATA);
   SetIndexBuffer(4,  C3a, INDICATOR_DATA); SetIndexBuffer(5,  C3b, INDICATOR_DATA);
   SetIndexBuffer(6,  C4a, INDICATOR_DATA); SetIndexBuffer(7,  C4b, INDICATOR_DATA);
   SetIndexBuffer(8,  CndO, INDICATOR_DATA); SetIndexBuffer(9,  CndH, INDICATOR_DATA);
   SetIndexBuffer(10, CndL, INDICATOR_DATA); SetIndexBuffer(11, CndC, INDICATOR_DATA);
   SetIndexBuffer(12, CndCol, INDICATOR_COLOR_INDEX);
   SetIndexBuffer(13, BufUp,    INDICATOR_DATA);
   SetIndexBuffer(14, BufDn,    INDICATOR_DATA);
   SetIndexBuffer(15, BufLong,  INDICATOR_DATA);
   SetIndexBuffer(16, BufShort, INDICATOR_DATA);
   //--- espelhos so para leitura externa (regra 3 do cabecalho)
   SetIndexBuffer(MIR_STLINE, MirSt,    INDICATOR_CALCULATIONS);
   SetIndexBuffer(MIR_STDIR,  MirDir,   INDICATOR_CALCULATIONS);
   SetIndexBuffer(MIR_LONG,   MirLong,  INDICATOR_CALCULATIONS);
   SetIndexBuffer(MIR_SHORT,  MirShort, INDICATOR_CALCULATIONS);

   ArraySetAsSeries(C1a,false); ArraySetAsSeries(C1b,false);
   ArraySetAsSeries(C2a,false); ArraySetAsSeries(C2b,false);
   ArraySetAsSeries(C3a,false); ArraySetAsSeries(C3b,false);
   ArraySetAsSeries(C4a,false); ArraySetAsSeries(C4b,false);
   ArraySetAsSeries(CndO,false);ArraySetAsSeries(CndH,false);
   ArraySetAsSeries(CndL,false);ArraySetAsSeries(CndC,false);
   ArraySetAsSeries(CndCol,false);
   ArraySetAsSeries(BufUp,false);   ArraySetAsSeries(BufDn,false);
   ArraySetAsSeries(BufLong,false); ArraySetAsSeries(BufShort,false);
   ArraySetAsSeries(MirSt,false);   ArraySetAsSeries(MirDir,false);
   ArraySetAsSeries(MirLong,false); ArraySetAsSeries(MirShort,false);

   PlotIndexSetInteger(7, PLOT_ARROW, 233);   // triangulo para cima
   PlotIndexSetInteger(8, PLOT_ARROW, 234);   // triangulo para baixo

   for(int i = 0; i < 9; ++i) PlotIndexSetDouble(i, PLOT_EMPTY_VALUE, EMPTY_VALUE);
   if(!InpShowCloud)
      for(int i = 0; i <= 3; ++i) PlotIndexSetInteger(i, PLOT_DRAW_TYPE, DRAW_NONE);
   if(!InpPaintBars) PlotIndexSetInteger(4, PLOT_DRAW_TYPE, DRAW_NONE);
   if(!InpShowSignals)
   { PlotIndexSetInteger(7, PLOT_DRAW_TYPE, DRAW_NONE);
     PlotIndexSetInteger(8, PLOT_DRAW_TYPE, DRAW_NONE); }

   g_manualW[0]=InpWVal; g_manualW[1]=InpWSlp; g_manualW[2]=InpWAcc; g_manualW[3]=InpWMid;
   g_manualW[4]=InpWPct; g_manualW[5]=InpWChn; g_manualW[6]=InpWSpr; g_manualW[7]=InpWReg;
   g_lastBar = 0;

   IndicatorSetString(INDICATOR_SHORTNAME,
                      StringFormat("ML Supertrend b0723a (k=%d)", InpK));
   PrintFormat("[TV_02_Trend] build 0723a | nuvem=%s velas=%s setas=%s maxBars=%d",
               InpShowCloud?"on":"OFF", InpPaintBars?"on":"OFF",
               InpShowSignals?"on":"OFF", InpMaxBars);
   return INIT_SUCCEEDED;
}

//+------------------------------------------------------------------+
double SrcAt(const int mode, const double o, const double h,
             const double l, const double c)
{
   switch(mode)
   {
      case SRC_OPEN:  return o;
      case SRC_HIGH:  return h;
      case SRC_LOW:   return l;
      case SRC_HL2:   return (h+l)/2.0;
      case SRC_HLC3:  return (h+l+c)/3.0;
      case SRC_OHLC4: return (o+h+l+c)/4.0;
      case SRC_HLCC4: return (h+l+c+c)/4.0;
   }
   return c;
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

   static bool warned = false;
   const int needed = InpMemory + 150;
   if(!warned && rates_total < needed)
   {
      warned = true;
      PrintFormat("[TV_02_Trend] Historico curto: %d barras, recomendado >= %d "
                  "(Memory Depth %d + aquecimento).", rates_total, needed, InpMemory);
   }

   if(prev_calculated > 0 && g_lastBar == time[rates_total-1]) return rates_total;
   g_lastBar = time[rates_total-1];

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
                  "TV_02_Trend", usable, usable*0.034);
   }
   const ulong tIni = GetMicrosecondCount();
   const int off    = rates_total - usable;

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
   if(ArraySize(st) != usable)
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
      PrintFormat("[TV_02_Trend] MlRsiCalculate falhou (rc=%d, barras=%d)", n, usable);
      return 0;
   }

   {
      const double ms = (GetMicrosecondCount()-tIni)/1000.0;
      static bool avisouTempo = false;
      if(!avisouTempo && ms > 800.0)
      {
         avisouTempo = true;
         PrintFormat("[TV_02_Trend] O calculo levou %.0f ms em %d barras. O MT5 "
                     "considera lento acima de ~1000 ms e o grafico pisca. "
                     "Reduza 'Barras calculadas' (atual: %d).", ms, usable, InpMaxBars);
      }
   }

   for(int i = 0; i < off; ++i)
   {
      C1a[i]=C1b[i]=C2a[i]=C2b[i]=C3a[i]=C3b[i]=C4a[i]=C4b[i]=EMPTY_VALUE;
      CndO[i]=CndH[i]=CndL[i]=CndC[i]=EMPTY_VALUE; CndCol[i]=0;
      BufUp[i]=BufDn[i]=BufLong[i]=BufShort[i]=EMPTY_VALUE;
      MirSt[i]=MirDir[i]=MirLong[i]=MirShort[i]=0.0;
   }

   for(int i = 0; i < usable; ++i)
   {
      const int b = off + i;

      C1a[b]=C1b[b]=C2a[b]=C2b[b]=C3a[b]=C3b[b]=C4a[b]=C4b[b]=EMPTY_VALUE;
      CndO[b]=CndH[b]=CndL[b]=CndC[b]=EMPTY_VALUE; CndCol[b]=0;
      BufUp[b]=BufDn[b]=BufLong[b]=BufShort[b]=EMPTY_VALUE;
      MirSt[b]=0.0; MirDir[b]=dr[i]; MirLong[b]=0.0; MirShort[b]=0.0;

      if(st[i] == st[i])          // NaN durante o aquecimento
      {
         //--- linha do stop, uma por direcao para a cor trocar na virada
         if(dr[i] > 0) BufUp[b] = st[i];
         else          BufDn[b] = st[i];
         MirSt[b] = st[i];

         //--- Pine 552-570: 4 camadas entre o stop e o preco que ele segue
         if(InpShowCloud)
         {
            const double ref = SrcAt((int)InpStSrc, open[b], high[b], low[b], close[b]);
            const double d   = ref - st[i];
            C1a[b]=st[i];           C1b[b]=st[i]+d*0.25;
            C2a[b]=st[i]+d*0.25;    C2b[b]=st[i]+d*0.50;
            C3a[b]=st[i]+d*0.50;    C3b[b]=st[i]+d*0.75;
            C4a[b]=st[i]+d*0.75;    C4b[b]=ref;
         }

         //--- Pine 540-542: a vela recebe a cor do regime
         if(InpPaintBars)
         {
            CndO[b]=open[b]; CndH[b]=high[b]; CndL[b]=low[b]; CndC[b]=close[b];
            CndCol[b] = (dr[i] > 0) ? 0 : 1;
         }
      }

      //--- Pine 576-581: seta ancorada na extremidade das ultimas 5 barras.
      //    ARMADILHA 14: o buffer do plot so recebe com as setas ligadas —
      //    DRAW_NONE nao basta (o MT5 restaura estilo salvo no grafico e
      //    desenha o que houver no buffer). O ESPELHO recebe sempre: e a
      //    API de leitura para EAs e nunca e desenhado.
      if(lg[i] > 0.5)
      {
         double lo = low[b];
         for(int j = b; j > b-5 && j >= 0; --j) if(low[j] < lo) lo = low[j];
         if(InpShowSignals) BufLong[b] = lo;
         MirLong[b] = lo;
      }
      else if(sh[i] > 0.5)
      {
         double hi = high[b];
         for(int j = b; j > b-5 && j >= 0; --j) if(high[j] > hi) hi = high[j];
         if(InpShowSignals) BufShort[b] = hi;
         MirShort[b] = hi;
      }
   }
   return rates_total;
}
//+------------------------------------------------------------------+
