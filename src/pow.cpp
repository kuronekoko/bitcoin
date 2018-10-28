// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"

unsigned int static LinearWeightedMovingAverage(const CBlockIndex* pindexLast, const Consensus::Params& params);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock,
                                 const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

	// Zawy's LWMA.
	return LwmaGetNextWorkRequired(pindexLast, pblock, params);
}

unsigned int LwmaGetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // Special difficulty rule for testnet:
    // If the new block's timestamp is more than 2 * 10 minutes
    // then allow mining of a min-difficulty block.
    if (params.fPowAllowMinDifficultyBlocks &&
        pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2) {
        return UintToArith256(params.powLimit).GetCompact();
    }
    return LinearWeightedMovingAverage(pindexLast, params);
}

// refer to https://github.com/zawy12/difficulty-algorithms/issues/3#issuecomment-388386175
// LWMA for BTC clones
// Algorithm by zawy, LWMA idea by Tom Harding
// Code by h4x3rotab of BTC Gold, modified/updated by zawy
// https://github.com/zawy12/difficulty-algorithms/issues/3#issuecomment-388386175
//  FTL must be changed to about N*T/20 = 360 for T=120 and N=60 coins.
//  FTL is MAX_FUTURE_BLOCK_TIME in chain.h.
//  FTL in Ignition, Numus, and others can be found in main.h as DRIFT.
//  Some coins took out a variable, and need to change the 2*60*60 here:
//  if (block.GetBlockTime() > nAdjustedTime + 2 * 60 * 60)
unsigned int static LinearWeightedMovingAverage(const CBlockIndex* pindexLast, const Consensus::Params& params) {
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Genesis block
    if (pindexLast == NULL) {
        return nProofOfWorkLimit;
    }

    if (params.fPowNoRetargeting) {
        return pindexLast->nBits;
    }

    if (pindexLast->nHeight <= 8) {
    		return nProofOfWorkLimit;
    }

    const int FTL = MAX_FUTURE_BLOCK_TIME;
    const int T = params.nPowTargetSpacing;
    const int height = pindexLast->nHeight;
    const int N = 8;
    const int k = N*(N+1)*T/2;
    assert(height > N);

    arith_uint256 sum_target;
    int t = 0, j = 0, solvetime;

    // Loop through N most recent blocks.
    for (int i = height - N+1; i <= height; i++) {
        const CBlockIndex* block = pindexLast->GetAncestor(i);
        const CBlockIndex* block_Prev = block->GetAncestor(i - 1);
        solvetime = block->GetBlockTime() - block_Prev->GetBlockTime();
        solvetime = std::max(-FTL, std::min(solvetime, 6*T));
        j++;
        t += solvetime * j;  // Weighted solvetime sum.
        arith_uint256 target;
        target.SetCompact(block->nBits);
        sum_target += target / (k * N);
    }

    // Keep t reasonable to >= 1/10 of expected t.
    if (t < k/10 ) {
        t = k/10;
    }
    arith_uint256 next_target = t * sum_target;

    return next_target.GetCompact();
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
//    const arith_uint256 bnPowLimit = UintToArith256(params.powLimitLegacy);
    const arith_uint256 bnPowLimit = UintToArith256(uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}


bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit)) {
        return false;
    }

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

