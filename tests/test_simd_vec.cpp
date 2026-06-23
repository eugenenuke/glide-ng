#include <catch2/catch_test_macros.hpp>
#include "backends/software/SimdVec.h"
#include <vector>
#include <iostream>
#include <algorithm>

TEST_CASE("SIMD Float Vector (Simd8f) Operations", "[SIMD][Simd8f]") {
    // 1. Construction and element access
    Simd8f f1(1.5f);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(f1[i] == 1.5f);
    }

    Simd8f f2(0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(f2[i] == static_cast<float>(i));
    }

    // 2. Arithmetic
    Simd8f sum = f1 + f2;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(sum[i] == 1.5f + static_cast<float>(i));
    }

    Simd8f diff = f2 - f1;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(diff[i] == static_cast<float>(i) - 1.5f);
    }

    Simd8f prod = f1 * f2;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(prod[i] == 1.5f * static_cast<float>(i));
    }

    Simd8f div = f2 / f1;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(div[i] == static_cast<float>(i) / 1.5f);
    }

    // 3. Min/Max
    Simd8f min_val = Min(f2, Simd8f(3.0f));
    for (int i = 0; i < 8; ++i) {
        REQUIRE(min_val[i] == std::min(static_cast<float>(i), 3.0f));
    }

    Simd8f max_val = Max(f2, Simd8f(3.0f));
    for (int i = 0; i < 8; ++i) {
        REQUIRE(max_val[i] == std::max(static_cast<float>(i), 3.0f));
    }
}

TEST_CASE("SIMD Integer Vector (Simd8i) Operations", "[SIMD][Simd8i]") {
    // 1. Construction and element access
    Simd8i i1(42);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(i1[i] == 42);
    }

    Simd8i i2(0, 10, 20, 30, 40, 50, 60, 70);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(i2[i] == i * 10);
    }

    // 2. Arithmetic
    Simd8i sum = i1 + i2;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(sum[i] == 42 + i * 10);
    }

    Simd8i diff = i2 - i1;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(diff[i] == i * 10 - 42);
    }

    Simd8i prod = i2 * Simd8i(2);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(prod[i] == i * 20);
    }

    // 3. Bitwise
    Simd8i val(0x0F0F0F0F);
    Simd8i mask(0x00FF00FF);
    Simd8i bit_and = val & mask;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(bit_and[i] == 0x000F000F);
    }

    Simd8i bit_or = val | mask;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(bit_or[i] == 0x0FFF0FFF);
    }

    Simd8i bit_xor = val ^ mask;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(bit_xor[i] == (0x0F0F0F0F ^ 0x00FF00FF));
    }

    Simd8i bit_not = ~val;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(bit_not[i] == static_cast<int32_t>(~0x0F0F0F0F));
    }

    // 4. Shifts
    Simd8i s1(1);
    Simd8i shl = s1 << 4;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(shl[i] == 16);
    }

    Simd8i s2(-32); // 0xFFFFFFE0
    Simd8i sar = s2 >> 2; // Arithmetic shift (sign extension)
    for (int i = 0; i < 8; ++i) {
        REQUIRE(sar[i] == -8);
    }

    Simd8i s3(0xF0000000);
    Simd8i shr = s3.Srl(4); // Logical shift right (zero-fill)
    for (int i = 0; i < 8; ++i) {
        REQUIRE(static_cast<uint32_t>(shr[i]) == 0x0F000000);
    }
}

TEST_CASE("SIMD Mask & Selection (SimdMask) Operations", "[SIMD][SimdMask]") {
    Simd8f a(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f);
    Simd8f b(5.0f);

    // Comparisons
    SimdMask lt = a < b; // Lanes 0,1,2,3 should be true (1.0, 2.0, 3.0, 4.0 < 5.0)
    REQUIRE(lt.AnyTrue() == true);
    REQUIRE(lt.AllZero() == false);
    REQUIRE(lt.AllTrue() == false);

    REQUIRE(lt[0] != 0.0f); // True lane has non-zero sign bit or all bits set
    REQUIRE(lt[4] == 0.0f); // False lane is zero

    SimdMask eq = a == Simd8f(3.0f); // Lane 2 is true
    REQUIRE(eq.AnyTrue() == true);
    REQUIRE(eq.AllTrue() == false);
    REQUIRE(eq[2] != 0.0f);
    REQUIRE(eq[1] == 0.0f);

    // Logical on masks
    SimdMask combined = lt & !eq; // Lanes 0, 1, 3 are true
    REQUIRE(combined[0] != 0.0f);
    REQUIRE(combined[1] != 0.0f);
    REQUIRE(combined[2] == 0.0f); // Excluded by !eq
    REQUIRE(combined[3] != 0.0f);
    REQUIRE(combined[4] == 0.0f);

    // Branchless Selection
    Simd8f select_f = Select(lt, a, Simd8f(0.0f));
    for (int i = 0; i < 8; ++i) {
        if (i < 4) {
            REQUIRE(select_f[i] == static_cast<float>(i + 1));
        } else {
            REQUIRE(select_f[i] == 0.0f);
        }
    }

    Simd8i ia(10, 20, 30, 40, 50, 60, 70, 80);
    Simd8i ib(0);
    SimdMask ilt = Simd8f::Convert(ia) < Simd8f(45.0f); // Lanes 0,1,2,3 are true
    Simd8i select_i = Select(ilt, ia, ib);
    for (int i = 0; i < 8; ++i) {
        if (i < 4) {
            REQUIRE(select_i[i] == (i + 1) * 10);
        } else {
            REQUIRE(select_i[i] == 0);
        }
    }
}

