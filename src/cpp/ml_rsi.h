//+------------------------------------------------------------------+
//| ml_rsi.h                                                         |
//| Engine C++ do "Machine Learning RSI | AI Classification &        |
//| Ranking (Zeiierman)"                                             |
//| (c) Zeiierman — CC BY-NC-SA 4.0 (NAO comercial), Pine v6          |
//|                                                                  |
//| Porte de:                                                        |
//|   indicadores_tradingview/02_Machine_Learning_RSI_AI_Classifi...  |
//|   source.pine  (602 linhas)                                      |
//|                                                                  |
//| LICENCA: Attribution-NonCommercial-ShareAlike 4.0. Uso pessoal e  |
//| estudo livres; uso COMERCIAL nao permitido pelo autor.            |
//|                                                                  |
//| O QUE O MOTOR FAZ                                                 |
//|  1. Extrai 8 features de 3 RSIs (base, rapido=len/2, lento=2*len):|
//|     value, slope, accel, mid, pct, churn(vol), spread, regime.    |
//|  2. Rotula cada barra passada com o resultado das `horizon` barras|
//|     seguintes, em degraus de ATR: -3..+3.                         |
//|  3. Aprende o PESO de cada feature por discriminante de Fisher    |
//|     (separacao entre-classes / dentro-da-classe), suavizado por   |
//|     EMA (autoSpeed).                                              |
//|  4. kNN real: distancia Lorentziana ponderada sum(w*log(1+|d|)),  |
//|     top-K com substituicao do pior vizinho.                       |
//|  5. Votacao por 1/(1+gap) -> bias, agreement, tightness.          |
//|  6. Supertrend cuja largura de banda e modulada pela conviccao.   |
//|  7. Rank e Confidence compostos -> gates de sinal.                |
//|                                                                  |
//| DECISAO DE ARQUITETURA — FULL-RECOMPUTE, SEM ESTADO              |
//| O Pine mantem `var matrix bank` (janela deslizante de memoryDepth |
//| linhas, inserindo no topo). NAO replicamos essa matriz: ela e     |
//| DERIVAVEL. Na barra i, bank.row(idx) contem sempre                |
//|     features da barra (i - idx - horizon)  +  outcome da barra    |
//|     (i - idx)                                                     |
//| porque uma linha e inserida por barra confirmada, em ordem LIFO.  |
//| Logo basta indexar as series de features/outcome ja calculadas —  |
//| sem matriz, sem estado, sem risco de dessincronia com             |
//| prev_calculated. Ver a licao registrada no porte #04: estado      |
//| dentro da DLL produz acumulacao silenciosa e contaminacao entre   |
//| instancias.                                                       |
//|                                                                  |
//| CUSTO: O(n * memoryDepth/spacing * 8). Medido no proprio teste.   |
//| O wrapper MQL5 so recalcula quando ha barra nova.                 |
//|                                                                  |
//| Fronteira: apenas int/double/ponteiros. Nenhum bool, nenhuma      |
//| struct atravessa a DLL.                                           |
//+------------------------------------------------------------------+
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define MLRSI_ABI_VERSION 1

/* ~~ fonte de preco ~~ */
#define MLRSI_SRC_CLOSE 0
#define MLRSI_SRC_OPEN  1
#define MLRSI_SRC_HIGH  2
#define MLRSI_SRC_LOW   3
#define MLRSI_SRC_HL2   4
#define MLRSI_SRC_HLC3  5
#define MLRSI_SRC_OHLC4 6
#define MLRSI_SRC_HLCC4 7

/* ~~ tipo da linha de sinal (RSI Signal Line) ~~ */
#define MLRSI_MA_NONE   0
#define MLRSI_MA_SMA    1
#define MLRSI_MA_SMA_BB 2
#define MLRSI_MA_EMA    3
#define MLRSI_MA_RMA    4
#define MLRSI_MA_WMA    5
#define MLRSI_MA_VWMA   6

__declspec(dllexport) int __stdcall MlRsiVersion(void);

