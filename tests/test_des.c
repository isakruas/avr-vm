/*
 * Copyright 2025-present Isak Ruas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "test_common.h"

/*
 * Canonical FIPS-46 DES test vector:
 *   key        = 0x133457799BBCDFF1
 *   plaintext  = 0x0123456789ABCDEF
 *   ciphertext = 0x85E813540F0AB405
 *
 * The block lives in R0..R7 and the key in R8..R15, LSB of each value in the
 * lowest-numbered register. Running rounds 0..15 (DES = 0x940B | (K << 4))
 * performs a full encryption (H = 0) or decryption (H = 1).
 */

static const uint8_t PT[8] = {0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01};
static const uint8_t KEY[8] = {0xF1, 0xDF, 0xBC, 0x9B, 0x79, 0x57, 0x34, 0x13};
static const uint8_t CT[8] = {0x05, 0xB4, 0x0A, 0x0F, 0x54, 0x13, 0xE8, 0x85};

static void load_block(avr_t *c, const uint8_t *block) {
    for (int i = 0; i < 8; i++) c->R[i] = block[i];
    for (int i = 0; i < 8; i++) c->R[8 + i] = KEY[i];
}

static void run_16_rounds(avr_t *c) {
    for (int k = 0; k < 16; k++) put_op(c, (uint32_t)(k * 2), 0x940B | (k << 4));
    run_n(c, 16);
}

static void test_des_encrypt(avr_t *c) {
    load_block(c, PT);                 /* H = 0 after reset -> encrypt */
    run_16_rounds(c);
    for (int i = 0; i < 8; i++) CHECK_EQ_U(c->R[i], CT[i]);
}

static void test_des_decrypt(avr_t *c) {
    load_block(c, CT);
    c->sreg |= (uint8_t)(1u << F_H);   /* H = 1 -> decrypt */
    run_16_rounds(c);
    for (int i = 0; i < 8; i++) CHECK_EQ_U(c->R[i], PT[i]);
}

static void test_des_round_advances_pc(avr_t *c) {
    put_op(c, 0, 0x940B);              /* DES round 0 */
    avr_step(c);
    CHECK_EQ_U(c->pc, 2);
    CHECK_EQ_U(c->cycles, 1);
    CHECK(c->running);
}

int main(void) {
    avr_t cpu;
    avr_init(&cpu);

    RUN_TEST(test_des_encrypt, &cpu);
    RUN_TEST(test_des_decrypt, &cpu);
    RUN_TEST(test_des_round_advances_pc, &cpu);

    avr_free(&cpu);
    TEST_SUMMARY("des");
}