TEST_CASE("SIMD 2x4 Pixel Block Memory Operations (Load2x4 / Store2x4)", "[SIMD][LoadStore2x4]") {
    SECTION("Floating-point depth buffers (Simd8f)") {
        alignas(32) std::vector<float> buffer(20, 0.0f);
        // Row 0: elements 3, 4, 5, 6
        buffer[3] = 10.0f; buffer[4] = 11.0f; buffer[5] = 12.0f; buffer[6] = 13.0f;
        // Row 1: elements 13, 14, 15, 16
        buffer[13] = 20.0f; buffer[14] = 21.0f; buffer[15] = 22.0f; buffer[16] = 23.0f;

        // Load 2x4 block
        Simd8f v = Simd8f::Load2x4(&buffer[3], 10);

        // Verify elements inside the register
        REQUIRE(v[0] == 10.0f);
        REQUIRE(v[1] == 11.0f);
        REQUIRE(v[2] == 12.0f);
        REQUIRE(v[3] == 13.0f);
        REQUIRE(v[4] == 20.0f);
        REQUIRE(v[5] == 21.0f);
        REQUIRE(v[6] == 22.0f);
        REQUIRE(v[7] == 23.0f);

        // Modify register
        Simd8f modified = v + Simd8f(100.0f);

        // Store back to a different buffer with a different stride (e.g. 8)
        alignas(32) std::vector<float> out_buffer(16, 0.0f);
        // Target Row 0: starts at 2 (stride 8)
        // Target Row 1: starts at 10
        Simd8f::Store2x4(&out_buffer[2], 8, modified);

        // Verify output buffer
        // Row 0: elements 2,3,4,5
        REQUIRE(out_buffer[2] == 110.0f);
        REQUIRE(out_buffer[3] == 111.0f);
        REQUIRE(out_buffer[4] == 112.0f);
        REQUIRE(out_buffer[5] == 113.0f);
        // Surrounding memory in Row 0 should remain untouched
        REQUIRE(out_buffer[0] == 0.0f);
        REQUIRE(out_buffer[1] == 0.0f);
        REQUIRE(out_buffer[6] == 0.0f);
        REQUIRE(out_buffer[7] == 0.0f);

        // Row 1: elements 10,11,12,13
        REQUIRE(out_buffer[10] == 120.0f);
        REQUIRE(out_buffer[11] == 121.0f);
        REQUIRE(out_buffer[12] == 122.0f);
        REQUIRE(out_buffer[13] == 123.0f);
        // Surrounding memory in Row 1 should remain untouched
        REQUIRE(out_buffer[8] == 0.0f);
        REQUIRE(out_buffer[9] == 0.0f);
        REQUIRE(out_buffer[14] == 0.0f);
        REQUIRE(out_buffer[15] == 0.0f);
    }

    SECTION("32-bit integer color buffers (Simd8i)") {
        alignas(32) std::vector<uint32_t> buffer(20, 0);
        // Row 0: elements 3, 4, 5, 6
        buffer[3] = 0xFF000010; buffer[4] = 0xFF000011; buffer[5] = 0xFF000012; buffer[6] = 0xFF000013;
        // Row 1: elements 13, 14, 15, 16
        buffer[13] = 0xFF000020; buffer[14] = 0xFF000021; buffer[15] = 0xFF000022; buffer[16] = 0xFF000023;

        // Load 2x4 block
        Simd8i v = Simd8i::Load2x4(&buffer[3], 10);

        // Verify elements inside the register
        REQUIRE(v[0] == static_cast<int32_t>(0xFF000010));
        REQUIRE(v[1] == static_cast<int32_t>(0xFF000011));
        REQUIRE(v[2] == static_cast<int32_t>(0xFF000012));
        REQUIRE(v[3] == static_cast<int32_t>(0xFF000013));
        REQUIRE(v[4] == static_cast<int32_t>(0xFF000020));
        REQUIRE(v[5] == static_cast<int32_t>(0xFF000021));
        REQUIRE(v[6] == static_cast<int32_t>(0xFF000022));
        REQUIRE(v[7] == static_cast<int32_t>(0xFF000023));

        // Modify register
        Simd8i modified = v & Simd8i(0x0000FFFF);

        // Store back to a different buffer with a different stride (e.g. 8)
        alignas(32) std::vector<uint32_t> out_buffer(16, 0);
        // Target Row 0: starts at 2 (stride 8)
        // Target Row 1: starts at 10
        Simd8i::Store2x4(&out_buffer[2], 8, modified);

        // Verify output buffer
        // Row 0: elements 2,3,4,5
        REQUIRE(out_buffer[2] == 0x00000010);
        REQUIRE(out_buffer[3] == 0x00000011);
        REQUIRE(out_buffer[4] == 0x00000012);
        REQUIRE(out_buffer[5] == 0x00000013);
        // Surrounding memory in Row 0 should remain untouched
        REQUIRE(out_buffer[0] == 0);
        REQUIRE(out_buffer[1] == 0);
        REQUIRE(out_buffer[6] == 0);
        REQUIRE(out_buffer[7] == 0);

        // Row 1: elements 10,11,12,13
        REQUIRE(out_buffer[10] == 0x00000020);
        REQUIRE(out_buffer[11] == 0x00000021);
        REQUIRE(out_buffer[12] == 0x00000022);
        REQUIRE(out_buffer[13] == 0x00000023);
        // Surrounding memory in Row 1 should remain untouched
        REQUIRE(out_buffer[8] == 0);
        REQUIRE(out_buffer[9] == 0);
        REQUIRE(out_buffer[14] == 0);
        REQUIRE(out_buffer[15] == 0);
    }
}