//+------------------------------------------------------------------+
//| MlRsiCalculate — calcula TODAS as barras de uma vez               |
//|                                                                   |
//| ENTRADA (series, indice 0 = barra mais ANTIGA)                    |
//|   open/high/low/close/volume, size                                |
//|                                                                   |
//| PARAMETROS (nomes iguais aos inputs do Pine)                      |
//|   priceSrcMode  MLRSI_SRC_*      (Pine: priceSrc, default close)  |
//|   rsiBase       Base RSI Length  (14)                             |
//|   memoryDepth   Memory Depth     (500)                            |
//|   kNeighbors    Analog Count k   (8)                              |
//|   atrFactor     Learning Sensitivity xATR (0.5)                   |
//|   autoWeightsOn 1 = Fisher automatico; 0 = usa manualWeights      |
//|   autoSpeed     Adaptation Speed (1.0)                            |
//|   manualWeights 8 pesos, usados so com autoWeightsOn == 0         |
//|   useTrendGate/useVolBand/useChop  filtros (0/1)                  |
//|   volBandLo     Min Vol Rank     (20)                             |
//|   gateRank/gateConf   limiares de sinal (60/50)                   |
//|   stSrcMode     MLRSI_SRC_*      (Pine: stSrc, default hl2)       |
//|   stMultBase    ATR Multiplier   (1.5)                            |
//|   stMlResp      ML Band Adaptivity (1.0)                          |
//|   stAtrLen      ATR do supertrend (10)                            |
//|   maType/maLength/bbMult   linha de sinal do RSI                  |
//|                                                                   |
//| SAIDA (arrays de `size` elementos; NaN onde o Pine daria `na`)    |
//|   mlRsiOut    a linha ML RSI (RSI + tilt de conviccao, EMA 3)     |
//|   sigMaOut    linha de sinal (NaN se maType == NONE)              |
//|   bbUpOut/bbLoOut  bandas de Bollinger (NaN se nao for SMA+BB)    |
//|   stLineOut   trailing stop adaptativo                            |
//|   stDirOut    +1 uptrend / -1 downtrend                           |
//|   rankOut     0..100  qualidade do setup                          |
//|   confOut     0..100  conviccao do motor                          |
//|   biasOut     -1/0/+1 direcao do motor                            |
//|   longOut/shortOut  1.0 na barra do sinal, 0.0 nas demais         |
//|   flipUpOut/flipDnOut  1.0 na barra em que o supertrend virou     |
//|   Qualquer ponteiro de saida pode ser NULL se nao interessar.     |
//|                                                                   |
//| Retorno: numero de barras processadas; -1 em argumento invalido.  |
//+------------------------------------------------------------------+
__declspec(dllexport) int __stdcall MlRsiCalculate(
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
   double *flipUpOut, double *flipDnOut);

//+------------------------------------------------------------------+
//| MlRsiWeights — os 8 pesos aprendidos na ULTIMA barra              |
//| Serve para inspecionar o que o otimizador de Fisher concluiu.     |
//| Chame depois de MlRsiCalculate. Retorna quantos escreveu, ou -1.  |
//+------------------------------------------------------------------+
__declspec(dllexport) int __stdcall MlRsiWeights(double *out, int capacity);

//+------------------------------------------------------------------+
//| MlRsiDiag — instantaneo do motor na ULTIMA barra calculada        |
//| out[0] avgGap  distancia media dos k vizinhos escolhidos          |
//| out[1] gapTight  0..1, quao apertado ficou o agrupamento          |
//| out[2] agreeFrac 0..1, fracao dos votos a favor do bias           |
//| out[3] analogScore  media ponderada dos rotulos (-3..+3)          |
//| out[4] kCount    quantos vizinhos foram realmente usados          |
//|                                                                   |
//| So diagnostico: escrito no fim de MlRsiCalculate e nunca lido de  |
//| volta pelo calculo. Reflete a ULTIMA chamada — com duas           |
//| instancias, vale a que rodou por ultimo.                          |
//| Retorna quantos escreveu, ou -1.                                  |
//+------------------------------------------------------------------+
__declspec(dllexport) int __stdcall MlRsiDiag(double *out, int capacity);

#ifdef __cplusplus
}
#endif
