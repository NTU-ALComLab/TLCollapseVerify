/**CFile****************************************************************

  FileName    [sfmDec.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [SAT-based optimization using internal don't-cares.]

  Synopsis    [SAT-based decomposition.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: sfmDec.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "sfmInt.h"
#include "misc/st/st.h"
#include "map/mio/mio.h"
#include "base/abc/abc.h"
#include "misc/util/utilTruth.h"
#include "opt/dau/dau.h"
#include "map/mio/exp.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

typedef struct Sfm_Dec_t_ Sfm_Dec_t;
struct Sfm_Dec_t_
{
    // external
    Sfm_Par_t *       pPars;       // parameters
    Sfm_Lib_t *       pLib;        // library
    Sfm_Tim_t *       pTim;        // timing
    Abc_Ntk_t *       pNtk;        // network
    // library
    Vec_Int_t         vGateSizes;  // fanin counts
    Vec_Wrd_t         vGateFuncs;  // gate truth tables
    Vec_Wec_t         vGateCnfs;   // gate CNFs
    Vec_Ptr_t         vGateHands;  // gate handles
    int               GateConst0;  // special gates
    int               GateConst1;  // special gates
    int               GateBuffer;  // special gates
    int               GateInvert;  // special gates
    int               GateAnd[4];  // special gates
    int               GateOr[4];   // special gates
    // objects
    int               nDivs;       // the number of divisors
    int               nMffc;       // the number of divisors
    int               AreaMffc;    // the area of gates in MFFC
    int               DelayMin;    // temporary min delay
    int               iTarget;     // target node
    int               iUseThis;    // next cofactoring var to try
    int               DeltaCrit;   // critical delta
    int               AreaInv;     // inverter area
    int               DelayInv;    // inverter delay
    Mio_Gate_t *      pGateInv;    // inverter
    word              uCareSet;    // computed careset
    Vec_Int_t         vObjRoots;   // roots of the window
    Vec_Int_t         vObjGates;   // functionality
    Vec_Wec_t         vObjFanins;  // fanin IDs
    Vec_Int_t         vObjMap;     // object map
    Vec_Int_t         vObjDec;     // decomposition
    Vec_Int_t         vObjMffc;    // MFFC nodes
    Vec_Int_t         vObjInMffc;  // inputs of MFFC nodes
    Vec_Wrd_t         vObjSims;    // simulation patterns
    Vec_Wrd_t         vObjSims2;   // simulation patterns
    Vec_Ptr_t         vMatchGates; // matched gates
    Vec_Ptr_t         vMatchFans;  // matched fanins
    // solver
    sat_solver *      pSat;        // reusable solver 
    Vec_Wec_t         vClauses;    // CNF clauses for the node
    Vec_Int_t         vImpls[2];   // onset/offset implications
    Vec_Wrd_t         vSets[2];    // onset/offset patterns
    int               nPats[2];    // CEX count
    word              uMask[2];    // mask count
    word              TtElems[SFM_SUPP_MAX][SFM_WORD_MAX];
    word *            pTtElems[SFM_SUPP_MAX];
    // temporary
    Vec_Int_t         vTemp;
    Vec_Int_t         vTemp2;
    Vec_Int_t         vCands;
    word              Copy[4];
    int               nSuppVars;
    // statistics
    abctime           timeLib;
    abctime           timeWin;
    abctime           timeCnf;
    abctime           timeSat;
    abctime           timeSatSat;
    abctime           timeSatUnsat;
    abctime           timeTime;
    abctime           timeOther;
    abctime           timeStart;
    abctime           timeTotal;
    int               nTotalNodesBeg;
    int               nTotalEdgesBeg;
    int               nTotalNodesEnd;
    int               nTotalEdgesEnd;
    int               nNodesTried;
    int               nNodesChanged;
    int               nNodesConst0;
    int               nNodesConst1;
    int               nNodesBuf;
    int               nNodesInv;
    int               nNodesAndOr;
    int               nNodesResyn;
    int               nSatCalls;
    int               nSatCallsSat;
    int               nSatCallsUnsat;
    int               nSatCallsOver;
    int               nTimeOuts;
    int               nNoDecs;
    int               nEfforts;
    int               nMaxDivs;
    int               nMaxWin;
    word              nAllDivs;
    word              nAllWin;
    int               nLuckySizes[SFM_SUPP_MAX+1];
    int               nLuckyGates[SFM_SUPP_MAX+1];
};

#define SFM_MASK_PI     1  // supp(node) is contained in supp(TFI(pivot))
#define SFM_MASK_INPUT  2  // supp(node) does not overlap with supp(TFI(pivot))
#define SFM_MASK_FANIN  4  // the same as above (pointed to by node with SFM_MASK_PI | SFM_MASK_INPUT)
#define SFM_MASK_MFFC   8  // MFFC nodes, including the target node
#define SFM_MASK_PIVOT 16  // the target node

static inline Sfm_Dec_t * Sfm_DecMan( Abc_Obj_t * p )                        { return (Sfm_Dec_t *)p->pNtk->pData;                  }
static inline word        Sfm_DecObjSim( Sfm_Dec_t * p, Abc_Obj_t * pObj )   { return Vec_WrdEntry(&p->vObjSims, Abc_ObjId(pObj));  }
static inline word        Sfm_DecObjSim2( Sfm_Dec_t * p, Abc_Obj_t * pObj )  { return Vec_WrdEntry(&p->vObjSims2, Abc_ObjId(pObj)); }

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Setup parameter structure.]

  Description []

  SideEffects []

  SeeAlso     []

***********************************************************************/
void Sfm_ParSetDefault3( Sfm_Par_t * pPars )
{
    memset( pPars, 0, sizeof(Sfm_Par_t) );
    pPars->nTfoLevMax   =  100;  // the maximum fanout levels
    pPars->nTfiLevMax   =  100;  // the maximum fanin levels
    pPars->nFanoutMax   =   30;  // the maximum number of fanoutsp
    pPars->nMffcMin     =    1;  // the maximum MFFC size
    pPars->nMffcMax     =    3;  // the maximum MFFC size
    pPars->nVarMax      =    6;  // the maximum variable count
    pPars->nDecMax      =    1;  // the maximum number of decompositions
    pPars->nWinSizeMax  =    0;  // the maximum window size
    pPars->nGrowthLevel =    0;  // the maximum allowed growth in level
    pPars->nBTLimit     =    0;  // the maximum number of conflicts in one SAT run
    pPars->nTimeWin     =    1;  // the size of timing window in percents
    pPars->DeltaCrit    =    0;  // delta delay in picoseconds
    pPars->fUseAndOr    =    0;  // enable internal detection of AND/OR gates
    pPars->fZeroCost    =    0;  // enable zero-cost replacement
    pPars->fMoreEffort  =    0;  // enables using more effort
    pPars->fUseSim      =    0;  // enable simulation
    pPars->fArea        =    0;  // performs optimization for area
    pPars->fVerbose     =    0;  // enable basic stats
    pPars->fVeryVerbose =    0;  // enable detailed stats
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Sfm_Dec_t * Sfm_DecStart( Sfm_Par_t * pPars, Mio_Library_t * pLib, Abc_Ntk_t * pNtk )
{
    extern void Sfm_LibPreprocess( Mio_Library_t * pLib, Vec_Int_t * vGateSizes, Vec_Wrd_t * vGateFuncs, Vec_Wec_t * vGateCnfs, Vec_Ptr_t * vGateHands );
    Sfm_Dec_t * p = ABC_CALLOC( Sfm_Dec_t, 1 ); int i;
    p->timeStart = Abc_Clock();
    p->pPars     = pPars;
    p->pNtk      = pNtk;
    p->pSat      = sat_solver_new();
    p->pGateInv  = Mio_LibraryReadInv( pLib );
    p->AreaInv   = MIO_NUM*Mio_GateReadArea( p->pGateInv );
    p->DelayInv  = MIO_NUM*Mio_GateReadDelayMax( p->pGateInv );
    p->DeltaCrit = pPars->DeltaCrit ? MIO_NUM*pPars->DeltaCrit : 5 * (int)(MIO_NUM*Mio_LibraryReadDelayInvMax(pLib)) / 2;
p->timeLib = Abc_Clock();
    p->pLib = Sfm_LibPrepare( pPars->nVarMax, 1, !pPars->fArea, pPars->fVerbose, pPars->fLibVerbose );
p->timeLib = Abc_Clock() - p->timeLib;
    if ( !pPars->fArea )
        p->pTim = Sfm_TimStart( pLib, NULL, pNtk, p->DeltaCrit );
    if ( pPars->fVeryVerbose )
//    if ( pPars->fVerbose )
        Sfm_LibPrint( p->pLib );
    pNtk->pData = p;
    // enter library
    assert( Abc_NtkIsMappedLogic(pNtk) );
    Sfm_LibPreprocess( pLib, &p->vGateSizes, &p->vGateFuncs, &p->vGateCnfs, &p->vGateHands );
    p->GateConst0 = Mio_GateReadValue( Mio_LibraryReadConst0(pLib) );
    p->GateConst1 = Mio_GateReadValue( Mio_LibraryReadConst1(pLib) );
    p->GateBuffer = Mio_GateReadValue( Mio_LibraryReadBuf(pLib) );
    p->GateInvert = Mio_GateReadValue( Mio_LibraryReadInv(pLib) );
    if ( pPars->fRrOnly )
    {
        p->GateAnd[0] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "and00", NULL) );
        p->GateAnd[1] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "and01", NULL) );
        p->GateAnd[2] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "and10", NULL) );
        p->GateAnd[3] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "and11", NULL) );
        p->GateOr[0] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "or00", NULL) );
        p->GateOr[1] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "or01", NULL) );
        p->GateOr[2] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "or10", NULL) );
        p->GateOr[3] = Mio_GateReadValue( Mio_LibraryReadGateByName(pLib, "or11", NULL) );
    }
    // elementary truth tables
    for ( i = 0; i < SFM_SUPP_MAX; i++ )
        p->pTtElems[i] = p->TtElems[i];
    Abc_TtElemInit( p->pTtElems, SFM_SUPP_MAX );
    p->iUseThis = -1;
    return p;
}
void Sfm_DecStop( Sfm_Dec_t * p )
{
    Abc_Ntk_t * pNtk = p->pNtk;
    Abc_Obj_t * pObj; int i;
    Abc_NtkForEachNode( pNtk, pObj, i )
        //assert( (int)pObj->Level == Abc_ObjLevelNew(pObj) );
        if ( (int)pObj->Level != Abc_ObjLevelNew(pObj) )
            printf( "Level count mismatch at node %d.\n", i );
    Sfm_LibStop( p->pLib );
    if ( p->pTim ) Sfm_TimStop( p->pTim );
    // library
    Vec_IntErase( &p->vGateSizes );
    Vec_WrdErase( &p->vGateFuncs );
    Vec_WecErase( &p->vGateCnfs );
    Vec_PtrErase( &p->vGateHands );
    // objects
    Vec_IntErase( &p->vObjRoots );
    Vec_IntErase( &p->vObjGates );
    Vec_WecErase( &p->vObjFanins );
    Vec_IntErase( &p->vObjMap );
    Vec_IntErase( &p->vObjDec );
    Vec_IntErase( &p->vObjMffc );
    Vec_IntErase( &p->vObjInMffc );
    Vec_WrdErase( &p->vObjSims );
    Vec_WrdErase( &p->vObjSims2 );
    Vec_PtrErase( &p->vMatchGates );
    Vec_PtrErase( &p->vMatchFans );
    // solver
    sat_solver_delete( p->pSat );
    Vec_WecErase( &p->vClauses );
    Vec_IntErase( &p->vImpls[0] );
    Vec_IntErase( &p->vImpls[1] );
    Vec_WrdErase( &p->vSets[0] );
    Vec_WrdErase( &p->vSets[1] );
    // temporary
    Vec_IntErase( &p->vTemp );
    Vec_IntErase( &p->vTemp2 );
    Vec_IntErase( &p->vCands );
    ABC_FREE( p );
    pNtk->pData = NULL;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline word Sfm_ObjSimulate( Abc_Obj_t * pObj )
{
    Sfm_Dec_t * p = Sfm_DecMan( pObj );
    Vec_Int_t * vExpr = Mio_GateReadExpr( (Mio_Gate_t *)pObj->pData );
    Abc_Obj_t * pFanin; int i; word uFanins[6];
    assert( Abc_ObjFaninNum(pObj) <= 6 );
    Abc_ObjForEachFanin( pObj, pFanin, i )
        uFanins[i] = Sfm_DecObjSim( p, pFanin );
    return Exp_Truth6( Abc_ObjFaninNum(pObj), vExpr, uFanins );
}
static inline word Sfm_ObjSimulate2( Abc_Obj_t * pObj )
{
    Sfm_Dec_t * p = Sfm_DecMan( pObj );
    Vec_Int_t * vExpr = Mio_GateReadExpr( (Mio_Gate_t *)pObj->pData );
    Abc_Obj_t * pFanin; int i; word uFanins[6];
    Abc_ObjForEachFanin( pObj, pFanin, i )
        if ( (pFanin->iTemp & SFM_MASK_PIVOT) )
            uFanins[i] = Sfm_DecObjSim2( p, pFanin );
        else
            uFanins[i] = Sfm_DecObjSim( p, pFanin );
    return Exp_Truth6( Abc_ObjFaninNum(pObj), vExpr, uFanins );
}
static inline void Sfm_NtkSimulate( Abc_Ntk_t * pNtk )
{
    Vec_Ptr_t * vNodes;
    Abc_Obj_t * pObj; int i; word uTemp;
    Sfm_Dec_t * p = Sfm_DecMan( Abc_NtkPi(pNtk, 0) );
    Vec_WrdFill( &p->vObjSims,  2*Abc_NtkObjNumMax(pNtk), 0 );
    Vec_WrdFill( &p->vObjSims2, 2*Abc_NtkObjNumMax(pNtk), 0 );
    Gia_ManRandomW(1);
    assert( p->pPars->fUseSim );
    Abc_NtkForEachCi( pNtk, pObj, i )
    {
        Vec_WrdWriteEntry( &p->vObjSims, Abc_ObjId(pObj), (uTemp = Gia_ManRandomW(0)) );
        //printf( "Inpt = %5d : ", Abc_ObjId(pObj) );
        //Extra_PrintBinary( stdout, (unsigned *)&uTemp, 64 );
        //printf( "\n" );
    }
    vNodes = Abc_NtkDfs( pNtk, 1 );
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
    {
        Vec_WrdWriteEntry( &p->vObjSims, Abc_ObjId(pObj), (uTemp = Sfm_ObjSimulate(pObj)) );
        //printf( "Obj  = %5d : ", Abc_ObjId(pObj) );
        //Extra_PrintBinary( stdout, (unsigned *)&uTemp, 64 );
        //printf( "\n" );
    }
    Vec_PtrFree( vNodes );
}
static inline void Sfm_ObjSimulateNode( Abc_Obj_t * pObj )
{
    Sfm_Dec_t * p = Sfm_DecMan( pObj );
    if ( !p->pPars->fUseSim )
        return;
    Vec_WrdWriteEntry( &p->vObjSims, Abc_ObjId(pObj), Sfm_ObjSimulate(pObj) );
    if ( (pObj->iTemp & SFM_MASK_PIVOT) )
        Vec_WrdWriteEntry( &p->vObjSims2, Abc_ObjId(pObj), Sfm_ObjSimulate2(pObj) );
}
static inline void Sfm_ObjFlipNode( Abc_Obj_t * pObj )
{
    Sfm_Dec_t * p = Sfm_DecMan( pObj );
    if ( !p->pPars->fUseSim )
        return;
    Vec_WrdWriteEntry( &p->vObjSims2, Abc_ObjId(pObj), ~Sfm_DecObjSim(p, pObj) );
}
static inline word Sfm_ObjFindCareSet( Abc_Ntk_t * pNtk, Vec_Int_t * vRoots )
{
    Sfm_Dec_t * p = Sfm_DecMan( Abc_NtkPi(pNtk, 0) );
    Abc_Obj_t * pObj; int i; word Res = 0;
    if ( !p->pPars->fUseSim )
        return 0;
    Abc_NtkForEachObjVec( vRoots, pNtk, pObj, i )
        Res |= Sfm_DecObjSim(p, pObj) ^ Sfm_DecObjSim2(p, pObj);
    return Res;
}
static inline void Sfm_ObjSetupSimInfo( Abc_Obj_t * pObj )
{
    int nPatKeep = 24;
    Sfm_Dec_t * p = Sfm_DecMan( pObj );
    word uCareSet = p->uCareSet;
    word uValues  = Sfm_DecObjSim(p, pObj);
    int c, d, i, Indexes[2][64];
    assert( p->iTarget == pObj->iTemp );
    assert( p->pPars->fUseSim );
    // find what patterns go to on-set/off-set
    p->nPats[0] = p->nPats[1] = 0;
    p->uMask[0] = p->uMask[1] = 0;
    Vec_WrdFill( &p->vSets[0], p->nDivs, 0 );
    Vec_WrdFill( &p->vSets[1], p->nDivs, 0 );
    if ( uCareSet == 0 )
        return;
    for ( i = 0; i < 64; i++ )
        if ( (uCareSet >> i) & 1 )
        {
            c = !((uValues >> i) & 1);
            Indexes[c][p->nPats[c]++] = i;
        }
    for ( c = 0; c < 2; c++ )
    {
        p->nPats[c] = Abc_MinInt( p->nPats[c], nPatKeep );
        p->uMask[c] = Abc_Tt6Mask( p->nPats[c] );
    }
    // write patterns
    for ( d = 0; d < p->nDivs; d++ )
    {
        word uSim = Vec_WrdEntry( &p->vObjSims, Vec_IntEntry(&p->vObjMap, d) );
        for ( c = 0; c < 2; c++ )
            for ( i = 0; i < p->nPats[c]; i++ )
                if ( (uSim >> Indexes[c][i]) & 1 )
                    *Vec_WrdEntryP(&p->vSets[c], d) |= ((word)1 << i);
    }
    //printf( "Node %d : Onset = %d. Offset = %d.\n", pObj->Id, p->nPats[0], p->nPats[1] );
}
static inline void Sfm_ObjSetdownSimInfo( Abc_Obj_t * pObj )
{
    int nPatKeep = 32;
    Sfm_Dec_t * p = Sfm_DecMan( pObj );
    int c, d; word uSim, uSims[2], uMask;
    assert( p->pPars->fUseSim );
    for ( d = 0; d < p->nDivs; d++ )
    {
        uSim = Vec_WrdEntry( &p->vObjSims, Vec_IntEntry(&p->vObjMap, d) );
        for ( c = 0; c < 2; c++ )
        {
            uMask = p->nPats[c] < nPatKeep ? p->uMask[c] : Abc_Tt6Mask(nPatKeep);
            uSims[c] = (Vec_WrdEntry(&p->vSets[c], d) & uMask) | (uSim & ~uMask);
            uSim >>= 32;
        }
        uSim = (uSims[0] & 0xFFFFFFFF) | (uSims[1] << 32);
        Vec_WrdWriteEntry( &p->vObjSims, Vec_IntEntry(&p->vObjMap, d), uSim );
    }
}
/*
void Sfm_ObjSetdownSimInfo( Abc_Obj_t * pObj )
{
    int nPatKeep = 32;
    Sfm_Dec_t * p = Sfm_DecMan( pObj );
    word uSim, uMaskKeep[2];
    int c, d, nKeeps[2]; 
    for ( c = 0; c < 2; c++ )
    {
        nKeeps[c] = Abc_MaxInt(p->nPats[c], nPatKeep);
        uMaskKeep[c] = Abc_Tt6Mask( nKeeps[c] );
    }
    for ( d = 0; d < p->nDivs; d++ )
    {
        uSim = Vec_WrdEntry( &p->vObjSims, Vec_IntEntry(&p->vObjMap, d) ) << (nKeeps[0] + nKeeps[1]);
        uSim |= (Vec_WrdEntry(&p->vSets[0], d) & uMaskKeep[0]) | ((Vec_WrdEntry(&p->vSets[1], d) & uMaskKeep[1]) << nKeeps[0]);
        Vec_WrdWriteEntry( &p->vObjSims, Vec_IntEntry(&p->vObjMap, d), uSim );
    }
}
*/

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Sfm_DecPrepareSolver( Sfm_Dec_t * p )
{
    Vec_Int_t * vRoots = &p->vObjRoots;
    Vec_Int_t * vFaninVars = &p->vTemp2;
    Vec_Int_t * vLevel, * vClause;
    int i, k, Gate, iObj, RetValue;
    int nTfiSize = p->iTarget + 1; // including node
    int nWinSize = Vec_IntSize(&p->vObjGates);
    int nSatVars = 2 * nWinSize - nTfiSize;
    assert( nWinSize == Vec_IntSize(&p->vObjGates) );
    assert( p->iTarget < nWinSize );
    // create SAT solver
    sat_solver_restart( p->pSat );
    sat_solver_setnvars( p->pSat, nSatVars + Vec_IntSize(vRoots) );
    // add CNF clauses for the TFI
    Vec_IntForEachEntry( &p->vObjGates, Gate, i )
    {
        if ( Gate == -1 )
            continue;
        // generate CNF 
        vLevel = Vec_WecEntry( &p->vObjFanins, i );
        Vec_IntPush( vLevel, i );
        Sfm_TranslateCnf( &p->vClauses, (Vec_Str_t *)Vec_WecEntry(&p->vGateCnfs, Gate), vLevel, -1 );
        Vec_IntPop( vLevel );
        // add clauses
        Vec_WecForEachLevel( &p->vClauses, vClause, k )
        {
            if ( Vec_IntSize(vClause) == 0 )
                break;
            RetValue = sat_solver_addclause( p->pSat, Vec_IntArray(vClause), Vec_IntArray(vClause) + Vec_IntSize(vClause) );
            if ( RetValue == 0 )
                return 0;
        }
    }
    // add CNF clauses for the TFO
    Vec_IntForEachEntryStart( &p->vObjGates, Gate, i, nTfiSize )
    {
        assert( Gate != -1 );
        vLevel = Vec_WecEntry( &p->vObjFanins, i );
        Vec_IntClear( vFaninVars );
        Vec_IntForEachEntry( vLevel, iObj, k )
            Vec_IntPush( vFaninVars, iObj <= p->iTarget ? iObj : iObj + nWinSize - nTfiSize );
        Vec_IntPush( vFaninVars, i + nWinSize - nTfiSize );
        // generate CNF 
        Sfm_TranslateCnf( &p->vClauses, (Vec_Str_t *)Vec_WecEntry(&p->vGateCnfs, Gate), vFaninVars, p->iTarget );
        // add clauses
        Vec_WecForEachLevel( &p->vClauses, vClause, k )
        {
            if ( Vec_IntSize(vClause) == 0 )
                break;
            RetValue = sat_solver_addclause( p->pSat, Vec_IntArray(vClause), Vec_IntArray(vClause) + Vec_IntSize(vClause) );
            if ( RetValue == 0 )
                return 0;
        }
    }
    if ( nTfiSize < nWinSize )
    {
        // create XOR clauses for the roots
        Vec_IntClear( vFaninVars );
        Vec_IntForEachEntry( vRoots, iObj, i )
        {
            Vec_IntPush( vFaninVars, Abc_Var2Lit(nSatVars, 0) );
            sat_solver_add_xor( p->pSat, iObj, iObj + nWinSize - nTfiSize, nSatVars++, 0 );
        }
        // make OR clause for the last nRoots variables
        RetValue = sat_solver_addclause( p->pSat, Vec_IntArray(vFaninVars), Vec_IntLimit(vFaninVars) );
        if ( RetValue == 0 )
            return 0;
        assert( nSatVars == sat_solver_nvars(p->pSat) );
    }
    else assert( Vec_IntSize(vRoots) == 1 );
    // finalize
    RetValue = sat_solver_simplify( p->pSat );
    return 1;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Sfm_DecFindCost( Sfm_Dec_t * p, int c, int iLit, word Mask )
{
    int Value0 = Abc_TtCountOnes( Vec_WrdEntry(&p->vSets[!c], Abc_Lit2Var(iLit)) & Mask );
    int Cost0 = Abc_LitIsCompl(iLit) ? Abc_TtCountOnes( p->uMask[!c] & Mask ) - Value0 : Value0;
    return Cost0;
}
void Sfm_DecPrint( Sfm_Dec_t * p, word * Masks )
{
    int c, i, k, Entry;
    for ( c = 0; c < 2; c++ )
    {
        Vec_Int_t * vLevel = Vec_WecEntry( &p->vObjFanins, p->iTarget );
        printf( "%s-SET of object %d (divs = %d) with gate \"%s\" and fanins: ", 
            c ? "OFF": "ON", p->iTarget, p->nDivs, 
            Mio_GateReadName((Mio_Gate_t *)Vec_PtrEntry(&p->vGateHands, Vec_IntEntry(&p->vObjGates,p->iTarget))) );
        Vec_IntForEachEntry( vLevel, Entry, i )
            printf( "%d ", Entry );
        printf( "\n" );

        printf( "Implications: " );
        Vec_IntForEachEntry( &p->vImpls[c], Entry, i )
            printf( "%s%d(%d) ", Abc_LitIsCompl(Entry)? "!":"", Abc_Lit2Var(Entry), Sfm_DecFindCost(p, c, Entry, Masks ? Masks[!c] : ~(word)0) );
        printf( "\n" );
        printf( "     " );
        for ( i = 0; i < p->nDivs; i++ )
            printf( "%d", (i / 10) % 10 );
        printf( "\n" );
        printf( "     " );
        for ( i = 0; i < p->nDivs; i++ )
            printf( "%d", i % 10 );
        printf( "\n" );
        for ( k = 0; k < p->nPats[c]; k++ )
        {
            printf( "%2d : ", k );
            for ( i = 0; i < p->nDivs; i++ )
                printf( "%d", (int)((Vec_WrdEntry(&p->vSets[c], i) >> k) & 1) );
            printf( "\n" );
        }
        //printf( "\n" );
    }
}
int Sfm_DecPeformDecOne( Sfm_Dec_t * p, int * pfConst )
{
    int fVerbose = p->pPars->fVeryVerbose;
    int nBTLimit = p->pPars->nBTLimit;
    int i, k, c, Entry, status, Lits[3], RetValue;
    int iLitBest = -1, iCBest = -1, CostMin = ABC_INFINITY, Cost;
    abctime clk;
    *pfConst = -1;
    // check stuck-at-0/1 (on/off-set empty)
    p->nPats[0] = p->nPats[1] = 0;
    p->uMask[0] = p->uMask[1] = 0;
    Vec_IntClear( &p->vImpls[0] );
    Vec_IntClear( &p->vImpls[1] );
    Vec_WrdClear( &p->vSets[0] );
    Vec_WrdClear( &p->vSets[1] );
    for ( c = 0; c < 2; c++ )
    {
        p->nSatCalls++;
        Lits[0] = Abc_Var2Lit( p->iTarget, c );
        clk = Abc_Clock();
        status = sat_solver_solve( p->pSat, Lits, Lits + 1, nBTLimit, 0, 0, 0 );
        if ( status == l_Undef )
        {
            p->nTimeOuts++;
            return -2;
        }
        if ( status == l_False )
        {
            p->nSatCallsUnsat++;
            p->timeSatUnsat += Abc_Clock() - clk;
            *pfConst = c;
            return -1;
        }
        assert( status == l_True );
        p->nSatCallsSat++;
        p->timeSatSat += Abc_Clock() - clk;
        // record this status
        for ( i = 0; i < p->nDivs; i++ )
            Vec_WrdPush( &p->vSets[c], (word)sat_solver_var_value(p->pSat, i) );
        p->nPats[c]++;
        p->uMask[c] = 1;
    }
    // proceed checking divisors based on their values
    for ( c = 0; c < 2; c++ )
    {
        Lits[0] = Abc_Var2Lit( p->iTarget, c );
        for ( i = 0; i < p->nDivs; i++ )
        {
            word Column = Vec_WrdEntry(&p->vSets[c], i);
            if ( Column != 0 && Column != p->uMask[c] ) // diff value is possible
                continue;
            p->nSatCalls++;
            Lits[1] = Abc_Var2Lit( i, Column != 0 );
            clk = Abc_Clock();
            status = sat_solver_solve( p->pSat, Lits, Lits + 2, nBTLimit, 0, 0, 0 );
            if ( status == l_Undef )
            {
                p->nTimeOuts++;
                return -2;
            }
            if ( status == l_False )
            {
                p->nSatCallsUnsat++;
                p->timeSatUnsat += Abc_Clock() - clk;
                Vec_IntPush( &p->vImpls[c], Abc_LitNot(Lits[1]) );
                continue;
            }
            assert( status == l_True );
            p->nSatCallsSat++;
            p->timeSatSat += Abc_Clock() - clk;
            if ( p->nPats[c] == 64 )
            {
                p->nSatCallsOver++;
                continue;
            }
            // record this status
            for ( k = 0; k < p->nDivs; k++ )
                if ( sat_solver_var_value(p->pSat, k) )
                    *Vec_WrdEntryP(&p->vSets[c], k) |= ((word)1 << p->nPats[c]);
            p->uMask[c] |= ((word)1 << p->nPats[c]++);
        }
    }
    // find the best decomposition
    for ( c = 0; c < 2; c++ )
    {
        Vec_IntForEachEntry( &p->vImpls[c], Entry, i )
        {
            Cost = Sfm_DecFindCost(p, c, Entry, (~(word)0));
            if ( CostMin > Cost )
            {
                CostMin = Cost;
                iLitBest = Entry;
                iCBest = c;
            }
        }
    }
    if ( CostMin == ABC_INFINITY )
    {
        p->nNoDecs++;
        return -2;
    }

    // add clause
    Lits[0] = Abc_Var2Lit( p->iTarget, iCBest );
    Lits[1] = iLitBest;
    RetValue = sat_solver_addclause( p->pSat, Lits, Lits + 2 );
    if ( RetValue == 0 )
        return -1;

    // print the results
    if ( fVerbose )
    {
        printf( "\nBest literal (%d; %s%d) with weight %d.\n\n", iCBest, Abc_LitIsCompl(iLitBest)? "!":"", Abc_Lit2Var(iLitBest), CostMin );
        Sfm_DecPrint( p, NULL );
    }
    return Abc_Var2Lit( iLitBest, iCBest );
}
int Sfm_DecPeformDec( Sfm_Dec_t * p )
{
    Vec_Int_t * vLevel;
    int i, Dec, Last, fCompl, Pol, fConst = -1, nNodes = Vec_IntSize(&p->vObjGates);
    // perform decomposition
    Vec_IntClear( &p->vObjDec );
    for ( i = 0; i <= p->nMffc; i++ )
    {
        Dec = Sfm_DecPeformDecOne( p, &fConst );
        if ( Dec == -2 )
        {
            if ( p->pPars->fVeryVerbose )
                printf( "There is no decomposition (or time out occurred).\n" );
            return -1;
        }
        if ( Dec == -1 )
            break;
        Vec_IntPush( &p->vObjDec, Dec );
    }
    if ( i == p->nMffc + 1 )
    {
        if ( p->pPars->fVeryVerbose )
        printf( "Area-reducing decomposition is not found.\n" );
        return -1;
    }
    // check constant
    if ( Vec_IntSize(&p->vObjDec) == 0 )
    {
        assert( fConst >= 0 );
        // add gate
        Vec_IntPush( &p->vObjGates, fConst ? p->GateConst1 : p->GateConst0 );
        vLevel = Vec_WecPushLevel( &p->vObjFanins );
        // report
        if ( p->pPars->fVeryVerbose )
            printf( "Create constant %d.\n", fConst );
        return Vec_IntSize(&p->vObjDec);
    }
    // create network
    Last = Vec_IntPop( &p->vObjDec );
    fCompl = Abc_LitIsCompl(Last);
    Last = Abc_LitNotCond( Abc_Lit2Var(Last), fCompl );
    if ( Vec_IntSize(&p->vObjDec) == 0 )
    {
        // add gate
        Vec_IntPush( &p->vObjGates, Abc_LitIsCompl(Last) ? p->GateInvert : p->GateBuffer );
        vLevel = Vec_WecPushLevel( &p->vObjFanins );
        Vec_IntPush( vLevel, Abc_Lit2Var(Last) );
        // report
        if ( p->pPars->fVeryVerbose )
            printf( "Create buf/inv %d = %s%d.\n", nNodes, Abc_LitIsCompl(Last) ? "!":"", Abc_Lit2Var(Last) );
        return Vec_IntSize(&p->vObjDec);
    }
    Vec_IntForEachEntryReverse( &p->vObjDec, Dec, i )
    {
        fCompl = Abc_LitIsCompl(Dec);
        Dec = Abc_LitNotCond( Abc_Lit2Var(Dec), fCompl );
        // add gate
        Pol = (Abc_LitIsCompl(Last) << 1) | Abc_LitIsCompl(Dec);
        if ( fCompl )
            Vec_IntPush( &p->vObjGates, p->GateOr[Pol] );
        else
            Vec_IntPush( &p->vObjGates, p->GateAnd[Pol] );
        vLevel = Vec_WecPushLevel( &p->vObjFanins );
        Vec_IntPush( vLevel, Abc_Lit2Var(Dec) );
        Vec_IntPush( vLevel, Abc_Lit2Var(Last) );
        // report
        if ( p->pPars->fVeryVerbose )
            printf( "Create node %s%d = %s%d and %s%d (gate %d).\n", 
                fCompl? "!":"", nNodes,
                Abc_LitIsCompl(Last)? "!":"", Abc_Lit2Var(Last),  
                Abc_LitIsCompl(Dec)? "!":"", Abc_Lit2Var(Dec), Pol  );
        // prepare for the next one
        Last = Abc_Var2Lit( nNodes, 0 );
        nNodes++;
    }
    //printf( "\n" );
    return Vec_IntSize(&p->vObjDec);
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Sfm_DecMffcArea( Abc_Ntk_t * pNtk, Vec_Int_t * vMffc )
{
    Abc_Obj_t * pObj; 
    int i, nAreaMffc = 0;
    Abc_NtkForEachObjVec( vMffc, pNtk, pObj, i )
        nAreaMffc += (int)(MIO_NUM * Mio_GateReadArea((Mio_Gate_t *)pObj->pData));
    return nAreaMffc;
}
int Sfm_MffcDeref_rec( Abc_Obj_t * pObj )
{
    Abc_Obj_t * pFanin;
    int i, Area = (int)(MIO_NUM*Mio_GateReadArea((Mio_Gate_t *)pObj->pData));
    Abc_ObjForEachFanin( pObj, pFanin, i )
    {
        assert( pFanin->vFanouts.nSize > 0 );
        if ( --pFanin->vFanouts.nSize == 0 && !Abc_ObjIsCi(pFanin) )
            Area += Sfm_MffcDeref_rec( pFanin );
    }
    return Area;
}
int Sfm_MffcRef_rec( Abc_Obj_t * pObj )
{
    Abc_Obj_t * pFanin;
    int i, Area = (int)(MIO_NUM*Mio_GateReadArea((Mio_Gate_t *)pObj->pData));
    Abc_ObjForEachFanin( pObj, pFanin, i )
    {
        if ( pFanin->vFanouts.nSize++ == 0 && !Abc_ObjIsCi(pFanin) )
            Area += Sfm_MffcRef_rec( pFanin );
    }
    return Area;
}
int Sfm_DecMffcAreaReal( Abc_Obj_t * pPivot, Vec_Int_t * vCut )
{
    Abc_Ntk_t * pNtk = pPivot->pNtk;
    Abc_Obj_t * pObj; 
    int i, Area1, Area2;
    assert( Abc_ObjIsNode(pPivot) );
    Abc_NtkForEachObjVec( vCut, pNtk, pObj, i )
        pObj->vFanouts.nSize++;
    Area1 = Sfm_MffcDeref_rec( pPivot );
    Area2 = Sfm_MffcRef_rec( pPivot );
    Abc_NtkForEachObjVec( vCut, pNtk, pObj, i )
        pObj->vFanouts.nSize--;
    assert( Area1 == Area2 );
    return Area1;
}
void Sfm_DecPrepareVec( Vec_Int_t * vMap, int * pNodes, int nNodes, Vec_Int_t * vCut )
{
    int i;
    Vec_IntClear( vCut );
    for ( i = 0; i < nNodes; i++ )
        Vec_IntPush( vCut, Vec_IntEntry(vMap, pNodes[i]) );    
}
int Sfm_DecComputeFlipInvGain( Sfm_Dec_t * p, Abc_Obj_t * pPivot, int * pfNeedInv )
{
    Abc_Obj_t * pFanout; 
    Mio_Gate_t * pGate, * pGateNew;
    int i, Handle, fNeedInv = 0, Gain = 0;
    Abc_ObjForEachFanout( pPivot, pFanout, i )
    {
        if ( !Abc_ObjIsNode(pFanout) )
        {
            fNeedInv = 1;
            continue;
        }
        pGate = (Mio_Gate_t*)pFanout->pData;
        if ( Abc_ObjFaninNum(pFanout) == 1 && Mio_GateIsInv(pGate) )
        {
            Gain += p->AreaInv;
            continue;
        }
        Handle = Sfm_LibFindComplInputGate( &p->vGateFuncs, Mio_GateReadValue(pGate), Abc_ObjFaninNum(pFanout), Abc_NodeFindFanin(pFanout, pPivot), NULL );
        if ( Handle == -1 )
        {
            fNeedInv = 1;
            continue;
        }
        pGateNew = (Mio_Gate_t *)Vec_PtrEntry( &p->vGateHands, Handle );
        Gain += MIO_NUM*Mio_GateReadArea(pGate) - MIO_NUM*Mio_GateReadArea(pGateNew);
    }
    if ( fNeedInv )
        Gain -= p->AreaInv;
    if ( pfNeedInv )
        *pfNeedInv = fNeedInv;
    return Gain;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Sfm_DecCombineDec( Sfm_Dec_t * p, word * pTruth0, word * pTruth1, int * pSupp0, int * pSupp1, int nSupp0, int nSupp1, word * pTruth, int * pSupp, int Var )
{
    Vec_Int_t vVec0 = { 2*SFM_SUPP_MAX, nSupp0, pSupp0 };
    Vec_Int_t vVec1 = { 2*SFM_SUPP_MAX, nSupp1, pSupp1 };
    Vec_Int_t vVec  = { 2*SFM_SUPP_MAX, 0,      pSupp  };
    int nWords0 = Abc_TtWordNum(nSupp0);
    int nSupp, iSuppVar;
    // check the case of equal cofactors
    if ( nSupp0 == nSupp1 && !memcmp(pSupp0, pSupp1, sizeof(int)*nSupp0) && !memcmp(pTruth0, pTruth1, sizeof(word)*nWords0) )
    {
        memcpy( pSupp,  pSupp0,  sizeof(int)*nSupp0   );
        memcpy( pTruth, pTruth0, sizeof(word)*nWords0 );
        Abc_TtStretch6( pTruth, nSupp0, p->pPars->nVarMax );
        return nSupp0;
    }
    // merge support variables
    Vec_IntTwoMerge2Int( &vVec0, &vVec1, &vVec );
    Vec_IntPushOrder( &vVec, Var );
    nSupp = Vec_IntSize( &vVec );
    if ( nSupp > p->pPars->nVarMax )
        return -2;
    // expand truth tables
    Abc_TtStretch6( pTruth0, nSupp0, nSupp );
    Abc_TtStretch6( pTruth1, nSupp1, nSupp );
    Abc_TtExpand( pTruth0, nSupp, pSupp0, nSupp0, pSupp, nSupp );
    Abc_TtExpand( pTruth1, nSupp, pSupp1, nSupp1, pSupp, nSupp );
    // perform operation
    iSuppVar = Vec_IntFind( &vVec, Var );
    Abc_TtMux( pTruth, p->pTtElems[iSuppVar], pTruth1, pTruth0, Abc_TtWordNum(nSupp) );
    Abc_TtStretch6( pTruth, nSupp, p->pPars->nVarMax );
    return nSupp;
}
int Sfm_DecPeformDec_rec( Sfm_Dec_t * p, word * pTruth, int * pSupp, int * pAssump, int nAssump, word Masks[2], int fCofactor, int nSuppAdd )
{
    int nBTLimit = p->pPars->nBTLimit;
//    int fVerbose = p->pPars->fVeryVerbose;
    int Var = -1, CostMin = ABC_INFINITY;
    int c, i, d, iLit, Cost, status;
    abctime clk;
    assert( nAssump <= SFM_SUPP_MAX );
    if ( p->pPars->fVeryVerbose )
    {
        printf( "\nObject %d\n", p->iTarget );
        printf( "Divs = %d.  Nodes = %d.  Mffc = %d.  Mffc area = %.2f.    ", p->nDivs, Vec_IntSize(&p->vObjGates), p->nMffc, MIO_NUMINV*p->AreaMffc );
        printf( "Pat0 = %d.  Pat1 = %d.    ", p->nPats[0], p->nPats[1] );
        printf( "\n" );
        if ( nAssump )
        {
            printf( "Cofactor: " );
            for ( i = 0; i < nAssump; i++ )
                printf( " %s%d", Abc_LitIsCompl(pAssump[i])? "!":"", Abc_Lit2Var(pAssump[i]) );
            printf( "\n" );
        }
    }
    // check constant
    for ( c = 0; c < 2; c++ )
    {
        if ( p->uMask[c] & Masks[c] ) // there are some patterns
            continue;
        p->nSatCalls++;
        pAssump[nAssump] = Abc_Var2Lit( p->iTarget, c );
        clk = Abc_Clock();
        status = sat_solver_solve( p->pSat, pAssump, pAssump + nAssump + 1, nBTLimit, 0, 0, 0 );
        if ( status == l_Undef )
        {
            p->nTimeOuts++;
            return -2;
        }
        if ( status == l_False )
        {
            p->nSatCallsUnsat++;
            p->timeSatUnsat += Abc_Clock() - clk;
            Abc_TtConst( pTruth, Abc_TtWordNum(p->pPars->nVarMax), c );
            if ( p->pPars->fVeryVerbose )
                printf( "Found constant %d.\n", c );
            return 0;
        }
        assert( status == l_True );
        p->nSatCallsSat++;
        p->timeSatSat += Abc_Clock() - clk;
        if ( p->nPats[c] == 64 )
        {
            p->nSatCallsOver++;
            continue;//return -2;//continue;
        }
        for ( i = 0; i < p->nDivs; i++ )
            if ( sat_solver_var_value(p->pSat, i) )
                *Vec_WrdEntryP(&p->vSets[c], i) |= ((word)1 << p->nPats[c]);
        p->uMask[c] |= ((word)1 << p->nPats[c]++);
    }

    if ( p->iUseThis != -1 )
    {
        Var = p->iUseThis;
        p->iUseThis = -1;
        goto cofactor;
    }

    // check implications
    Vec_IntClear( &p->vImpls[0] );
    Vec_IntClear( &p->vImpls[1] );
    for ( d = 0; d < p->nDivs; d++ )
    {
        int Impls[2] = {-1, -1};
        for ( c = 0; c < 2; c++ )
        {
            word MaskAll = p->uMask[c] & Masks[c];
            word MaskCur = Vec_WrdEntry(&p->vSets[c], d) & Masks[c];
            if ( MaskAll != 0 && MaskCur != 0 && MaskCur != MaskAll ) // has both ones and zeros
                continue;
            p->nSatCalls++;
            pAssump[nAssump]   = Abc_Var2Lit( p->iTarget, c );
            pAssump[nAssump+1] = Abc_Var2Lit( d, MaskCur != 0 );
            clk = Abc_Clock();
            status = sat_solver_solve( p->pSat, pAssump, pAssump + nAssump + 2, nBTLimit, 0, 0, 0 );
            if ( status == l_Undef )
            {
                p->nTimeOuts++;
                return -2;
            }
            if ( status == l_False )
            {
                p->nSatCallsUnsat++;
                p->timeSatUnsat += Abc_Clock() - clk;
                Impls[c] = Abc_LitNot(pAssump[nAssump+1]);
                Vec_IntPush( &p->vImpls[c], Abc_LitNot(pAssump[nAssump+1]) );
                continue;
            }
            assert( status == l_True );
            p->nSatCallsSat++;
            p->timeSatSat += Abc_Clock() - clk;
            if ( p->nPats[c] == 64 )
            {
                p->nSatCallsOver++;
                continue;//return -2;//continue;
            }
            // record this status
            for ( i = 0; i < p->nDivs; i++ )
                if ( sat_solver_var_value(p->pSat, i) )
                    *Vec_WrdEntryP(&p->vSets[c], i) |= ((word)1 << p->nPats[c]);
            p->uMask[c] |= ((word)1 << p->nPats[c]++);
        }
        if ( Impls[0] == -1 || Impls[1] == -1 )
            continue;
        if ( Impls[0] == Impls[1] )
        {
            Vec_IntPop( &p->vImpls[0] );
            Vec_IntPop( &p->vImpls[1] );
            continue;
        }
        assert( Abc_Lit2Var(Impls[0]) == Abc_Lit2Var(Impls[1]) );
        // found buffer/inverter
        Abc_TtUnit( pTruth, Abc_TtWordNum(p->pPars->nVarMax), Abc_LitIsCompl(Impls[0]) );
        pSupp[0] = Abc_Lit2Var(Impls[0]);
        if ( p->pPars->fVeryVerbose )
            printf( "Found variable %s%d.\n", Abc_LitIsCompl(Impls[0]) ? "!":"", pSupp[0] );
        return 1;        
    }
    if ( nSuppAdd > p->pPars->nVarMax - 2 )
    {
        if ( p->pPars->fVeryVerbose )
            printf( "The number of assumption is more than MFFC size.\n" );
        return -2;
    }
    // try using all implications at once
    if ( p->pPars->fUseAndOr )
    for ( c = 0; c < 2; c++ )
    {
        if ( Vec_IntSize(&p->vImpls[!c]) < 2 )
            continue;
        p->nSatCalls++;
        pAssump[nAssump] = Abc_Var2Lit( p->iTarget, c );
        assert( Vec_IntSize(&p->vImpls[!c]) < SFM_WIN_MAX-10 );
        Vec_IntForEachEntry( &p->vImpls[!c], iLit, i )
            pAssump[nAssump+1+i] = iLit;
        clk = Abc_Clock();
        status = sat_solver_solve( p->pSat, pAssump, pAssump + nAssump+1+i, nBTLimit, 0, 0, 0 );
        if ( status == l_Undef )
        {
            p->nTimeOuts++;
            return -2;
        }
        if ( status == l_False )
        {
            int * pFinal, nFinal = sat_solver_final( p->pSat, &pFinal );
            p->nSatCallsUnsat++;
            p->timeSatUnsat += Abc_Clock() - clk;
            if ( nFinal + nSuppAdd > 6 )
                continue;
            // collect only relevant literals
            for ( i = d = 0; i < nFinal; i++ )
                if ( Vec_IntFind(&p->vImpls[!c], Abc_LitNot(pFinal[i])) >= 0 )
                    pSupp[d++] = Abc_LitNot(pFinal[i]);
            nFinal = d;
            // create AND/OR gate
            assert( nFinal <= 6 );
            if ( c )
            {
                *pTruth = ~(word)0;
                for ( i = 0; i < nFinal; i++ )
                {
                    *pTruth &= Abc_LitIsCompl(pSupp[i]) ? ~s_Truths6[i] : s_Truths6[i];
                    pSupp[i] = Abc_Lit2Var(pSupp[i]);
                }
            }
            else
            {
                *pTruth = 0;
                for ( i = 0; i < nFinal; i++ )
                {
                    *pTruth |= Abc_LitIsCompl(pSupp[i]) ? s_Truths6[i] : ~s_Truths6[i];
                    pSupp[i] = Abc_Lit2Var(pSupp[i]);
                }
            }
            Abc_TtStretch6( pTruth, nFinal, p->pPars->nVarMax );
            p->nNodesAndOr++;
            if ( p->pPars->fVeryVerbose )
                printf( "Found %d-input AND/OR gate.\n", nFinal );
            return nFinal;
        }
        assert( status == l_True );
        p->nSatCallsSat++;
        p->timeSatSat += Abc_Clock() - clk;
        if ( p->nPats[c] == 64 )
        {
            p->nSatCallsOver++;
            continue;//return -2;//continue;
        }
        for ( i = 0; i < p->nDivs; i++ )
            if ( sat_solver_var_value(p->pSat, i) )
                *Vec_WrdEntryP(&p->vSets[c], i) |= ((word)1 << p->nPats[c]);
        p->uMask[c] |= ((word)1 << p->nPats[c]++);
    }

    // find the best cofactoring variable
//    if ( !fCofactor || Vec_IntSize(&p->vImpls[0]) + Vec_IntSize(&p->vImpls[1]) > 2 )
    for ( c = 0; c < 2; c++ )
    {
        Vec_IntForEachEntry( &p->vImpls[c], iLit, i )
        {
            if ( Vec_IntSize(&p->vImpls[c]) > 1 && Vec_IntFind(&p->vObjDec, Abc_Lit2Var(iLit)) >= 0 )
                continue;
            Cost = Sfm_DecFindCost( p, c, iLit, Masks[!c] );
            if ( CostMin > Cost )
            {
                CostMin = Cost;
                Var = Abc_Lit2Var(iLit);
            }
        }
    }

/*
    {
        int Lit0 = Vec_IntSize(&p->vImpls[0]) ? Vec_IntEntry(&p->vImpls[0], 0) : -1;
        int Lit1 = Vec_IntSize(&p->vImpls[1]) ? Vec_IntEntry(&p->vImpls[1], 0) : -1;
        if ( Lit0 == -1 && Lit1 >= 0 )
            Var = Abc_Lit2Var(Lit1);
        else if ( Lit1 == -1 && Lit0 >= 0 )
            Var = Abc_Lit2Var(Lit0);
        else if ( Lit0 >= 0 && Lit1 >= 0 )
        {
            if ( Lit0 < Lit1 )
                Var = Abc_Lit2Var(Lit0);
            else
                Var = Abc_Lit2Var(Lit1);
        }
    }
*/

    if ( Var == -1 && fCofactor )
    {
        //for ( Var = p->nDivs - 1; Var >= 0; Var-- )
        Vec_IntForEachEntryReverse( &p->vObjInMffc, Var, i )
            if ( Vec_IntFind(&p->vObjDec, Var) == -1 )
                break;
//        if ( i == Vec_IntSize(&p->vObjInMffc) )
        if ( i == -1 )
            Var = -1;
        fCofactor = 0;
    }

    if ( p->pPars->fVeryVerbose )
    {
        Sfm_DecPrint( p, Masks );
        printf( "Best var %d with weight %d.  Cofactored = %s\n", Var, CostMin, Var == p->nDivs - 1 ? "yes" : "no" );
        printf( "\n" );
    }
cofactor:
    // cofactor the problem
    if ( Var >= 0 )
    {
        word uTruth[2][SFM_WORD_MAX], MasksNext[2];
        int Supp[2][2*SFM_SUPP_MAX], nSupp[2] = {0};
        Vec_IntPush( &p->vObjDec, Var );
        for ( i = 0; i < 2; i++ )
        {
            for ( c = 0; c < 2; c++ )
            {
                word MaskVar = Vec_WrdEntry(&p->vSets[c], Var);
                MasksNext[c] = Masks[c] & (i ? MaskVar | ~p->uMask[c] : ~MaskVar);
            }
            pAssump[nAssump] = Abc_Var2Lit( Var, !i );
            nSupp[i] = Sfm_DecPeformDec_rec( p, uTruth[i], Supp[i], pAssump, nAssump+1, MasksNext, fCofactor, (i ? nSupp[0] : 0) + nSuppAdd + 1 );
            if ( nSupp[i] == -2 )
                return -2;
        }
        // combine solutions
        return Sfm_DecCombineDec( p, uTruth[0], uTruth[1], Supp[0], Supp[1], nSupp[0], nSupp[1], pTruth, pSupp, Var );
    }
    return -2;
}
int Sfm_DecPeformDec2( Sfm_Dec_t * p, Abc_Obj_t * pObj )
{
    word uTruth[SFM_DEC_MAX][SFM_WORD_MAX], Masks[2];
    int pSupp[SFM_DEC_MAX][2*SFM_SUPP_MAX];
    int nSupp[SFM_DEC_MAX], pAssump[SFM_WIN_MAX];
    int fVeryVerbose = p->pPars->fPrintDecs || p->pPars->fVeryVerbose;
    int nDecs = Abc_MaxInt(p->pPars->nDecMax, 1);
    //int fNeedInv, AreaGainInv = Sfm_DecComputeFlipInvGain(p, pObj, &fNeedInv);
    int i, RetValue, Prev = 0, iBest = -1, AreaThis, AreaNew;//, AreaNewInv;
    int GainThis, GainBest = -1, iLibObj, iLibObjBest = -1; 
    assert( p->pPars->fArea == 1 );
//printf( "AreaGainInv = %8.2f  ", MIO_NUMINV*AreaGainInv );
    if ( p->pPars->fUseSim )
        Sfm_ObjSetupSimInfo( pObj );
    else
    {
        p->nPats[0] = p->nPats[1] = 0;
        p->uMask[0] = p->uMask[1] = 0;
        Vec_WrdFill( &p->vSets[0], p->nDivs, 0 );
        Vec_WrdFill( &p->vSets[1], p->nDivs, 0 );
    }
    //Sfm_DecPrint( p, NULL );
    if ( fVeryVerbose )
        printf( "\nNode %4d : MFFC %2d\n", p->iTarget, p->nMffc );
    assert( p->pPars->nDecMax <= SFM_DEC_MAX );
    Vec_IntClear( &p->vObjDec );
    for ( i = 0; i < nDecs; i++ )
    {
        // reduce the variable array
        if ( Vec_IntSize(&p->vObjDec) > Prev )
            Vec_IntShrink( &p->vObjDec, Prev );
        Prev = Vec_IntSize(&p->vObjDec) + 1;
        // perform decomposition 
        Masks[0] = Masks[1] = ~(word)0;
        nSupp[i] = Sfm_DecPeformDec_rec( p, uTruth[i], pSupp[i], pAssump, 0, Masks, 1, 0 );
        if ( nSupp[i] == -2 )
        {
            if ( fVeryVerbose )
                printf( "Dec  %d: Pat0 = %2d  Pat1 = %2d  NO DEC.\n", i, p->nPats[0], p->nPats[1] );
            continue;
        }
        if ( fVeryVerbose )
            printf( "Dec  %d: Pat0 = %2d  Pat1 = %2d  Supp = %d  ", i, p->nPats[0], p->nPats[1], nSupp[i] );
        if ( fVeryVerbose )
            Dau_DsdPrintFromTruth( uTruth[i], nSupp[i] );
        if ( nSupp[i] < 2 )
        {
            p->nSuppVars = nSupp[i];
            Abc_TtCopy( p->Copy, uTruth[i], SFM_WORD_MAX, 0 ); 
            RetValue = Sfm_LibImplementSimple( p->pLib, uTruth[i], pSupp[i], nSupp[i], &p->vObjGates, &p->vObjFanins );
            assert( nSupp[i] <= p->pPars->nVarMax );
            p->nLuckySizes[nSupp[i]]++;
            assert( RetValue <= 2 );
            p->nLuckyGates[RetValue]++;
            //printf( "\n" );
            return RetValue;
        }

        p->nSuppVars = nSupp[i];
        Abc_TtCopy( p->Copy, uTruth[i], SFM_WORD_MAX, 0 ); 
        AreaNew = Sfm_LibFindAreaMatch( p->pLib, uTruth[i], nSupp[i], &iLibObj );
/*
        uTruth[i][0] = ~uTruth[i][0];
        AreaNewInv = Sfm_LibFindAreaMatch( p->pLib, uTruth[i], nSupp[i], NULL );
        uTruth[i][0] = ~uTruth[i][0];

        if ( AreaNew > 0 && AreaNewInv > 0 && AreaNew - AreaNewInv + AreaGainInv > 0 )
            printf( "AreaNew = %8.2f   AreaNewInv = %8.2f   Gain = %8.2f   Total = %8.2f\n", 
                MIO_NUMINV*AreaNew, MIO_NUMINV*AreaNewInv, MIO_NUMINV*(AreaNew - AreaNewInv), MIO_NUMINV*(AreaNew - AreaNewInv + AreaGainInv) );
        else
            printf( "\n" );
*/
        if ( AreaNew == -1 )
            continue;


        // compute area savings
        Sfm_DecPrepareVec( &p->vObjMap, pSupp[i], nSupp[i], &p->vTemp );
        AreaThis = Sfm_DecMffcAreaReal(pObj, &p->vTemp);
        assert( p->AreaMffc <= AreaThis );
        if ( p->pPars->fZeroCost ? (AreaNew > AreaThis) : (AreaNew >= AreaThis) )
            continue;
        // find the best gain
        GainThis = AreaThis - AreaNew;
        assert( GainThis >= 0 );
        if ( GainBest < GainThis )
        {
            GainBest    = GainThis;
            iLibObjBest = iLibObj;
            iBest       = i;
        }
    }
    if ( p->pPars->fUseSim )
        Sfm_ObjSetdownSimInfo( pObj );
    if ( iBest == -1 )
    {
        if ( fVeryVerbose )
            printf( "Best  : NO DEC.\n" );
        p->nNoDecs++;
        //printf( "\n" );
        return -2;
    }
    if ( fVeryVerbose )
        printf( "Best %d: %d  ", iBest, nSupp[iBest] );
    if ( fVeryVerbose )
      Dau_DsdPrintFromTruth( uTruth[iBest], nSupp[iBest] );
    // implement
    assert( iLibObjBest >= 0 );
    RetValue = Sfm_LibImplementGatesArea( p->pLib, pSupp[iBest], nSupp[iBest], iLibObjBest, &p->vObjGates, &p->vObjFanins );
    assert( nSupp[iBest] <= p->pPars->nVarMax );
    p->nLuckySizes[nSupp[iBest]]++;
    assert( RetValue <= 2 );
    p->nLuckyGates[RetValue]++;
    return 1;
}
int Sfm_DecPeformDec3( Sfm_Dec_t * p, Abc_Obj_t * pObj )
{
    word uTruth[SFM_DEC_MAX][SFM_WORD_MAX], Masks[2];
    int pSupp[SFM_DEC_MAX][2*SFM_SUPP_MAX];
    int nSupp[SFM_DEC_MAX], pAssump[SFM_WIN_MAX];
    int fVeryVerbose = p->pPars->fPrintDecs || p->pPars->fVeryVerbose;
    int nDecs = Abc_MaxInt(p->pPars->nDecMax, 1);
    int i, k, DelayOrig = 0, DelayMin, nMatches, iBest = -1, RetValue, Prev = 0; 
    Mio_Gate_t * pGate1Best = NULL, * pGate2Best = NULL; 
    char * pFans1Best = NULL, * pFans2Best = NULL;
    assert( p->pPars->fArea == 0 );
    p->DelayMin = 0;
    if ( p->pPars->fUseSim )
        Sfm_ObjSetupSimInfo( pObj );
    else
    {
        p->nPats[0] = p->nPats[1] = 0;
        p->uMask[0] = p->uMask[1] = 0;
        Vec_WrdFill( &p->vSets[0], p->nDivs, 0 );
        Vec_WrdFill( &p->vSets[1], p->nDivs, 0 );
    }
    //Sfm_DecPrint( p, NULL );
    if ( fVeryVerbose )
        printf( "\nNode %4d : MFFC %2d\n", p->iTarget, p->nMffc );
    // set limit on search for decompositions in delay-model
    assert( p->pPars->nDecMax <= SFM_DEC_MAX );
    Vec_IntClear( &p->vObjDec );
    for ( i = 0; i < nDecs; i++ )
    {
        DelayMin = DelayOrig = Sfm_TimReadObjDelay( p->pTim, Abc_ObjId(pObj) );
        // reduce the variable array
        if ( Vec_IntSize(&p->vObjDec) > Prev )
            Vec_IntShrink( &p->vObjDec, Prev );
        Prev = Vec_IntSize(&p->vObjDec) + 1;
        // perform decomposition 
        Masks[0] = Masks[1] = ~(word)0;
        nSupp[i] = Sfm_DecPeformDec_rec( p, uTruth[i], pSupp[i], pAssump, 0, Masks, 1, 0 );
        if ( nSupp[i] == -2 )
        {
            if ( fVeryVerbose )
                printf( "Dec  %d: Pat0 = %2d  Pat1 = %2d  NO DEC.\n", i, p->nPats[0], p->nPats[1] );
            continue;
        }
        if ( fVeryVerbose )
            printf( "Dec  %d: Pat0 = %2d  Pat1 = %2d  Supp = %d  ", i, p->nPats[0], p->nPats[1], nSupp[i] );
        if ( fVeryVerbose )
            Dau_DsdPrintFromTruth( uTruth[i], nSupp[i] );
        if ( nSupp[i] == 1 && uTruth[i][0] == ABC_CONST(0x5555555555555555) && DelayMin <= p->DelayInv + Sfm_TimReadObjDelay(p->pTim, Vec_IntEntry(&p->vObjMap, pSupp[i][0])) )
        {
            if ( fVeryVerbose )
                printf( "Dec  %d: Pat0 = %2d  Pat1 = %2d  NO DEC.\n", i, p->nPats[0], p->nPats[1] );
            continue;
        }
        if ( nSupp[i] < 2 )
        {
            p->nSuppVars = nSupp[i];
            Abc_TtCopy( p->Copy, uTruth[i], SFM_WORD_MAX, 0 ); 
            RetValue = Sfm_LibImplementSimple( p->pLib, uTruth[i], pSupp[i], nSupp[i], &p->vObjGates, &p->vObjFanins );
            assert( nSupp[i] <= p->pPars->nVarMax );
            p->nLuckySizes[nSupp[i]]++;
            assert( RetValue <= 2 );
            p->nLuckyGates[RetValue]++;
            return RetValue;
        }
        //if ( pObj->Id == 689 )
        // {
        //    int s = 0;
        //}

        // try the delay
        p->nSuppVars = nSupp[i];
        Abc_TtCopy( p->Copy, uTruth[i], SFM_WORD_MAX, 0 ); 
        nMatches = Sfm_LibFindDelayMatches( p->pLib, uTruth[i], pSupp[i], nSupp[i], &p->vMatchGates, &p->vMatchFans );
        for ( k = 0; k < nMatches; k++ )
        {
            Mio_Gate_t * pGate1 = (Mio_Gate_t *)Vec_PtrEntry( &p->vMatchGates, 2*k+0 );
            Mio_Gate_t * pGate2 = (Mio_Gate_t *)Vec_PtrEntry( &p->vMatchGates, 2*k+1 );
            char * pFans1 = (char *)Vec_PtrEntry( &p->vMatchFans, 2*k+0 );
            char * pFans2 = (char *)Vec_PtrEntry( &p->vMatchFans, 2*k+1 );
            Vec_Int_t vFanins = { nSupp[i], nSupp[i], pSupp[i] };
            int Delay = Sfm_TimEvalRemapping( p->pTim, &vFanins, &p->vObjMap, pGate1, pFans1, pGate2, pFans2 );
            if ( DelayMin > Delay )
            {
                DelayMin   = Delay;
                pGate1Best = pGate1;
                pGate2Best = pGate2;
                pFans1Best = pFans1;
                pFans2Best = pFans2;
                iBest      = i;
            }
        }
    }
    if ( p->pPars->fUseSim )
        Sfm_ObjSetdownSimInfo( pObj );
    if ( iBest == -1 )
    {
        if ( fVeryVerbose )
            printf( "Best  : NO DEC.\n" );
        p->nNoDecs++;
        return -2;
    }
    if ( fVeryVerbose )
        printf( "Best %d: %d  ", iBest, nSupp[iBest] );
//    if ( fVeryVerbose )
//        Dau_DsdPrintFromTruth( uTruth[iBest], nSupp[iBest] );
    RetValue = Sfm_LibImplementGatesDelay( p->pLib, pSupp[iBest], pGate1Best, pGate2Best, pFans1Best, pFans2Best, &p->vObjGates, &p->vObjFanins );
    assert( nSupp[iBest] <= p->pPars->nVarMax );
    p->nLuckySizes[nSupp[iBest]]++;
    assert( RetValue <= 2 );
    p->nLuckyGates[RetValue]++;
    p->DelayMin = DelayMin;
    return 1;
}

/**Function*************************************************************

  Synopsis    [Incremental level update.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkUpdateIncLevel_rec( Abc_Obj_t * pObj )
{
    Abc_Obj_t * pFanout;
    int i, LevelNew = Abc_ObjLevelNew(pObj);
    if ( LevelNew == Abc_ObjLevel(pObj) && Abc_ObjIsNode(pObj) && Abc_ObjFaninNum(pObj) > 0 )
        return;
    pObj->Level = LevelNew;
    if ( !Abc_ObjIsCo(pObj) )
        Abc_ObjForEachFanout( pObj, pFanout, i )
            Abc_NtkUpdateIncLevel_rec( pFanout );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NtkDfsCheck_rec( Abc_Obj_t * pObj, Abc_Obj_t * pPivot )
{
    Abc_Obj_t * pFanin; int i;
    if ( pObj == pPivot )
        return 0;
    if ( Abc_NodeIsTravIdCurrent( pObj ) )
        return 1;
    Abc_NodeSetTravIdCurrent( pObj );
    if ( Abc_ObjIsCi(pObj) )
        return 1;
    assert( Abc_ObjIsNode(pObj) );
    Abc_ObjForEachFanin( pObj, pFanin, i )
        if ( !Abc_NtkDfsCheck_rec(pFanin, pPivot) )
            return 0;
    return 1;
}
void Abc_NtkDfsReverseOne_rec( Abc_Obj_t * pObj, Vec_Int_t * vTfo, int nLevelMax, int nFanoutMax )
{
    Abc_Obj_t * pFanout; int i;
    if ( Abc_NodeIsTravIdCurrent( pObj ) )
        return;
    Abc_NodeSetTravIdCurrent( pObj );
    if ( Abc_ObjIsCo(pObj) || Abc_ObjLevel(pObj) > nLevelMax )
        return;
    assert( Abc_ObjIsNode( pObj ) );
    if ( Abc_ObjFanoutNum(pObj) <= nFanoutMax )
    {
        Abc_ObjForEachFanout( pObj, pFanout, i )
            if ( Abc_ObjIsCo(pFanout) || Abc_ObjLevel(pFanout) > nLevelMax )
                break;
        if ( i == Abc_ObjFanoutNum(pObj) )
            Abc_ObjForEachFanout( pObj, pFanout, i )
                Abc_NtkDfsReverseOne_rec( pFanout, vTfo, nLevelMax, nFanoutMax );
    }
    Vec_IntPush( vTfo, Abc_ObjId(pObj) );
    pObj->iTemp = 0;
}
int Abc_NtkDfsOne_rec( Abc_Obj_t * pObj, Vec_Int_t * vTfi, int nLevelMin, int CiLabel )
{
    Abc_Obj_t * pFanin; int i;
    if ( Abc_NodeIsTravIdCurrent( pObj ) )
        return pObj->iTemp;
    Abc_NodeSetTravIdCurrent( pObj );
    if ( Abc_ObjIsCi(pObj) || (Abc_ObjLevel(pObj) < nLevelMin && Abc_ObjFaninNum(pObj) > 0) )
    {
        Vec_IntPush( vTfi, Abc_ObjId(pObj) );
        return (pObj->iTemp = CiLabel);
    }
    assert( Abc_ObjIsNode(pObj) );
    pObj->iTemp = Abc_ObjFaninNum(pObj) ? 0 : CiLabel;
    Abc_ObjForEachFanin( pObj, pFanin, i )
        pObj->iTemp |= Abc_NtkDfsOne_rec( pFanin, vTfi, nLevelMin, CiLabel );
    Vec_IntPush( vTfi, Abc_ObjId(pObj) );
    Sfm_ObjSimulateNode( pObj );
    return pObj->iTemp;
}
void Sfm_DecAddNode( Abc_Obj_t * pObj, Vec_Int_t * vMap, Vec_Int_t * vGates, int fSkip, int fVeryVerbose )
{
    if ( fVeryVerbose )
        printf( "%d:%d(%d) ", Vec_IntSize(vMap), Abc_ObjId(pObj), pObj->iTemp );
    if ( fVeryVerbose )
        Abc_ObjPrint( stdout, pObj );
    Vec_IntPush( vMap, Abc_ObjId(pObj) );
    Vec_IntPush( vGates, fSkip ? -1 : Mio_GateReadValue((Mio_Gate_t *)pObj->pData) );
}
static inline int Sfm_DecNodeIsMffc( Abc_Obj_t * p, int nLevelMin )
{
    return Abc_ObjIsNode(p) && Abc_ObjFanoutNum(p) == 1 && Abc_NodeIsTravIdCurrent(p) && (Abc_ObjLevel(p) >= nLevelMin || Abc_ObjFaninNum(p) == 0);
}
static inline int Sfm_DecNodeIsMffcInput( Abc_Obj_t * p, int nLevelMin, Sfm_Tim_t * pTim, Abc_Obj_t * pPivot )
{
    return Abc_NodeIsTravIdCurrent(p) && Sfm_TimNodeIsNonCritical(pTim, pPivot, p);
}
void Sfm_DecMarkMffc( Abc_Obj_t * pPivot, int nLevelMin, int nMffcMax, int fVeryVerbose, Vec_Int_t * vMffc, Vec_Int_t * vInMffc, Sfm_Tim_t * pTim )
{
    Abc_Obj_t * pFanin, * pFanin2, * pFanin3, * pObj; int i, k, n;
    assert( nMffcMax > 0 );
    Vec_IntFill( vMffc, 1, Abc_ObjId(pPivot) );
    if ( pTim != NULL )
    {
        pPivot->iTemp |= SFM_MASK_MFFC;
        pPivot->iTemp |= SFM_MASK_PIVOT;
        // collect MFFC inputs (these are low-delay nodes close to the pivot)
        Vec_IntClear(vInMffc);
        Abc_ObjForEachFanin( pPivot, pFanin, i )
            if ( Sfm_DecNodeIsMffcInput(pFanin, nLevelMin, pTim, pPivot) )
                Vec_IntPushUnique( vInMffc, Abc_ObjId(pFanin) );
        Abc_ObjForEachFanin( pPivot, pFanin, i )
            Abc_ObjForEachFanin( pFanin, pFanin2, k )
                if ( Sfm_DecNodeIsMffcInput(pFanin2, nLevelMin, pTim, pPivot) )
                    Vec_IntPushUnique( vInMffc, Abc_ObjId(pFanin2) );
        Abc_ObjForEachFanin( pPivot, pFanin, i )
            Abc_ObjForEachFanin( pFanin, pFanin2, k )
                Abc_ObjForEachFanin( pFanin2, pFanin3, n )
                    if ( Sfm_DecNodeIsMffcInput(pFanin3, nLevelMin, pTim, pPivot) )
                        Vec_IntPushUnique( vInMffc, Abc_ObjId(pFanin3) );

/*
        printf( "Node %d: (%.2f)  ", pPivot->Id, MIO_NUMINV*Sfm_TimReadObjDelay(pTim, Abc_ObjId(pPivot))  );
        Abc_ObjForEachFanin( pPivot, pFanin, i )
            printf( "%d: %.2f  ", Abc_ObjLevel(pFanin), MIO_NUMINV*Sfm_TimReadObjDelay(pTim, Abc_ObjId(pFanin)) );
        printf( "\n" );

        printf( "Node %d: ", pPivot->Id );
        Abc_NtkForEachObjVec( vInMffc, pPivot->pNtk, pObj, i )
            printf( "%d: %.2f  ", Abc_ObjLevel(pObj), MIO_NUMINV*Sfm_TimReadObjDelay(pTim, Abc_ObjId(pObj)) );
        printf( "\n" );
*/
    }
    else
    {

        // collect MFFC
        Abc_ObjForEachFanin( pPivot, pFanin, i )
            if ( Sfm_DecNodeIsMffc(pFanin, nLevelMin) && Vec_IntSize(vMffc) < nMffcMax )
                Vec_IntPushUnique( vMffc, Abc_ObjId(pFanin) );
        Abc_ObjForEachFanin( pPivot, pFanin, i )
            if ( Sfm_DecNodeIsMffc(pFanin, nLevelMin) && Vec_IntSize(vMffc) < nMffcMax )
                Abc_ObjForEachFanin( pFanin, pFanin2, k )
                    if ( Sfm_DecNodeIsMffc(pFanin2, nLevelMin) && Vec_IntSize(vMffc) < nMffcMax )
                        Vec_IntPushUnique( vMffc, Abc_ObjId(pFanin2) );
        Abc_ObjForEachFanin( pPivot, pFanin, i )
            if ( Sfm_DecNodeIsMffc(pFanin, nLevelMin) && Vec_IntSize(vMffc) < nMffcMax )
                Abc_ObjForEachFanin( pFanin, pFanin2, k )
                    if ( Sfm_DecNodeIsMffc(pFanin2, nLevelMin) && Vec_IntSize(vMffc) < nMffcMax )
                        Abc_ObjForEachFanin( pFanin2, pFanin3, n )
                            if ( Sfm_DecNodeIsMffc(pFanin3, nLevelMin) && Vec_IntSize(vMffc) < nMffcMax )
                                Vec_IntPushUnique( vMffc, Abc_ObjId(pFanin3) );
        // mark MFFC
        assert( Vec_IntSize(vMffc) <= nMffcMax );
        Abc_NtkForEachObjVec( vMffc, pPivot->pNtk, pObj, i )
            pObj->iTemp |= SFM_MASK_MFFC;
        pPivot->iTemp |= SFM_MASK_PIVOT;
        // collect MFFC inputs
        Vec_IntClear(vInMffc);
        Abc_NtkForEachObjVec( vMffc, pPivot->pNtk, pObj, i )
            Abc_ObjForEachFanin( pObj, pFanin, k )
                if ( Abc_NodeIsTravIdCurrent(pFanin) && pFanin->iTemp == SFM_MASK_PI )
                    Vec_IntPushUnique( vInMffc, Abc_ObjId(pFanin) );

//        printf( "Node %d: ", pPivot->Id );
//        Abc_ObjForEachFanin( pPivot, pFanin, i )
//            printf( "%d ", Abc_ObjFanoutNum(pFanin) );
//        printf( "\n" );

//        Abc_NtkForEachObjVec( vInMffc, pPivot->pNtk, pObj, i )
//            printf( "%d ", Abc_ObjFanoutNum(pObj) );
//        printf( "\n" );

    }
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Sfm_DecExtract( Abc_Ntk_t * pNtk, Sfm_Par_t * pPars, Abc_Obj_t * pPivot, Vec_Int_t * vRoots, Vec_Int_t * vGates, Vec_Wec_t * vFanins, Vec_Int_t * vMap, Vec_Int_t * vTfi, Vec_Int_t * vTfo, Vec_Int_t * vMffc, Vec_Int_t * vInMffc, Sfm_Tim_t * pTim )
{
    int fVeryVerbose = 0;//pPars->fVeryVerbose;
    Vec_Int_t * vLevel;
    Abc_Obj_t * pObj, * pFanin;
    int nLevelMax = pPivot->Level + pPars->nTfoLevMax;
    int nLevelMin = pPivot->Level - pPars->nTfiLevMax;
    int i, k, nTfiSize, nDivs = -1;
    assert( Abc_ObjIsNode(pPivot) );
if ( fVeryVerbose )
printf( "\n\nTarget %d\n", Abc_ObjId(pPivot) );
    // collect TFO nodes
    Vec_IntClear( vTfo );
    Abc_NtkIncrementTravId( pNtk );
    Abc_NtkDfsReverseOne_rec( pPivot, vTfo, nLevelMax, pPars->nFanoutMax );
    // count internal fanouts
    Abc_NtkForEachObjVec( vTfo, pNtk, pObj, i )
        Abc_ObjForEachFanin( pObj, pFanin, k )
            pFanin->iTemp++;
    // compute roots
    Vec_IntClear( vRoots );
    Abc_NtkForEachObjVec( vTfo, pNtk, pObj, i )
        if ( pObj->iTemp != Abc_ObjFanoutNum(pObj) )
            Vec_IntPush( vRoots, Abc_ObjId(pObj) );
    assert( Vec_IntSize(vRoots) > 0 );
    // collect TFI and mark nodes
    Vec_IntClear( vTfi );
    Abc_NtkIncrementTravId( pNtk );
    Abc_NtkDfsOne_rec( pPivot, vTfi, nLevelMin, SFM_MASK_PI );
    nTfiSize = Vec_IntSize(vTfi);
    Sfm_ObjFlipNode( pPivot );
    // additinally mark MFFC
    Sfm_DecMarkMffc( pPivot, nLevelMin, pPars->nMffcMax, fVeryVerbose, vMffc, vInMffc, pTim );
    assert( Vec_IntSize(vMffc) <= pPars->nMffcMax );
if ( fVeryVerbose )
printf( "Mffc size = %d. Mffc area = %.2f. InMffc size = %d.\n", Vec_IntSize(vMffc), Sfm_DecMffcArea(pNtk, vMffc)*MIO_NUMINV, Vec_IntSize(vInMffc) );
    // collect TFI(TFO)
    Abc_NtkForEachObjVec( vRoots, pNtk, pObj, i )
        Abc_NtkDfsOne_rec( pObj, vTfi, nLevelMin, SFM_MASK_INPUT );
    // mark input-only nodes pointed to by mixed nodes
    Abc_NtkForEachObjVecStart( vTfi, pNtk, pObj, i, nTfiSize )
        if ( pObj->iTemp != SFM_MASK_INPUT )
            Abc_ObjForEachFanin( pObj, pFanin, k )
                if ( pFanin->iTemp == SFM_MASK_INPUT )
                    pFanin->iTemp = SFM_MASK_FANIN;
    // collect nodes supported only on TFI fanins and not MFFC
if ( fVeryVerbose )
printf( "\nDivs:\n" );
    Vec_IntClear( vMap );
    Vec_IntClear( vGates );
    Abc_NtkForEachObjVec( vTfi, pNtk, pObj, i )
        if ( pObj->iTemp == SFM_MASK_PI )
            Sfm_DecAddNode( pObj, vMap, vGates, Abc_ObjIsCi(pObj) || (Abc_ObjLevel(pObj) < nLevelMin && Abc_ObjFaninNum(pObj) > 0), fVeryVerbose );
    nDivs = Vec_IntSize(vMap);
    // add other nodes that are not in TFO and not in MFFC
if ( fVeryVerbose )
printf( "\nSides:\n" );
    Abc_NtkForEachObjVec( vTfi, pNtk, pObj, i )
        if ( pObj->iTemp == (SFM_MASK_PI | SFM_MASK_INPUT) || pObj->iTemp == SFM_MASK_FANIN )
            Sfm_DecAddNode( pObj, vMap, vGates, pObj->iTemp == SFM_MASK_FANIN, fVeryVerbose );
    // reorder nodes acording to delay
    if ( pTim )
    {
        int nDivsNew, nOldSize = Vec_IntSize(vMap);
        Vec_IntClear( vTfo );
        Vec_IntAppend( vTfo, vMap );
        nDivsNew = Sfm_TimSortArrayByArrival( pTim, vTfo, Abc_ObjId(pPivot) );
        // collect again
        Vec_IntClear( vMap );
        Vec_IntClear( vGates );
        Abc_NtkForEachObjVec( vTfo, pNtk, pObj, i )
            Sfm_DecAddNode( pObj, vMap, vGates, Abc_ObjIsCi(pObj) || (Abc_ObjLevel(pObj) < nLevelMin && Abc_ObjFaninNum(pObj) > 0) || pObj->iTemp == SFM_MASK_FANIN, 0 );
        assert( nOldSize == Vec_IntSize(vMap) );
        // update divisor count
        nDivs = nDivsNew;
    }
    // add the TFO nodes
if ( fVeryVerbose )
printf( "\nTFO:\n" );
    Abc_NtkForEachObjVec( vTfi, pNtk, pObj, i )
        if ( pObj->iTemp >= SFM_MASK_MFFC )
            Sfm_DecAddNode( pObj, vMap, vGates, 0, fVeryVerbose );
    assert( Vec_IntSize(vMap) == Vec_IntSize(vGates) );
if ( fVeryVerbose )
printf( "\n" );
    // create node IDs
    Vec_WecClear( vFanins );
    Abc_NtkForEachObjVec( vMap, pNtk, pObj, i )
    {
        pObj->iTemp = i;
        vLevel = Vec_WecPushLevel( vFanins );
        if ( Vec_IntEntry(vGates, i) >= 0 )
            Abc_ObjForEachFanin( pObj, pFanin, k )
                Vec_IntPush( vLevel, pFanin->iTemp );
    }
    // compute care set
    Sfm_DecMan(pPivot)->uCareSet = Sfm_ObjFindCareSet(pPivot->pNtk, vRoots);

    //printf( "care = %5d : ", Abc_ObjId(pPivot) );
    //Extra_PrintBinary( stdout, (unsigned *)&Sfm_DecMan(pPivot)->uCareSet, 64 );
    //printf( "\n" );

    // remap roots
    Abc_NtkForEachObjVec( vRoots, pNtk, pObj, i )
        Vec_IntWriteEntry( vRoots, i, pObj->iTemp );
    // remap inputs to MFFC
    Abc_NtkForEachObjVec( vInMffc, pNtk, pObj, i )
        Vec_IntWriteEntry( vInMffc, i, pObj->iTemp );

/*
    // check
    Abc_NtkForEachObjVec( vMap, pNtk, pObj, i )
    {
        if ( i == nDivs )
            break;
        Abc_NtkIncrementTravId( pNtk );
        assert( Abc_NtkDfsCheck_rec(pObj, pPivot) );
    }
*/  
    return nDivs;
}
Abc_Obj_t * Sfm_DecInsert( Abc_Ntk_t * pNtk, Abc_Obj_t * pPivot, int Limit, Vec_Int_t * vGates, Vec_Wec_t * vFanins, Vec_Int_t * vMap, Vec_Ptr_t * vGateHandles, int GateBuf, int GateInv, Vec_Wrd_t * vFuncs, Vec_Int_t * vTimeNodes )
{
    Abc_Obj_t * pObjNew = NULL; 
    Vec_Int_t * vLevel;
    int i, k, iObj, Gate;
    if ( vTimeNodes )
        Vec_IntClear( vTimeNodes );
    // assuming that new gates are appended at the end
    assert( Limit < Vec_IntSize(vGates) );
    assert( Limit == Vec_IntSize(vMap) );
    if ( Limit + 1 == Vec_IntSize(vGates) )
    {
        Gate = Vec_IntEntryLast(vGates);
        if ( Gate == GateBuf )
        {
            iObj = Vec_WecEntryEntry( vFanins, Limit, 0 );
            pObjNew = Abc_NtkObj( pNtk, Vec_IntEntry(vMap, iObj) );
            Abc_ObjReplace( pPivot, pObjNew );
            // update level
            pObjNew->Level = 0;
            Abc_NtkUpdateIncLevel_rec( pObjNew );
            if ( vTimeNodes )
                Vec_IntPush( vTimeNodes, Abc_ObjId(pObjNew) );
            return pObjNew;
        }
        else if ( vTimeNodes == NULL && Gate == GateInv )
        {
            // check if fanouts can be updated
            Abc_Obj_t * pFanout;
            Abc_ObjForEachFanout( pPivot, pFanout, i )
                if ( !Abc_ObjIsNode(pFanout) || Sfm_LibFindComplInputGate(vFuncs, Mio_GateReadValue((Mio_Gate_t*)pFanout->pData), Abc_ObjFaninNum(pFanout), Abc_NodeFindFanin(pFanout, pPivot), NULL) == -1 )
                    break;
            // update fanouts
            if ( i == Abc_ObjFanoutNum(pPivot) )
            {
                Abc_ObjForEachFanout( pPivot, pFanout, i )
                {
                    int iFanin = Abc_NodeFindFanin(pFanout, pPivot), iFaninNew = -1;
                    int iGate = Mio_GateReadValue((Mio_Gate_t*)pFanout->pData);
                    int iGateNew = Sfm_LibFindComplInputGate( vFuncs, iGate, Abc_ObjFaninNum(pFanout), iFanin, &iFaninNew );
                    assert( iGateNew >= 0 && iGateNew != iGate && iFaninNew >= 0 );
                    pFanout->pData = Vec_PtrEntry( vGateHandles, iGateNew );
                    //assert( iFanin == iFaninNew );
                    // swap fanins
                    if ( iFanin != iFaninNew )
                    {
                        int * pArray = Vec_IntArray( &pFanout->vFanins );
                        ABC_SWAP( int, pArray[iFanin], pArray[iFaninNew] );
                    }
                }
                iObj = Vec_WecEntryEntry( vFanins, Limit, 0 );
                pObjNew = Abc_NtkObj( pNtk, Vec_IntEntry(vMap, iObj) );
                Abc_ObjReplace( pPivot, pObjNew );
                // update level
                pObjNew->Level = 0;
                Abc_NtkUpdateIncLevel_rec( pObjNew );
                return pObjNew;
            }
        }
    }
    // introduce new gates
    Vec_IntForEachEntryStart( vGates, Gate, i, Limit )
    {
        vLevel = Vec_WecEntry( vFanins, i );
        pObjNew = Abc_NtkCreateNode( pNtk );
        Vec_IntForEachEntry( vLevel, iObj, k )
            Abc_ObjAddFanin( pObjNew, Abc_NtkObj(pNtk, Vec_IntEntry(vMap, iObj)) );
        pObjNew->pData = Vec_PtrEntry( vGateHandles, Gate );
        assert( Abc_ObjFaninNum(pObjNew) == Mio_GateReadPinNum((Mio_Gate_t *)pObjNew->pData) );
        Vec_IntPush( vMap, Abc_ObjId(pObjNew) );
        if ( vTimeNodes )
            Vec_IntPush( vTimeNodes, Abc_ObjId(pObjNew) );
    }
    Abc_ObjReplace( pPivot, pObjNew );
    // update level
    Abc_NtkForEachObjVecStart( vMap, pNtk, pObjNew, i, Limit )
        Abc_NtkUpdateIncLevel_rec( pObjNew );
    return pObjNew;
}
void Sfm_DecPrintStats( Sfm_Dec_t * p )
{
    int i;
    printf( "Node = %d. Try = %d. Change = %d.   Const0 = %d. Const1 = %d. Buf = %d. Inv = %d. Gate = %d. AndOr = %d. Effort = %d.  NoDec = %d.\n",
        p->nTotalNodesBeg, p->nNodesTried, p->nNodesChanged, p->nNodesConst0, p->nNodesConst1, p->nNodesBuf, p->nNodesInv, p->nNodesResyn, p->nNodesAndOr, p->nEfforts, p->nNoDecs );
    printf( "MaxDiv = %d. MaxWin = %d.   AveDiv = %d. AveWin = %d.   Calls = %d. (Sat = %d. Unsat = %d.)  Over = %d.  T/O = %d.\n",
        p->nMaxDivs, p->nMaxWin, (int)(p->nAllDivs/Abc_MaxInt(1, p->nNodesTried)), (int)(p->nAllWin/Abc_MaxInt(1, p->nNodesTried)), p->nSatCalls, p->nSatCallsSat, p->nSatCallsUnsat, p->nSatCallsOver, p->nTimeOuts );

    p->timeTotal = Abc_Clock() - p->timeStart;
    p->timeOther = p->timeTotal - p->timeLib - p->timeWin - p->timeCnf - p->timeSat - p->timeTime;

    ABC_PRTP( "Lib   ", p->timeLib  ,     p->timeTotal );
    ABC_PRTP( "Win   ", p->timeWin  ,     p->timeTotal );
    ABC_PRTP( "Cnf   ", p->timeCnf  ,     p->timeTotal );
    ABC_PRTP( "Sat   ", p->timeSat  ,     p->timeTotal );
    ABC_PRTP( " Sat  ", p->timeSatSat,    p->timeTotal );
    ABC_PRTP( " Unsat", p->timeSatUnsat,  p->timeTotal );
    ABC_PRTP( "Timing", p->timeTime ,     p->timeTotal );
    ABC_PRTP( "Other ", p->timeOther,     p->timeTotal );
    ABC_PRTP( "ALL   ", p->timeTotal,     p->timeTotal );

    printf( "Cone sizes:  " );
    for ( i = 0; i <= SFM_SUPP_MAX; i++ )
        if ( p->nLuckySizes[i] )
            printf( "%d=%d  ", i, p->nLuckySizes[i] );
    printf( "  " );

    printf( "Gate sizes:  " );
    for ( i = 0; i <= SFM_SUPP_MAX; i++ )
        if ( p->nLuckyGates[i] )
            printf( "%d=%d  ", i, p->nLuckyGates[i] );
    printf( "\n" );

    printf( "Reduction:   " );
    printf( "Nodes  %6d out of %6d (%6.2f %%)   ", p->nTotalNodesBeg-p->nTotalNodesEnd, p->nTotalNodesBeg, 100.0*(p->nTotalNodesBeg-p->nTotalNodesEnd)/Abc_MaxInt(1, p->nTotalNodesBeg) );
    printf( "Edges  %6d out of %6d (%6.2f %%)   ", p->nTotalEdgesBeg-p->nTotalEdgesEnd, p->nTotalEdgesBeg, 100.0*(p->nTotalEdgesBeg-p->nTotalEdgesEnd)/Abc_MaxInt(1, p->nTotalEdgesBeg) );
    printf( "\n" );
}
void Abc_NtkCountStats( Sfm_Dec_t * p, int Limit )
{
    int Gate, nGates = Vec_IntSize(&p->vObjGates);
    if ( nGates == Limit )
        return;
    Gate = Vec_IntEntryLast(&p->vObjGates);
    if ( nGates > Limit + 1 )
        p->nNodesResyn++;
    else if ( Gate == p->GateConst0 )
        p->nNodesConst0++;
    else if ( Gate == p->GateConst1 )
        p->nNodesConst1++;
    else if ( Gate == p->GateBuffer )
        p->nNodesBuf++;
    else if ( Gate == p->GateInvert )
        p->nNodesInv++;
    else 
        p->nNodesResyn++;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Abc_Obj_t * Abc_NtkAreaOptOne( Sfm_Dec_t * p, int i )
{
    abctime clk;
    Abc_Ntk_t * pNtk = p->pNtk;
    Sfm_Par_t * pPars = p->pPars;
    Abc_Obj_t * pObj = Abc_NtkObj( p->pNtk, i ); 
    int Limit, RetValue;
    if ( pPars->nMffcMin > 1 && Abc_NodeMffcLabel(pObj) < pPars->nMffcMin )
        return NULL;
    if ( pPars->iNodeOne && i != pPars->iNodeOne )
        return NULL;
    if ( pPars->iNodeOne )
        pPars->fVeryVerbose = (int)(i == pPars->iNodeOne);
    p->nNodesTried++;
clk = Abc_Clock();
    p->nDivs = Sfm_DecExtract( pNtk, pPars, pObj, &p->vObjRoots, &p->vObjGates, &p->vObjFanins, &p->vObjMap, &p->vTemp, &p->vTemp2, &p->vObjMffc, &p->vObjInMffc, NULL );
p->timeWin += Abc_Clock() - clk;
    if ( pPars->nWinSizeMax && pPars->nWinSizeMax < Vec_IntSize(&p->vObjGates) )
        return NULL;
    p->nMffc = Vec_IntSize(&p->vObjMffc);
    p->AreaMffc = Sfm_DecMffcArea(pNtk, &p->vObjMffc);
    p->nMaxDivs = Abc_MaxInt( p->nMaxDivs, p->nDivs );
    p->nAllDivs += p->nDivs;
    p->iTarget = pObj->iTemp;
    Limit = Vec_IntSize( &p->vObjGates );
    p->nMaxWin = Abc_MaxInt( p->nMaxWin, Limit );
    p->nAllWin += Limit;
clk = Abc_Clock();
    RetValue = Sfm_DecPrepareSolver( p );
p->timeCnf += Abc_Clock() - clk;
    if ( !RetValue )
        return NULL;
clk = Abc_Clock();
    if ( pPars->fRrOnly )
        RetValue = Sfm_DecPeformDec( p );
    else
        RetValue = Sfm_DecPeformDec2( p, pObj );
    if ( pPars->fMoreEffort && RetValue < 0 )
    {
        int Var, i;
        Vec_IntForEachEntryReverse( &p->vObjInMffc, Var, i )
        {
            p->iUseThis = Var; 
            if ( pPars->fRrOnly )
                RetValue = Sfm_DecPeformDec( p );
            else
                RetValue = Sfm_DecPeformDec2( p, pObj );
            p->iUseThis = -1;
            if ( RetValue < 0 )
            {
                //printf( "Node %d: Not found among %d.\n", Abc_ObjId(pObj), Vec_IntSize(&p->vObjInMffc) );
            }
            else
            {
                p->nEfforts++;
                if ( p->pPars->fVerbose )
                {
                    //printf( "Node %5d: (%2d out of %2d)  Gate=%s ", Abc_ObjId(pObj), i, Vec_IntSize(&p->vObjInMffc), Mio_GateReadName((Mio_Gate_t*)pObj->pData) );
                    //Dau_DsdPrintFromTruth( p->Copy, p->nSuppVars );
                }
                break;
            }
        }
    }
    if ( p->pPars->fVeryVerbose )
        printf( "\n\n" );
p->timeSat += Abc_Clock() - clk;
    if ( RetValue < 0 )
        return NULL;
    p->nNodesChanged++;
    Abc_NtkCountStats( p, Limit );
    return Sfm_DecInsert( pNtk, pObj, Limit, &p->vObjGates, &p->vObjFanins, &p->vObjMap, &p->vGateHands, p->GateBuffer, p->GateInvert, &p->vGateFuncs, NULL );
}
void Abc_NtkAreaOpt( Sfm_Dec_t * p )
{
    Abc_Obj_t * pObj; 
    int i, nStop = Abc_NtkObjNumMax(p->pNtk);
    Abc_NtkForEachNode( p->pNtk, pObj, i )
    {
        if ( i >= nStop || (p->pPars->nNodesMax && i > p->pPars->nNodesMax) )
            break;
        Abc_NtkAreaOptOne( p, i );
    }
}
void Abc_NtkAreaOpt2( Sfm_Dec_t * p )
{
    Abc_Obj_t * pObj, * pObjNew, * pFanin; 
    int i, k, nStop = Abc_NtkObjNumMax(p->pNtk);
    Vec_Ptr_t * vFront = Vec_PtrAlloc( 1000 );
    Abc_NtkForEachObj( p->pNtk, pObj, i )
        assert( pObj->fMarkB == 0 );
    // start the queue of nodes to be tried
    Abc_NtkForEachCo( p->pNtk, pObj, i )
        if ( Abc_ObjIsNode(Abc_ObjFanin0(pObj)) && !Abc_ObjFanin0(pObj)->fMarkB )
        {
            Abc_ObjFanin0(pObj)->fMarkB = 1;
            Vec_PtrPush( vFront, Abc_ObjFanin0(pObj) );
        }
    // process nodes in this order
    Vec_PtrForEachEntry( Abc_Obj_t *, vFront, pObj, i )
    {
        if ( Abc_ObjIsNone(pObj) )
            continue;
        pObjNew = Abc_NtkAreaOptOne( p, Abc_ObjId(pObj) );
        if ( pObjNew != NULL )
        {
            if ( !Abc_ObjIsNode(pObjNew) || Abc_ObjFaninNum(pObjNew) == 0 || pObjNew->fMarkB )
                continue;
            if ( (int)Abc_ObjId(pObjNew) < nStop )
            {
                pObjNew->fMarkB = 1;
                Vec_PtrPush( vFront, pObjNew );
                continue;
            }
        }
        else
            pObjNew = pObj;
        Abc_ObjForEachFanin( pObjNew, pFanin, k )
            if ( Abc_ObjIsNode(pFanin) && Abc_ObjFaninNum(pObjNew) > 0 && !pFanin->fMarkB )
            {
                pFanin->fMarkB = 1;
                Vec_PtrPush( vFront, pFanin );
            }
    }
    Abc_NtkForEachObj( p->pNtk, pObj, i )
        pObj->fMarkB = 0;
    Vec_PtrFree( vFront );
}

void Abc_NtkDelayOpt( Sfm_Dec_t * p )
{
    Abc_Ntk_t * pNtk = p->pNtk;
    Sfm_Par_t * pPars = p->pPars; int n;
    Abc_NtkCleanMarkABC( pNtk );
    for ( n = 0; pPars->nNodesMax == 0 || n < pPars->nNodesMax; n++ )
    {
        Abc_Obj_t * pObj, * pObjNew; abctime clk;
        int i = 0, Limit, RetValue;
        // collect nodes
        if ( pPars->iNodeOne )
            Vec_IntFill( &p->vCands, 1, pPars->iNodeOne );
        else if ( !Sfm_TimPriorityNodes(p->pTim, &p->vCands, p->pPars->nTimeWin) )
            break;
        // try improving delay for the nodes according to the priority
        Abc_NtkForEachObjVec( &p->vCands, p->pNtk, pObj, i )
        {
            int OldId = Abc_ObjId(pObj);
            int DelayOld = Sfm_TimReadObjDelay(p->pTim, OldId);
            assert( pObj->fMarkA == 0 );

            p->nNodesTried++;
clk = Abc_Clock();
            p->nDivs = Sfm_DecExtract( pNtk, pPars, pObj, &p->vObjRoots, &p->vObjGates, &p->vObjFanins, &p->vObjMap, &p->vTemp, &p->vTemp2, &p->vObjMffc, &p->vObjInMffc, p->pTim );
p->timeWin += Abc_Clock() - clk;
            if ( p->nDivs < 2 || (pPars->nWinSizeMax && pPars->nWinSizeMax < Vec_IntSize(&p->vObjGates)) )
            { 
                pObj->fMarkA = 1;
                continue;
            }
            p->nMffc = Vec_IntSize(&p->vObjMffc);
            p->AreaMffc = Sfm_DecMffcArea(pNtk, &p->vObjMffc);
            p->nMaxDivs = Abc_MaxInt( p->nMaxDivs, p->nDivs );
            p->nAllDivs += p->nDivs;
            p->iTarget = pObj->iTemp;
            Limit = Vec_IntSize( &p->vObjGates );
            p->nMaxWin = Abc_MaxInt( p->nMaxWin, Limit );
            p->nAllWin += Limit;
clk = Abc_Clock();
            RetValue = Sfm_DecPrepareSolver( p );
p->timeCnf += Abc_Clock() - clk;
            if ( !RetValue )
            {
                pObj->fMarkA = 1;
                continue;
            }
clk = Abc_Clock();
            RetValue = Sfm_DecPeformDec3( p, pObj );
            if ( pPars->fMoreEffort && RetValue < 0 )
            {
                int Var, i;
                Vec_IntForEachEntryReverse( &p->vObjInMffc, Var, i )
                {
                    p->iUseThis = Var; 
                    RetValue = Sfm_DecPeformDec3( p, pObj );
                    p->iUseThis = -1;
                    if ( RetValue < 0 )
                    {
                        //printf( "Node %d: Not found among %d.\n", Abc_ObjId(pObj), Vec_IntSize(&p->vObjInMffc) );
                    }
                    else
                    {
                        p->nEfforts++;
                        if ( p->pPars->fVerbose )
                        {
                            //printf( "Node %5d: (%2d out of %2d)  Gate=%s ", Abc_ObjId(pObj), i, Vec_IntSize(&p->vObjInMffc), Mio_GateReadName((Mio_Gate_t*)pObj->pData) );
                            //Dau_DsdPrintFromTruth( p->Copy, p->nSuppVars );
                        }
                        break;
                    }
                }
            }
            if ( p->pPars->fVeryVerbose )
                printf( "\n\n" );
p->timeSat += Abc_Clock() - clk;
            if ( RetValue < 0 )
            {
                pObj->fMarkA = 1;
                continue;
            }
            assert( Vec_IntSize(&p->vObjGates) - Limit > 0 );
            assert( Vec_IntSize(&p->vObjGates) - Limit <= 2 );
            p->nNodesChanged++;
            Abc_NtkCountStats( p, Limit );
            Sfm_DecInsert( pNtk, pObj, Limit, &p->vObjGates, &p->vObjFanins, &p->vObjMap, &p->vGateHands, p->GateBuffer, p->GateInvert, &p->vGateFuncs, &p->vTemp );
clk = Abc_Clock();
            Sfm_TimUpdateTiming( p->pTim, &p->vTemp );
p->timeTime += Abc_Clock() - clk;
            pObjNew = Abc_NtkObj( pNtk, Abc_NtkObjNumMax(pNtk)-1 );
            assert( p->DelayMin == 0 || p->DelayMin == Sfm_TimReadObjDelay(p->pTim, Abc_ObjId(pObjNew)) );
            // report
            if ( pPars->fDelayVerbose )
                printf( "Node %5d :  I =%3d.  Cand = %5d (%6.2f %%)   Old =%8.2f.  New =%8.2f.  Final =%8.2f\n", 
                    OldId, i, Vec_IntSize(&p->vCands), 100.0 * Vec_IntSize(&p->vCands) / Abc_NtkNodeNum(p->pNtk),
                    MIO_NUMINV*DelayOld, MIO_NUMINV*Sfm_TimReadObjDelay(p->pTim, Abc_ObjId(pObjNew)), 
                    MIO_NUMINV*Sfm_TimReadNtkDelay(p->pTim) );
            break;
        }
        if ( pPars->iNodeOne )
            break;
    }
    Abc_NtkCleanMarkABC( pNtk );
}
void Abc_NtkPerformMfs3( Abc_Ntk_t * pNtk, Sfm_Par_t * pPars )
{
    Sfm_Dec_t * p = Sfm_DecStart( pPars, (Mio_Library_t *)pNtk->pManFunc, pNtk );
    if ( pPars->fVerbose )
    {
        printf( "Remapping parameters: " );
        if ( pPars->nTfoLevMax )
            printf( "TFO = %d. ",     pPars->nTfoLevMax );
        if ( pPars->nTfiLevMax )
            printf( "TFI = %d. ",     pPars->nTfiLevMax );
        if ( pPars->nFanoutMax )
            printf( "FanMax = %d. ",  pPars->nFanoutMax );
        if ( pPars->nWinSizeMax )
            printf( "WinMax = %d. ",  pPars->nWinSizeMax );
        if ( pPars->nBTLimit )
            printf( "Confl = %d. ",   pPars->nBTLimit );
        if ( pPars->nMffcMin && pPars->fArea )
            printf( "MffcMin = %d. ", pPars->nMffcMin );
        if ( pPars->nMffcMax && pPars->fArea )
            printf( "MffcMax = %d. ", pPars->nMffcMax );
        if ( pPars->nDecMax )
            printf( "DecMax = %d. ",  pPars->nDecMax );
        if ( pPars->iNodeOne )
            printf( "Pivot = %d. ",   pPars->iNodeOne );
        if ( !pPars->fArea )
            printf( "Win = %d. ",     pPars->nTimeWin );
        if ( !pPars->fArea )
            printf( "Delta = %.2f ps. ", MIO_NUMINV*p->DeltaCrit );
        if ( pPars->fArea )
            printf( "0-cost = %s. ",  pPars->fZeroCost ? "yes" : "no" );
        printf( "Effort = %s. ",      pPars->fMoreEffort ? "yes" : "no" );
        printf( "Sim = %s. ",         pPars->fUseSim ? "yes" : "no" );
        printf( "\n" );
    }
    // preparation steps
    Abc_NtkLevel( pNtk );
    if ( p->pPars->fUseSim )
        Sfm_NtkSimulate( pNtk );
    // record statistics
    if ( pPars->fVerbose ) p->nTotalNodesBeg = Abc_NtkNodeNum(pNtk);
    if ( pPars->fVerbose ) p->nTotalEdgesBeg = Abc_NtkGetTotalFanins(pNtk);
    // perform optimization
    if ( pPars->fArea )
    {
        if ( pPars->fAreaRev )
            Abc_NtkAreaOpt2( p );
        else
            Abc_NtkAreaOpt( p );
    }
    else
        Abc_NtkDelayOpt( p );
    // record statistics
    if ( pPars->fVerbose ) p->nTotalNodesEnd = Abc_NtkNodeNum(pNtk);
    if ( pPars->fVerbose ) p->nTotalEdgesEnd = Abc_NtkGetTotalFanins(pNtk);
    // print stats and quit
    if ( pPars->fVerbose )
    Sfm_DecPrintStats( p );
    if ( pPars->fLibVerbose )
        Sfm_LibPrint( p->pLib );
    Sfm_DecStop( p );
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END

