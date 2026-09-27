#include "vexops.h"
#include "softfma.h"
#include <immintrin.h>
#include <x86intrin.h>
#include <stdint.h>
#include <string.h>
#include "oracle_hw.h"

#define LA  _mm256_loadu_si256((const __m256i*)a->b)
#define LB  _mm256_loadu_si256((const __m256i*)b->b)
#define XB  _mm_loadu_si128((const __m128i*)b->b)
#define ST(R) _mm256_storeu_si256((__m256i*)out->b,(R))

/* ground truth for non-immediate ops. operand slots match vec_exec(). */
int oracle_hw_simple(vex_op op, const ymm256*a,const ymm256*b,const ymm256*c,ymm256*out,uint64_t*gpr){
    __m256i r;
    switch(op){
    case VPADDB:r=_mm256_add_epi8(LA,LB);break; case VPADDW:r=_mm256_add_epi16(LA,LB);break;
    case VPADDD:r=_mm256_add_epi32(LA,LB);break; case VPADDQ:r=_mm256_add_epi64(LA,LB);break;
    case VPSUBB:r=_mm256_sub_epi8(LA,LB);break; case VPSUBW:r=_mm256_sub_epi16(LA,LB);break;
    case VPSUBD:r=_mm256_sub_epi32(LA,LB);break; case VPSUBQ:r=_mm256_sub_epi64(LA,LB);break;
    case VPADDSB:r=_mm256_adds_epi8(LA,LB);break; case VPADDSW:r=_mm256_adds_epi16(LA,LB);break;
    case VPADDUSB:r=_mm256_adds_epu8(LA,LB);break; case VPADDUSW:r=_mm256_adds_epu16(LA,LB);break;
    case VPSUBSB:r=_mm256_subs_epi8(LA,LB);break; case VPSUBSW:r=_mm256_subs_epi16(LA,LB);break;
    case VPSUBUSB:r=_mm256_subs_epu8(LA,LB);break; case VPSUBUSW:r=_mm256_subs_epu16(LA,LB);break;
    case VPAND:r=_mm256_and_si256(LA,LB);break; case VPANDN:r=_mm256_andnot_si256(LA,LB);break;
    case VPOR:r=_mm256_or_si256(LA,LB);break; case VPXOR:r=_mm256_xor_si256(LA,LB);break;
    case VPCMPEQB:r=_mm256_cmpeq_epi8(LA,LB);break; case VPCMPEQW:r=_mm256_cmpeq_epi16(LA,LB);break;
    case VPCMPEQD:r=_mm256_cmpeq_epi32(LA,LB);break; case VPCMPEQQ:r=_mm256_cmpeq_epi64(LA,LB);break;
    case VPCMPGTB:r=_mm256_cmpgt_epi8(LA,LB);break; case VPCMPGTW:r=_mm256_cmpgt_epi16(LA,LB);break;
    case VPCMPGTD:r=_mm256_cmpgt_epi32(LA,LB);break; case VPCMPGTQ:r=_mm256_cmpgt_epi64(LA,LB);break;
    case VPMINUB:r=_mm256_min_epu8(LA,LB);break; case VPMINUW:r=_mm256_min_epu16(LA,LB);break;
    case VPMINUD:r=_mm256_min_epu32(LA,LB);break; case VPMINSB:r=_mm256_min_epi8(LA,LB);break;
    case VPMINSW:r=_mm256_min_epi16(LA,LB);break; case VPMINSD:r=_mm256_min_epi32(LA,LB);break;
    case VPMAXUB:r=_mm256_max_epu8(LA,LB);break; case VPMAXUW:r=_mm256_max_epu16(LA,LB);break;
    case VPMAXUD:r=_mm256_max_epu32(LA,LB);break; case VPMAXSB:r=_mm256_max_epi8(LA,LB);break;
    case VPMAXSW:r=_mm256_max_epi16(LA,LB);break; case VPMAXSD:r=_mm256_max_epi32(LA,LB);break;
    case VPMULLW:r=_mm256_mullo_epi16(LA,LB);break; case VPMULLD:r=_mm256_mullo_epi32(LA,LB);break;
    case VPMULHW:r=_mm256_mulhi_epi16(LA,LB);break; case VPMULHUW:r=_mm256_mulhi_epu16(LA,LB);break;
    case VPMULHRSW:r=_mm256_mulhrs_epi16(LA,LB);break; case VPMULDQ:r=_mm256_mul_epi32(LA,LB);break;
    case VPMULUDQ:r=_mm256_mul_epu32(LA,LB);break; case VPMADDWD:r=_mm256_madd_epi16(LA,LB);break;
    case VPMADDUBSW:r=_mm256_maddubs_epi16(LA,LB);break;
    case VPAVGB:r=_mm256_avg_epu8(LA,LB);break; case VPAVGW:r=_mm256_avg_epu16(LA,LB);break;
    case VPSADBW:r=_mm256_sad_epu8(LA,LB);break;
    case VPABSB:r=_mm256_abs_epi8(LA);break; case VPABSW:r=_mm256_abs_epi16(LA);break;
    case VPABSD:r=_mm256_abs_epi32(LA);break;
    case VPSIGNB:r=_mm256_sign_epi8(LA,LB);break; case VPSIGNW:r=_mm256_sign_epi16(LA,LB);break;
    case VPSIGND:r=_mm256_sign_epi32(LA,LB);break; case VPHADDD:r=_mm256_hadd_epi32(LA,LB);break;
    case VPSLLW:r=_mm256_sll_epi16(LA,XB);break; case VPSLLD:r=_mm256_sll_epi32(LA,XB);break;
    case VPSLLQ:r=_mm256_sll_epi64(LA,XB);break; case VPSRLW:r=_mm256_srl_epi16(LA,XB);break;
    case VPSRLD:r=_mm256_srl_epi32(LA,XB);break; case VPSRLQ:r=_mm256_srl_epi64(LA,XB);break;
    case VPSRAW:r=_mm256_sra_epi16(LA,XB);break; case VPSRAD:r=_mm256_sra_epi32(LA,XB);break;
    case VPSLLVD:r=_mm256_sllv_epi32(LA,LB);break; case VPSLLVQ:r=_mm256_sllv_epi64(LA,LB);break;
    case VPSRLVD:r=_mm256_srlv_epi32(LA,LB);break; case VPSRLVQ:r=_mm256_srlv_epi64(LA,LB);break;
    case VPSRAVD:r=_mm256_srav_epi32(LA,LB);break;
    case VPSHUFB:r=_mm256_shuffle_epi8(LA,LB);break;
    case VPACKSSWB:r=_mm256_packs_epi16(LA,LB);break; case VPACKSSDW:r=_mm256_packs_epi32(LA,LB);break;
    case VPACKUSWB:r=_mm256_packus_epi16(LA,LB);break; case VPACKUSDW:r=_mm256_packus_epi32(LA,LB);break;
    case VPUNPCKLBW:r=_mm256_unpacklo_epi8(LA,LB);break; case VPUNPCKHBW:r=_mm256_unpackhi_epi8(LA,LB);break;
    case VPUNPCKLWD:r=_mm256_unpacklo_epi16(LA,LB);break; case VPUNPCKHWD:r=_mm256_unpackhi_epi16(LA,LB);break;
    case VPUNPCKLDQ:r=_mm256_unpacklo_epi32(LA,LB);break; case VPUNPCKHDQ:r=_mm256_unpackhi_epi32(LA,LB);break;
    case VPUNPCKLQDQ:r=_mm256_unpacklo_epi64(LA,LB);break; case VPUNPCKHQDQ:r=_mm256_unpackhi_epi64(LA,LB);break;
    case VPBLENDVB:r=_mm256_blendv_epi8(LA,LB,_mm256_loadu_si256((const __m256i*)c->b));break;
    case VPBROADCASTB:r=_mm256_broadcastb_epi8(XB);break; case VPBROADCASTW:r=_mm256_broadcastw_epi16(XB);break;
    case VPBROADCASTD:r=_mm256_broadcastd_epi32(XB);break; case VPBROADCASTQ:r=_mm256_broadcastq_epi64(XB);break;
    case VBROADCASTI128:r=_mm256_broadcastsi128_si256(XB);break;
    case VPMOVZXBW:r=_mm256_cvtepu8_epi16(XB);break; case VPMOVZXBD:r=_mm256_cvtepu8_epi32(XB);break;
    case VPMOVZXBQ:r=_mm256_cvtepu8_epi64(XB);break; case VPMOVZXWD:r=_mm256_cvtepu16_epi32(XB);break;
    case VPMOVZXWQ:r=_mm256_cvtepu16_epi64(XB);break; case VPMOVZXDQ:r=_mm256_cvtepu32_epi64(XB);break;
    case VPMOVSXBW:r=_mm256_cvtepi8_epi16(XB);break; case VPMOVSXBD:r=_mm256_cvtepi8_epi32(XB);break;
    case VPMOVSXBQ:r=_mm256_cvtepi8_epi64(XB);break; case VPMOVSXWD:r=_mm256_cvtepi16_epi32(XB);break;
    case VPMOVSXWQ:r=_mm256_cvtepi16_epi64(XB);break; case VPMOVSXDQ:r=_mm256_cvtepi32_epi64(XB);break;
    case VPERMD:r=_mm256_permutevar8x32_epi32(LB,LA);break;
    case VPERMPS:r=(__m256i)_mm256_permutevar8x32_ps((__m256)LB,LA);break;
    case VCVTPH2PS:r=(__m256i)_mm256_cvtph_ps(XB);break;
    case VPMOVMSKB:*gpr=(uint32_t)_mm256_movemask_epi8(LB);return 1;
    default:return 0;
    }
    ST(r); return 1;
}

