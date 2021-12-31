/*
** Modified from the following project
** 1. https://github.com/emp-toolkit/
*/

#ifndef KUNLUN_AES_HPP_
#define KUNLUN_AES_HPP_

#include "block.hpp"

namespace AES{

struct Key{ 
    block roundkey[11]; 
    size_t ROUND_NUM; 
};

#define EXPAND_ASSIST(v1, v2, v3, v4, SHUFF_CONST, AES_CONST)                               \
    v2 = _mm_aeskeygenassist_si128(v4, AES_CONST);                                          \
    v3 = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(v3), _mm_castsi128_ps(v1), 16));  \
    v1 = _mm_xor_si128(v1,v3);                                                              \
    v3 = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(v3), _mm_castsi128_ps(v1), 140)); \
    v1 = _mm_xor_si128(v1,v3);                                                              \
    v2 = _mm_shuffle_epi32(v2, SHUFF_CONST);                                                \
    v1 = _mm_xor_si128(v1,v2)


__attribute__((target("aes,sse2")))
inline void SetEncKey(const block &userkey, Key &key) {
    block x0, x1, x2;
    key.roundkey[0] = x0 = userkey;
    x2 = _mm_setzero_si128();
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 1);
    key.roundkey[1] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 2);
    key.roundkey[2] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 4);
    key.roundkey[3] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 8);
    key.roundkey[4] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 16);
    key.roundkey[5] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 32);
    key.roundkey[6] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 64);
    key.roundkey[7] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 128);
    key.roundkey[8] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 27);
    key.roundkey[9] = x0;
    EXPAND_ASSIST(x0, x1, x2, x0, 255, 54);
    key.roundkey[10] = x0;
    key.ROUND_NUM = 10;
}

__attribute__((target("aes,sse2")))
inline void SetDecKeyFast(Key &dkey, const Key &ekey) {

    dkey.ROUND_NUM = ekey.ROUND_NUM;
    int j = 0;
    int i = dkey.ROUND_NUM; 

    dkey.roundkey[i--] = ekey.roundkey[j++];
    while (i >= 1)
        dkey.roundkey[i--] = _mm_aesimc_si128(ekey.roundkey[j++]);
    dkey.roundkey[i] = ekey.roundkey[j];
}

__attribute__((target("aes,sse2")))
inline void SetDecKey(block userkey, Key &key) {
    Key temp_key;
    SetEncKey(userkey, temp_key);
    SetDecKeyFast(key, temp_key);
}

__attribute__((target("aes,sse2")))
inline void ECBEnc(const Key &key, block* data, size_t BLOCK_LEN) 
{
    for (auto i = 0; i < BLOCK_LEN; i++)
        data[i] = _mm_xor_si128(data[i], key.roundkey[0]);
    for (auto j = 1; j < key.ROUND_NUM; j++)
        for (auto i = 0; i < BLOCK_LEN; i++)
            data[i] = _mm_aesenc_si128(data[i], key.roundkey[j]);
    for (auto i = 0; i < BLOCK_LEN; i++)
        data[i] = _mm_aesenclast_si128(data[i], key.roundkey[key.ROUND_NUM]);
}

__attribute__((target("aes,sse2")))
inline void ECBDec(const Key &key, block* data, size_t BLOCK_LEN) 
{
    size_t i, j; 
    for (i = 0; i < BLOCK_LEN; i++)
        data[i] = _mm_xor_si128(data[i], key.roundkey[0]);
    for (j = 1; j < key.ROUND_NUM; j++)
        for (i = 0; i < BLOCK_LEN; i++)
            data[i] = _mm_aesdec_si128(data[i], key.roundkey[j]);
    for (i = 0; i < BLOCK_LEN; i++)
        data[i] = _mm_aesdeclast_si128(data[i], key.roundkey[j]);
}
}
#endif

// #ifdef __GNUC__
//   #ifndef __clang__
//     #pragma GCC push_options
//     #pragma GCC optimize ("unroll-loops")
//   #endif
// #endif


// #ifdef __GNUC_
//   #ifndef __clang___
//     #pragma GCC pop_options
//   #endif
// #endif