TEST_CASE("SIMD Precomputed Dither Vector Tables", "[SIMD][DitherTable]") {
    // 1. Verify 4x4 dither vector loader for y mod 4 = 0..3
    for (int y = 0; y < 10; ++y) {
        Simd8i dither = GetDitherVector4x4(y);
        int modY = y % 4;

        if (modY == 0) {
            REQUIRE(dither[0] == 0); REQUIRE(dither[1] == 8); REQUIRE(dither[2] == 2); REQUIRE(dither[3] == 10);
            REQUIRE(dither[4] == 12); REQUIRE(dither[5] == 4); REQUIRE(dither[6] == 14); REQUIRE(dither[7] == 6);
        } else if (modY == 1) {
            REQUIRE(dither[0] == 12); REQUIRE(dither[1] == 4); REQUIRE(dither[2] == 14); REQUIRE(dither[3] == 6);
            REQUIRE(dither[4] == 3); REQUIRE(dither[5] == 11); REQUIRE(dither[6] == 1); REQUIRE(dither[7] == 9);
        } else if (modY == 2) {
            REQUIRE(dither[0] == 3); REQUIRE(dither[1] == 11); REQUIRE(dither[2] == 1); REQUIRE(dither[3] == 9);
            REQUIRE(dither[4] == 15); REQUIRE(dither[5] == 7); REQUIRE(dither[6] == 13); REQUIRE(dither[7] == 5);
        } else if (modY == 3) {
            REQUIRE(dither[0] == 15); REQUIRE(dither[1] == 7); REQUIRE(dither[2] == 13); REQUIRE(dither[3] == 5);
            REQUIRE(dither[4] == 0); REQUIRE(dither[5] == 8); REQUIRE(dither[6] == 2); REQUIRE(dither[7] == 10);
        }
    }

    // 2. Verify 2x2 dither vector loader for y mod 4 = 0..3
    for (int y = 0; y < 10; ++y) {
        Simd8i dither = GetDitherVector2x2(y);
        int modY = y % 4;

        if (modY == 0 || modY == 2) {
            REQUIRE(dither[0] == 2); REQUIRE(dither[1] == 10); REQUIRE(dither[2] == 2); REQUIRE(dither[3] == 10);
            REQUIRE(dither[4] == 14); REQUIRE(dither[5] == 6); REQUIRE(dither[6] == 14); REQUIRE(dither[7] == 6);
        } else if (modY == 1 || modY == 3) {
            REQUIRE(dither[0] == 14); REQUIRE(dither[1] == 6); REQUIRE(dither[2] == 14); REQUIRE(dither[3] == 6);
            REQUIRE(dither[4] == 2); REQUIRE(dither[5] == 10); REQUIRE(dither[6] == 2); REQUIRE(dither[7] == 10);
        }
    }
}