#define IMM_SWITCH(FN) switch (imm) { \
    case 0x00: R = FN(0x00); break; case 0x1B: R = FN(0x1B); break; case 0x4E: R = FN(0x4E); break; \
    case 0xD8: R = FN(0xD8); break; case 0x39: R = FN(0x39); break; case 0xAA: R = FN(0xAA); break; \
    case 0x3C: R = FN(0x3C); break; case 0xA5: R = FN(0xA5); break; case 0xFF: R = FN(0xFF); break; \
    case 1: R = FN(1); break;   case 3: R = FN(3); break;   case 7: R = FN(7); break; \
    case 15: R = FN(15); break; case 16: R = FN(16); break; case 31: R = FN(31); break; \
    default: R = FN(0); break; }

void oracle_hw_imm(int idx, uint8_t imm, const ymm256 *a, const ymm256 *b, ymm256 *out) {
    __m256i A = _mm256_loadu_si256((const __m256i *)a->b);
    __m256i B = _mm256_loadu_si256((const __m256i *)b->b);
    __m256i R = _mm256_setzero_si256();
    switch (idx) {
#define F(I) _mm256_shuffle_epi32(B, I)
    case 0: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_shufflelo_epi16(B, I)
    case 1: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_shufflehi_epi16(B, I)
    case 2: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_slli_si256(A, I)
    case 3: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_srli_si256(A, I)
    case 4: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_blend_epi16(A, B, I)
    case 5: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_blend_epi32(A, B, I)
    case 6: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_alignr_epi8(A, B, I)
    case 7: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_permute2x128_si256(A, B, I)
    case 8: IMM_SWITCH(F) break;
#undef F
#define F(I) _mm256_permute4x64_epi64(B, I)
    case 9: IMM_SWITCH(F) break;
#undef F
#define F(I) (__m256i)_mm256_permute4x64_pd((__m256d)B, I)
    case 10: IMM_SWITCH(F) break;
#undef F
    case 11: {
        __m128i x = (imm & 1) ? _mm256_extracti128_si256(B, 1) : _mm256_extracti128_si256(B, 0);
        memset(out->b, 0, 32);
        _mm_storeu_si128((__m128i *)out->b, x);
        return;
    }
    case 12: {
        __m128i xb = _mm_loadu_si128((const __m128i *)b->b);
        R = (imm & 1) ? _mm256_inserti128_si256(A, xb, 1) : _mm256_inserti128_si256(A, xb, 0);
        break;
    }
    }
    _mm256_storeu_si256((__m256i *)out->b, R);
}

double oracle_hw_fma_d(int variant, double m1, double m2, double ad) {
    __m128d x = _mm_set_sd(m1), y = _mm_set_sd(m2), z = _mm_set_sd(ad), r;
    switch (variant) {
    case 0: r = _mm_fmadd_sd(x, y, z); break;  case 1: r = _mm_fmsub_sd(x, y, z); break;
    case 2: r = _mm_fnmadd_sd(x, y, z); break; default: r = _mm_fnmsub_sd(x, y, z); break;
    }
    return _mm_cvtsd_f64(r);
}

float oracle_hw_fma_f(int variant, float m1, float m2, float ad) {
    __m128 x = _mm_set_ss(m1), y = _mm_set_ss(m2), z = _mm_set_ss(ad), r;
    switch (variant) {
    case 0: r = _mm_fmadd_ss(x, y, z); break;  case 1: r = _mm_fmsub_ss(x, y, z); break;
    case 2: r = _mm_fnmadd_ss(x, y, z); break; default: r = _mm_fnmsub_ss(x, y, z); break;
    }
    return _mm_cvtss_f32(r);
}
